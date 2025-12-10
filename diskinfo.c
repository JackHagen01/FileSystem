#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

// Structure super_block_t, stores information for the superblock
typedef struct {
	uint16_t block_size;
	uint32_t block_count;
	uint32_t fat_start;
	uint32_t fat_blocks;
	uint32_t root_start;
	uint32_t root_blocks;
} __attribute__((packed)) super_block_t;

// Structure fat_t, stores information for the FAT
typedef struct {
	uint32_t free_blocks;
	uint32_t reserved_blocks;
	uint32_t allocated_blocks;
} __attribute__((packed)) fat_t;

// Function print_super_block, prints the information of the superblock
// Takes a superblock struct as input, prints to standard output
void print_super_block(super_block_t *super_block) {
	// Formats and prints values
	printf("Super block information:\nBlock size: %u\nBlock count: %u\nFAT starts: %u\nFAT blocks: %u\nRoot directory start: %u\nRoot directory blocks: %u\n",
		super_block->block_size,super_block->block_count,super_block->fat_start,
		super_block->fat_blocks,super_block->root_start,super_block->root_blocks);
	return;
}


// Function print_fat, finds and prints information about the FAT
// Takes an FAT struct, FAT table from the file, and the size of the FAT as input
// Prints to standard output
void print_fat(fat_t *fat,uint32_t *fat_table,size_t fat_size) {
	// Iterates through every block and increments values
	for (uint32_t i = 0; i < fat_size/4; i++) {
		switch (ntohl(fat_table[i])) {
			case 0x00000000:
				fat->free_blocks++;
				break;
			case 0x00000001:
				fat->reserved_blocks++;
				break;
			default:
				fat->allocated_blocks++;
				break;
		}
	}
	// Prints formatted information
	printf("\nFAT information:\nFree blocks: %u\nReserved blocks: %u\nAllocated blocks: %u\n",
			fat->free_blocks,fat->reserved_blocks,fat->allocated_blocks);
	return;
}

int main(int argc,char *argv[]) {
	// A filename is needed as an argument
	if (argc < 2) {
		perror("Error: No file inputted");
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

	// Creates FAT structure and allocates memory
	fat_t *fat = malloc(sizeof(fat_t));

	// Determines size of the FAT
	size_t fat_size = super_block->block_size * super_block->fat_blocks;
	
	// Used to hold values read from the file
	uint32_t *fat_table = malloc(fat_size);

	// Moves to the start of the FAT
	offset = super_block->block_size * super_block->fat_start;
	fseek(fp,offset,SEEK_SET);
	// Reads the FAT to fat_table
	fread(fat_table,sizeof(uint32_t),fat_size/sizeof(uint32_t),fp);

	fclose(fp);
	
	// Prints the formatted superblock information
	print_super_block(super_block);

	// Prints the formatted FAT information
	print_fat(fat,fat_table,fat_size);

	// Frees allocated memory
	free(super_block);
	free(fat);

	return(0);
}
