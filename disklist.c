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

// Function format_time, turns the created time of an entry into a formatted string
// Takes raw_time from file as input, returns formatted string
void format_time(uint8_t raw[7],char *buf,size_t buf_size) {
	uint16_t year = raw[0] | (raw[1] << 8);
   	uint8_t month = raw[2];
    	uint8_t day   = raw[3];
    	uint8_t hour  = raw[4];
    	uint8_t min   = raw[5];
    	uint8_t sec   = raw[6];

    	snprintf(buf, buf_size, "%04u/%02u/%02u %02u:%02u:%02u",
             ntohs(year), month, day, hour, min, sec);
	return;	
}

// Function list_directory, prints formatted string with contents of directory
// Takes filepath, starting block, block count, and block size as input
// Prints to standard output
void list_directory(FILE *fp,uint32_t fat_start,uint16_t block_size,uint32_t start_block) {
	// Stores the current block
	uint32_t current = start_block;

	// Offset for the next block
	off_t offset;

	// Loops until end of file is reached
	while (current != FAT_EOF) {
		fseek(fp,(off_t)current * block_size, SEEK_SET);

		size_t entries = block_size/sizeof(dir_entry_t);
		dir_entry_t entry;

		// Lists information for each entry
		for (size_t i = 0; i < entries; i++) {
			if (fread(&entry,sizeof(dir_entry_t),1,fp) != 1) break;
			if (entry.status == 0x00) continue; // Unused

			// Determines if entry is a file or directory
			char type;
			if (entry.status & (1 << 1)) type = 'F';
			else type = 'D';

			// Copies the filename to a string with a null terminator
			char name_buf[32];
			memcpy(name_buf,entry.name,31);
			name_buf[31] = '\0';

			// Formats the raw timestamp into a string
			char time_buf[20];
			format_time(entry.created,time_buf,sizeof(time_buf));

			// Prins formatted information
			printf("%c %10u %30s %s\n",
				type,ntohl(entry.size),name_buf,time_buf);
		}

		// Moves offset to next block
		offset = (off_t)fat_start * block_size + (off_t)current * sizeof(uint32_t);
		// Seeks and reads next block
		fseek(fp,offset,SEEK_SET);
		fread(&current,sizeof(uint32_t),1,fp);
		current = ntohl(current);
	}
	return;
}

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

int main(int argc,char *argv[]) {
	// A filename is needed as an argument
	if (argc < 2) {
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

	// Defaults to root directory if no input given, otherwise finds inputted subdirectory
	if (argc == 2 || !strcmp(argv[2],"/")) {
		// Lists contents in root directory
		list_directory(fp,super_block->fat_start,super_block->block_size,super_block->root_start);
	} else {
		uint32_t final_start,final_blocks;

		// Uses helper function to find the target subdirectory	
		if (resolve_path(fp, super_block->root_start, super_block->root_blocks,
					super_block->block_size, argv[2],&final_start, &final_blocks)) {
			// Lists contents in target subdirectory
			list_directory(fp, super_block->fat_start, super_block->block_size, final_start);
		} else {
			printf("Subdirectory \'%s\' not found\n",argv[2]);
		}
	}

	fclose(fp);

	// Free allocated memory
	free(super_block);
	free(fat_table);

	return 0;
}
