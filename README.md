# A Simple File System

## Overview

A simple file system allowing a user to view superblock and FAT info with diskinfo, view directory contents with disklist, copy a file from the system with diskget, and copy a file into the system with diskput. Made for CSC360: Operating Systems.

## Features

### Diskinfo

- Prints superblock and FAT info for an inputted disk image file
- Finds superblock info using given file
- Finds FAT info using superblock

### Disklist

- Prints each entry in the given directory
- Each entry has a filetype, size, name, and date created
- Supports multiple levels of subdirectories

### Diskget

- Copies a specified file from the file system to the current directory
- Supports multiple levels of subdirectories
- Allows renaming of copied file

### Diskput

- Copies a file from the current directory to a specified path in image file
- Creates new subdirectories if not found in file
- Allows renaming of copied file, multiple levels of subdirectories

## Compilation and Execution

Compile with provided Makefile:
`make`
or using:
`gcc diskinfo.c -o diskinfo`
`gcc disklist.c -o disklist`
`gcc diskget.c -o diskget`
`gcc diskput.c -o diskput`


### Diskinfo

Run with a disk image file as input:

`./diskinfo test.img`

### Disklist

Run with a disk image file and optional subdirectory:

`./disklist test.img` Lists root directory

`./disklist test.img /sub_Dir` Lists subdirectory

### Diskget

Run with a disk image file, filepath for target file, and filename for the copy:

`./diskget test.img /sub_Dir/test.txt test_copy.txt`

### Diskput

Run with a disk image file, filename to be copied, filepath with new filename to copy to:

`./diskput test.img test.txt /test_copy.txt` Copies to the root

`./diskput test.img test.txt /sub_Dir/test_copy.txt` Copies to sub_Dir, creating the directory if needed

## Author

Jackson Hagen




