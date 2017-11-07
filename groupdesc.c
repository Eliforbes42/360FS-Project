/*  groupdesc.c */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ext2fs/ext2_fs.h>

// typedef u8, u16, u32 SUPER for convenience //
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef struct ext2_super_block SUPER;
typedef struct ext2_group_desc GD;
SUPER *sp;
GD *gp;
#define BLKSIZE 1024
char buf[1024];
int fd, blksize, inodesize;
/*******************************************/

int print(char *s, u32 x)
{
 printf("%-30s = %8d\n", s, x);
}
int get_block(int fd, int blk, char *buf)
{
 lseek(fd, (long)blk*BLKSIZE, SEEK_SET);
 return read(fd, buf, BLKSIZE);
}
int group(char *device)
{
	char *buffer;
	fd = open(device, O_RDONLY);
	if (fd < 0){
		printf("open %sfailed\n", device); 
		exit(1);
	}
    get_block(fd, 2, buffer); // get group descriptor
	gp = (GD *)buf; // cast as group descriptor structure
	
	printf("\n-----Group Descriptor Info-----\n");
	print("bg_block_bitmap", gp->bg_block_bitmap);
	print("bg_inode_bitmap", gp->bg_inode_bitmap);
	print("bg_inode_table", gp->bg_inode_table);
	print("bg_free_blocks_count", gp->bg_free_blocks_count);
	print("bg_free_inodes_count", gp->bg_free_inodes_count);
	print("bg_used_dirs_count", gp->bg_used_dirs_count);
}

char *device = "mydisk"; // default device name
int main(int argc, char *argv[])
{
	if (argc>1)
	device = argv[1];
	group(device);
}