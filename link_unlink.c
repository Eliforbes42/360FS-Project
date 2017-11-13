/* link & unlink -- link_unlink.c */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ext2fs/ext2_fs.h>
#include <string.h>
#include <libgen.h>
#include "type.h"


// global variables
MINODE minode[NMINODE], *root;
struct mntTable mtable[4];
PROC proc[NPROC], *running;
int ninodes, nblocks, bmap, imap, iblock; // from superblock & GD
int dev;
char gline[25], *name[16]; // tokenized component string strings
int nname; // number of component strings
char *rootdev = "vdisk"; // default root_device



/*
Description of file
links and unlinks files in an ext2 file system
*/

/*
int verifyEmptyDir(MINODE *mip)
{
	char buf[BLKSIZE];//block buf
	char *cp;
	DIR *dp;//dir struct
    int numChildren = 0;
    
    get_block(mip->dev, mip->INODE.i_block[0], buf);//get data block of mip->INODE
    
	dp = (DIR *)buf;
    cp = buf;
    
    while (cp + dp->rec_len < buf + BLKSIZE)
    {
		cp += dp->rec_len;
		dp = (DIR *)cp;
		numChildren++;//keep track of how much is in dir
	}
    // dp NOW points at last entry in block
	if(numChildren > 2)
		return 0;//false, aka not empty
	else
		return 1;//true, dir is 'empty' (only has . and ..)
}*/


