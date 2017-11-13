/*util.c*/
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ext2fs/ext2_fs.h>
#include <string.h>
#include <libgen.h>
//#include <sys/stat.h>

#include "type.h"

MINODE minode[NMINODE];
MINODE *root;
PROC   proc[NPROC], *running;
struct mntTable mtable[4]; 

SUPER *sp;
GD    *gp;
INODE *ip;
DIR *dp;

char *names[32];
char *cp;
int n;		 // number of names
int dev;
int nblocks; // from superblock
int ninodes; // from superblock
int bmap;    // bmap block 
int imap;    // imap block 
int iblock;  // inodes begin block

/********** Functions as BEFORE ***********/
int tokenize(char *pathname)
{
    char *path = pathname; //temp variable so we don't destroy pathname
    
	if(names[0] = strtok(path, "/"))
		printf("got %s\n", names[0]);

    int i = 1; //already got names[0], so i=1
    
	while(names[i] = strtok(NULL, "/")){
		i++;
    }
    
	return i; //n = i;
}

int get_block(int fd, int blk, char buf[])
{
    lseek(fd, blk*BLKSIZE, SEEK_SET);
    return read(fd, buf, BLKSIZE);
}

int put_block(int dev, int blk, char buf[])
{
    lseek(dev, blk*BLKSIZE, SEEK_SET);
    return write(dev, buf, BLKSIZE);
}

MINODE *iget(int dev, int ino) //load INODE of (dev,ino) into a minode[]; return mip->minode[]
{
	char buf[BLKSIZE];
	int found = 0, blk, disp = 0, n;
	MINODE *mip = malloc(sizeof(MINODE));
    INODE *tip = malloc(sizeof(INODE));
    
	if(ino == 0)
        return;
        
	//printf("iget dev=%d , ino=%d\n",dev,ino);
    //search minode[ ] array for an item pointed by mip with the SAME (dev,ino)

	for(int i = 0; i < NMINODE; i++){
	 	if(minode[i].dev == dev && minode[i].ino == ino){
			*mip = minode[i];
	 		found = 1;
			n = i;
			break;
		}
    }
    
    if (found){
       mip->refCount++;  //inc user count by 1
       return mip;
    }

    //printf("iget, after first forloop\n");
    //search minode[ ] array for a mip whose refCount=0:

	for(int i = 0; i < NMINODE; i++){
	 	 if(minode[i].refCount == 0){
            //printf("got a node with 0 refCount\n");
			*mip = minode[i]; 
		 	mip->refCount = 1;   // mark it in use
			mip->dev = dev;
            mip->ino = ino;//minode[i].ino;
            
            //printf("inFor: mipDev=%d , mipIno=%d\n",mip->dev, mip->ino);
            //assign it to (dev, ino);
            
			mip->dirty = 0;
            mip->mounted = 0;
            
            //mip->mptr = NULL;//initialize other fields: dirty, mounted, mountPtr
            
            n = i;
            
			break;
		}
	}

    //ask mailman to compute

    blk  = (ino - 1) / 8 + iblock; //8 inodes per block
    disp = (ino - 1) % 8;
    
    //printf("blk=%d , disp=%d\n",blk,disp);
    
	if(disp<0)
        disp = 0;
        
	//printf("blk=%d , disp=%d\n",blk,disp);
    //load blk into buf[ ];

    get_block(dev, blk, buf); //printf("got block into buf\n");	
     
	//INODE *ip point at INODE in buf[ ];
	tip = (INODE *)buf + disp;

    //copy INODE into minode.INODE by
    mip->INODE = *tip; //ADD DIR CHECK? if(S_ISDIR(tip->i_mode))

    //printf("mipDev=%d , mipIno=%d\n",mip->dev,mip->ino);

    return mip;
}

int iput(MINODE *mip)  // dispose of a minode[] pointed by mip
{
	char buf[BLKSIZE];
	int block, disp;
	
    mip->refCount--; 
    
    if (mip->refCount > 0) 
        return;
    if (!mip->dirty)       
        return;
 
	/* write INODE back to disk */
	printf("iput: dev=%d ino=%d\n", mip->dev, mip->ino); 

    //ask mailman to compute
    block  = (mip->ino - 1) / 8 + iblock; //8 inodes per block
    disp = (mip->ino - 1) % 8; //Use mip->ino to compute 

    //block = blk# containing this INODE
    //disp  = offset of INODE in buf

    get_block(mip->dev, block, buf);

    ip = (INODE *)buf + disp;
    *ip = mip->INODE; // copy INODE into *ip

    put_block(mip->dev, block, buf);
}

int search(MINODE *mip, char *name)
{
    char buf[BLKSIZE];
    char temp[256];
    
    get_block(dev, mip->INODE.i_block[0], buf);

    dp = (struct ext2_dir_entry_2 *)buf;
    cp = buf;

    while(cp < buf + BLKSIZE)
    {
        strncpy(temp, dp->name, dp->name_len);

        temp[dp->name_len] = 0;

        if(strncmp(temp, name, dp->name_len) == 0 && (strlen(name) == dp->name_len)){
            printf("found\n");
            return dp->inode;
        }
        else{
            cp += dp->rec_len;
            dp = (struct ext2_dir_entry_2 *)cp;
        }
    }

    printf("not found\n");
    
    return 0;
}

int getino(char *pathname)
{
    int i, ino, blk, disp;
    char *buf; //char buf[BLKSIZE];
    INODE *ip;
    MINODE *mip;
    dev = root->dev; //only ONE device so far

    printf("getino: pathname=%s\n", pathname);

    if(strcmp(pathname, "/")==0)
        return 2;

    //printf("getino:1\n");

    if(pathname[0]=='/')
        mip = iget(dev, 2);
    else
        mip = iget(dev, proc[0].cwd->ino);

    //printf("getino:2\n");

    buf = pathname; //strcpy(buf, pathname);

    //printf("getino:2.5\n");

    n = tokenize(buf); //n = number of token strings

    //printf("3\n");

    for (i=0; i < n; i++){
        printf("===========================================\n");

        printf("getino: i=%d name[%d]=%s\n", i, i, names[i]);
 
        ino = search(mip, names[i]);

        if(ino==0){
            iput(mip);

            printf("name %s does not exist\n", names[i]);

            return 0;
        }

        iput(mip);

        mip = iget(dev, ino);
    }

    iput(mip);

    return ino;
}