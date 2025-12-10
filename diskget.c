#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

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

// Function resolve_path, uses find_subdir to locate a specified subdirectory
// Supports multiple levels of subdirectories by using string tokenization
// Returns 1 if successful, 0 otherwise
int resolve_path(FILE *fp,uint32_t root_start,uint32_t root_blocks,uint32_t block_size,
		const char *path,uint32_t *out_start,uint32_t *out_blocks) {

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
            		free(path_copy);
            		return 0;
        	}

        	// Advance into the found subdirectory
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

// Function copy_file, copies the target file to the user's current directory
// Entry to be copied and new filename are given as arguments
void copy_file(FILE *fp,uint32_t fat_start,uint32_t block_size,const dir_entry_t *entry,
		const char *filename) {
	// Opens the new file to write binary in
	FILE *out = fopen(filename,"wb");
	
	uint32_t current = ntohl(entry->starting_block);
	uint32_t remaining = ntohl(entry->size);

	// Writes everything until the end of the file
	while (current != FAT_EOF && remaining > 0) {
		fseek(fp,(off_t)current * block_size,SEEK_SET);

		size_t to_read = remaining < block_size ? remaining : block_size;
		char *buf = malloc(block_size);
		fread(buf,1,to_read,fp); // Reads from file
		fwrite(buf,1,to_read,out); // Writes to new file
		free(buf);

		remaining -= to_read;

		off_t offset = (off_t)fat_start * block_size + (off_t)current * sizeof(uint32_t);
		fseek(fp,offset,SEEK_SET);
		fread(&current,sizeof(uint32_t),1,fp);
		current = ntohl(current);
	}
	fclose(out);
	return;
}

int main(int argc,char *argv[]) {
	// A filename is needed as an argument
	if (argc < 3) {
		perror("Error: Not enough arguments");
		exit(1);
	}

	// Skips the file system ID, which is 8 bytes
	off_t offset = 8;
	// Creates a new super_block struct and allocates memory
	super_block_t *super_block = malloc(sizeof(super_block_t));

	// Opens the inputted file in read binary mode
	FILE* fp = fopen(argv[1],"rb");
	
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
	char *path_copy = strdup(argv[2]);
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
					super_block->block_size,dirpath,&dir_start,&dir_blocks)) {
		printf("Requested file %s not found in %s.\n",filename,dirpath);
		exit(1);
	}

	dir_entry_t entry;
	
	// Attempts to find the target file in the directory
	if (!find_file(fp,dir_start,dir_blocks,super_block->fat_start,super_block->block_size,filename,&entry)) {
		printf("Requested file %s not found in %s.\n",filename,dirpath);
		exit(1);
	}

	// Copies file to current directory
	copy_file(fp,super_block->fat_start,super_block->block_size,&entry,argv[3]);

	fclose(fp);

	// Free allocated memory
	free(super_block);
	free(fat_table);
	free(path_copy);

	return 0;
}

