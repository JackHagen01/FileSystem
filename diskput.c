#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <time.h>

#define FAT_EOF 0xFFFFFFFF

// Structure super_block_t, stores information for the superblock
typedef struct {
	uint16_t block_size;
	uint32_t block_count;
	uint32_t fat_start;
	uint32_t fat_blocks;
	uint32_t root_start;
	uint32_t root_blocks;
} __attribute__((packed)) super_block_t;

// Structure dir_entry_t, stores information about directory entries
typedef struct {
	uint8_t status;
	uint32_t starting_block;
	uint32_t block_count;
	uint32_t size;
	uint8_t created[7];
	uint8_t modified[7];
	char name[31];
	uint8_t unused[6];
} __attribute__((packed)) dir_entry_t;

// Function find_subdir, returns 1 if found, 0 if not
// Takes filepath, start blcok, block count, block size, target directory, and output variables as input
int find_subdir (FILE *fp, uint32_t start_block, uint32_t block_count,
		uint16_t block_size, char *target,
		uint32_t *out_start, uint32_t *out_blocks) {
	// Finds offset of target
	off_t offset = (off_t)start_block * block_size;
	// Finds size of target
	size_t size = (size_t)block_count * block_size;

	// Moves to target
	fseek(fp,offset,SEEK_SET);

	dir_entry_t entry;
	size_t entries = size/sizeof(dir_entry_t);

	// Iterates until target is found
	for (size_t i = 0; i < entries; i++) {
		if (fread(&entry,sizeof(dir_entry_t),1,fp) != 1) break;
		if (entry.status == 0x00) continue; // Unused

		// A directory is found
		if (entry.status & (1 << 2)) {
			// Copies entry name
			char name_buf[32];
			memcpy(name_buf,entry.name,31);
			name_buf[31] = '\0';

			// Trims trailing spaces
			for (int j = 30; j >= 0; j--) {
				if (name_buf[j] == '\0' || name_buf[j] == ' ' || name_buf[j] == 0x00) name_buf[j] = '\0';
				else break;
			}

			// Compares current entry to target directory
			if (strcmp(name_buf,target) == 0) {
				*out_start = ntohl(entry.starting_block);
				*out_blocks = ntohl(entry.block_count);
				return 1;
			}
		}
	}
	return 0; // Target not found
}

uint32_t allocate_block(FILE *fp,uint32_t fat_start,uint32_t block_size) {
	uint32_t block_num = 0;
	uint32_t fat_entry;

	uint32_t block_entries = block_size/sizeof(uint32_t);

	for (uint32_t b = 0; ; b++) {
		off_t fat_off = (off_t)fat_start * block_size + b * block_size;
		fseek(fp,fat_off,SEEK_SET);

		for (uint32_t i = 0; i < block_entries; i++) {
			if (fread(&fat_entry,sizeof(fat_entry),1,fp) != 1) break;
			if (ntohl(fat_entry) == 0) {
				block_num = b * block_entries + i;

				fat_entry = htonl(FAT_EOF);
				fseek(fp,fat_off + 1 * sizeof(uint32_t),SEEK_SET);
				fwrite(&fat_entry,sizeof(fat_entry),1,fp);
				return block_num;
			}
		}
		if (feof(fp)) break;
	}
	return 0;
}

void init_directory(FILE *fp,uint32_t block_num,uint32_t block_size) {
	dir_entry_t empty = {0};
	const size_t entries = block_size/sizeof(dir_entry_t);
	off_t off = (off_t)block_num * block_size;
	fseek(fp,off,SEEK_SET);
	for (size_t i = 0; i < entries; i++) {
		fwrite(&empty,sizeof(empty),1,fp);
	}
	fflush(fp);
	return;
}

int write_entry(FILE *fp,uint32_t dir_start,uint32_t block_size,uint32_t fat_start,const dir_entry_t *entry) {
	dir_entry_t current;
	size_t block_entries = block_size/sizeof(dir_entry_t);

	uint32_t current_block = dir_start;
	uint32_t last_block = dir_start;
	while (current_block != FAT_EOF) {
		off_t offset = (off_t)current_block * block_size;

		for (size_t i = 0; i < block_entries; i++) {
			fseek(fp,offset + (off_t)i * sizeof(dir_entry_t),SEEK_SET);
			if (fread(&current,sizeof(current),1,fp) != 1) break;

			if (current.status == 0x00) {
				fseek(fp,offset + (off_t)i * sizeof(dir_entry_t),SEEK_SET);
				fwrite(entry,sizeof(*entry),1,fp);
				fflush(fp);
				return 1;
			}
		}

		last_block = current_block;
		off_t fat_off = (off_t)fat_start * block_size + (off_t)current_block * sizeof(uint32_t);
		fseek(fp,fat_off,SEEK_SET);
		fread(&current_block,sizeof(uint32_t),1,fp);
		current_block = ntohl(current_block);
	}

	uint32_t new_block = allocate_block(fp,fat_start,block_size);
	if (new_block == 0) return 0;

	off_t fat_off = (off_t)fat_start * block_size + (off_t)last_block * sizeof(uint32_t);
	fseek(fp,fat_off,SEEK_SET);
	uint32_t link = htonl(new_block);
	fwrite(&link,sizeof(link),1,fp);

	fat_off = (off_t)fat_start * block_size + (off_t)new_block * sizeof(uint32_t);
	fseek(fp,fat_off,SEEK_SET);
	uint32_t eof = htonl(FAT_EOF);
	fwrite(&eof,sizeof(eof),1,fp);

	init_directory(fp,new_block,block_size);

	off_t offset = (off_t)new_block * block_size;
	fseek(fp,offset,SEEK_SET);
	fwrite(entry,sizeof(*entry),1,fp);
	fflush(fp);

	return 1;
}


void fill_timestamp(dir_entry_t *entry) {
	time_t now = time(NULL);
	struct tm *tm_now = localtime(&now);

	uint16_t year = tm_now->tm_year + 1900;
	entry->created[0] = (year >> 8) & 0xFF;
	entry->created[1] = year & 0xFF;
	entry->created[2] = tm_now->tm_mon + 1;
	entry->created[3] = tm_now->tm_mday;
	entry->created[4] = tm_now->tm_hour;
	entry->created[5] = tm_now->tm_min;
	entry->created[6] = tm_now->tm_sec;
	
	return;
}

// Function resolve_path, uses find_subdir to locate a specified subdirectory
// Supports multiple levels of subdirectories by using string tokenization
// Returns 1 if successful, 0 otherwise
int resolve_path(FILE *fp,uint32_t root_start,uint32_t root_blocks,uint32_t block_size,
		const char *path,uint32_t fat_start,uint32_t *out_start,uint32_t *out_blocks) {

	// Skip leading slash if present
    	if (path[0] == '/') path++;

    	// Make a copy of the path for strtok
    	char *path_copy = strdup(path);
    	if (!path_copy) return 0;

    	// Path is broken down into sections seperated by /, which indicates subdirectories
    	char *token = strtok(path_copy, "/");
    	uint32_t current_start = root_start;
    	uint32_t current_blocks = root_blocks;

    	// Checks each token
    	while (token) {
        	uint32_t sub_start = 0, sub_blocks = 0;

        	// Calls find_subdir to determine if the subdirectory is present
        	if (!find_subdir(fp,current_start,current_blocks,block_size,token,&sub_start,&sub_blocks)) {
            		sub_start = allocate_block(fp,fat_start,block_size);
			sub_blocks = 1;
			init_directory(fp,sub_start,block_size);

			dir_entry_t new_entry = {0};
			new_entry.status = 0x04;
			strncpy(new_entry.name,token,sizeof(new_entry.name)-1);
			new_entry.starting_block = htonl(sub_start);
			new_entry.block_count = ntohl(sub_blocks);
			new_entry.size = htonl(0);

			fill_timestamp(&new_entry);
        		
			write_entry(fp,current_start,block_size,fat_start,&new_entry);
		}

        	// Advance into the subdirectory
        	current_start = sub_start;
        	current_blocks = sub_blocks;

        	token = strtok(NULL, "/");
    	}

    	free(path_copy);

    	*out_start = current_start;
    	*out_blocks = current_blocks;
    	return 1;
}

// Function find_file, locates the target file within a directory
// Returns 1 if successful, 0 otherwise
// Saves target entry to out_entry
int find_file(FILE *fp,uint32_t start,uint32_t blocks,uint32_t fat_start,uint32_t block_size,
		const char *filename, dir_entry_t *out_entry) {
	size_t block_entries = block_size/sizeof(dir_entry_t);
	dir_entry_t entry;

	uint32_t current = start;
	
	// Iterates through every entry in the directory
	while (current != FAT_EOF) {
		fseek(fp,(off_t)current * block_size,SEEK_SET);
		for (size_t i = 0; i < block_entries; i++) {
			if (fread(&entry,sizeof(dir_entry_t),1,fp) != 1) break;
			if (entry.status == 0x00) continue;

			char name_buf[32];
			strncpy(name_buf,entry.name,sizeof(name_buf)-1);
			name_buf[31] = '\0';

			// Checks file type and name with target
			if (entry.status & (1 << 1) && !strcmp(name_buf,filename)) {
				*out_entry = entry;
				return 1;
			}
		}
		off_t offset = (off_t)fat_start * block_size + (off_t)current * sizeof(uint32_t);
		fseek(fp,offset,SEEK_SET);
		fread(&current,sizeof(uint32_t),1,fp);
		current = ntohl(current);
	}
	return 0;
}

uint32_t allocate_fat(FILE *fp,uint32_t fat_start,uint32_t block_size,size_t filesize) {
	uint32_t blocks_needed = (filesize + block_size - 1)/block_size;
	uint32_t first_block = 0,prev_block = 0;

	for (uint32_t b = 0; b < blocks_needed; b++) {
		uint32_t free_block = allocate_block(fp,fat_start,block_size);
		if (free_block == 0) {
			printf("No free blocks available\n");
			return 0;
		}

		if (first_block == 0) first_block = free_block;

		if (prev_block != 0) {
			off_t fat_off = (off_t)fat_start * block_size + prev_block * sizeof(uint32_t);
			fseek(fp,fat_off,SEEK_SET);
			uint32_t link = htonl(free_block);
			fwrite(&link,sizeof(link),1,fp);
		}
		prev_block = free_block;
	}
	off_t fat_off = (off_t)fat_start * block_size + prev_block * sizeof(uint32_t);
	fseek(fp,fat_off,SEEK_SET);
	uint32_t eof = htonl(FAT_EOF);
	fwrite(&eof,sizeof(eof),1,fp);

	return first_block;
}

void write_file(FILE *fp,FILE *src,uint32_t block_size,uint32_t first_block,
		size_t filesize,uint32_t fat_start) {
	uint32_t current = first_block;
	size_t remaining = filesize;
	char *buf = malloc(block_size);

	while (current != FAT_EOF && remaining > 0) {
		size_t to_read = remaining < block_size ? remaining : block_size;
		fread(buf,1,to_read,src);

		fseek(fp,(off_t)current * block_size,SEEK_SET);
		fwrite(buf,1,to_read,fp);

		remaining -= to_read;

		off_t fat_off = (off_t)fat_start * block_size + current * sizeof(uint32_t);
		fseek(fp,fat_off,SEEK_SET);
		fread(&current,sizeof(uint32_t),1,fp);
		current = ntohl(current);
	}
	free(buf);
	return;
}

int main(int argc,char *argv[]) {
	// A filename is needed as an argument
	if (argc < 3) {
		perror("Error: Not enough arguments");
		exit(1);
	}

	FILE *src = fopen(argv[2], "rb");
	if (!src) {
		printf("Source file %s not found.\n",argv[2]);
		exit(1);
	}

	// Skips the file system ID, which is 8 bytes
	off_t offset = 8;
	// Creates a new super_block struct and allocates memory
	super_block_t *super_block = malloc(sizeof(super_block_t));

	// Opens the inputted file in read binary mode
	FILE* fp = fopen(argv[1],"rb+");
	
	if (!fp) {
		perror("Error: File Invalid");
		exit(1);
	}

	// Moves to the specified offset, after the ID
	fseek(fp,offset,SEEK_SET);
	// Reads superblock information to the struct
	fread(super_block,sizeof(super_block_t),1,fp);

	// Superblock values are converted to the correct endianness
	super_block->block_size = ntohs(super_block->block_size);
	super_block->block_count = ntohl(super_block->block_count);
	super_block->fat_start = ntohl(super_block->fat_start);
	super_block->fat_blocks = ntohl(super_block->fat_blocks);
	super_block->root_start = ntohl(super_block->root_start);
	super_block->root_blocks = ntohl(super_block->root_blocks);

	// Determines size of the FAT
	size_t fat_size = super_block->block_size * super_block->fat_blocks;
	
	// Used to hold values read from the file
	uint32_t *fat_table = malloc(fat_size);

	// Moves to the start of the FAT
	offset = super_block->block_size * super_block->fat_start;
	fseek(fp,offset,SEEK_SET);
	// Reads the FAT to fat_table
	fread(fat_table,sizeof(uint32_t),fat_size/sizeof(uint32_t),fp);

	// Copies path and seperates filename
	char *path_copy = strdup(argv[3]);
	char *filename = strrchr(path_copy, '/');
	char *dirpath;

	// Checks if target file is in the root directory
	if (filename) {
		*filename++ = '\0'; // Removes dirpath from filename
		dirpath = path_copy;
	} else {
		filename = path_copy;
		dirpath = "/"; // Root
	}

	uint32_t dir_start,dir_blocks;

	// Attempts to find the directory of the target file
	if (!resolve_path(fp, super_block->root_start, super_block->root_blocks,
					super_block->block_size,dirpath,super_block->fat_start,&dir_start,&dir_blocks)) {
		printf("Failed to create directory %s\n",dirpath);
		exit(1);
	}

	fseek(src,0,SEEK_END);
	size_t filesize = ftell(src);
	rewind(src);

	uint32_t first_block = allocate_fat(fp,super_block->fat_start,super_block->block_size,filesize);

	write_file(fp,src,super_block->block_size,first_block,filesize,super_block->fat_start);

	dir_entry_t entry = {0};
	entry.status = 0x02;
	strncpy(entry.name,filename,sizeof(entry.name)-1);
	entry.starting_block = htonl(first_block);
	entry.block_count = htonl((filesize + super_block->block_size - 1)/super_block->block_size);
	entry.size = htonl(filesize);
	fill_timestamp(&entry);
	write_entry(fp,dir_start,super_block->block_size,super_block->fat_start,&entry);
	

	fclose(src);
	fclose(fp);

	// Free allocated memory
	free(super_block);
	free(fat_table);
	free(path_copy);

	return 0;
}

