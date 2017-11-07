/*************** imap.c program **************/
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ext2fs/ext2_fs.h>
typedef struct ext2_super_block SUPER;
typedef struct ext2_group_desc GD;
typedef unsigned char u8; 
#define BLKSIZE 1024
SUPER *sp;
GD *gp;
char buf[BLKSIZE];
int fd;

char* itoa(int val, int base){//strudel.org.uk/itoa/
	static char buf[32] = {0};
	int i = 30;
	
	for(; val && i ; --i, val /= base)	
		buf[i] = "0123456789abcdef"[val % base];	
	return &buf[i+1];	
}
// get_block() reads a disk block into a buf[ ]
int get_block(int fd, int blk, char *buf)
{
 lseek(fd, (long)blk*BLKSIZE, SEEK_SET);
 return read(fd, buf, BLKSIZE);
}
int imap(char *device)
{
 int i, ninodes, blksize, imapblk;
 fd = open(device, O_RDONLY);
 if (fd < 0){
	 printf("open %s failed\n", device); exit(1);
 }
 get_block(fd, 1, buf); // get superblock
 sp = (SUPER *)buf;
 // check magic number to ensure it’s an EXT2 FS
 ninodes = sp->s_inodes_count; // get inodes_count
 printf("ninodes = %d   ", ninodes);
 get_block(fd, 2, buf); // get group descriptor
 gp = (GD *)buf;
 imapblk = gp->bg_inode_bitmap; // get imap block number
 printf("imapblk = %d\n", imapblk);
 get_block(fd, imapblk, buf); // get imap block into buf[ ]
 
 char* binaryN;
 for (i=0; i<=ninodes/8; i++){ 
 	//printf("%02x ", (u8)buf[i]);  // print each byte in HEX
	 binaryN = itoa((u8)buf[i], 2);
	 printf("%s", binaryN);
	 if((u8)buf[i] == 0)
		 printf("00000000 ");
	 else if((u8)buf[i] < 255 && (u8)buf[i] > 0)
		 printf("00000 ");
	 else
		 printf(" ");
 }
 printf("\n");
}

int bmap(char *device)
{
	int i, ninodes, blksize, bmapblk;
	fd = open(device, O_RDONLY);
	if (fd < 0){
		printf("open %s failed\n", device); exit(1);
	}
	get_block(fd, 1, buf); // get superblock
	sp = (SUPER *)buf;
	// check magic number to ensure it’s an EXT2 FS
	ninodes = sp->s_inodes_count; // get inodes_count
	printf("ninodes = %d   ", ninodes);
	get_block(fd, 2, buf); // get group descriptor
	gp = (GD *)buf;
	bmapblk = gp->bg_block_bitmap; // get imap block number
	printf("bmapblk = %d\n", bmapblk);
	get_block(fd, bmapblk, buf); // get imap block into buf[ ]
	
	char* binaryN;
 	for (i=0; i<=ninodes/8; i++)
	{ 
 	//printf("%02x ", (u8)buf[i]);  // print each byte in HEX
	 binaryN = itoa((u8)buf[i], 2);
	 printf("%s", binaryN);
	 if((u8)buf[i] == 0)
		 printf("00000000 ");
	 else if((u8)buf[i] == 63)
		 printf("00 ");
	 else if((u8)buf[i] < 255 && (u8)buf[i] > 0)
		 printf("00000 ");
	 else
		 printf(" ");
   }
 printf("\n");
}

char *dev="mydisk"; // default device
int main(int argc, char *argv[ ] )
{
if (argc>1) dev = argv[1];

 printf("************imap************\n");
 imap(dev);
 printf("\n************bmap************\n");
 bmap(dev);
 printf("\n");
}