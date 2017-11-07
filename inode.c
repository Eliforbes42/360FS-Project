/*********** inode.c file **********/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <ext2fs/ext2_fs.h>
#define BLKSIZE 1024

// typedef u8, u16, u32 SUPER for convenience //
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef struct ext2_group_desc GD;
typedef struct ext2_super_block SUPER;
typedef struct ext2_inode INODE;
typedef struct ext2_dir_entry_2 DIR;
SUPER *sp;
GD *gp;
INODE *ip;
DIR *dp;
char buf[BLKSIZE];
int fd, firstdata, inodesize, blksize, iblock;
char *dev = "mydisk";
int get_block(int fd, int blk, char *buf)
{
 lseek(fd, blk*BLKSIZE, SEEK_SET);
 return read(fd, buf, BLKSIZE);
}
void parsePath(char *names[], char *pathname)
{
	char *path = pathname;//temp variable so we don't destroy pathname
	if(names[0] = strtok(path, "/"))
		printf("got %s\n", names[0]);

	int i = 1;//already got names[0], so i=1
	while(names[i] = strtok(NULL, "/")){
		printf("while: got %s\n", names[i]);
		i++;
	}
}

int getNameNum(char *nameArr[])
{
	int i = 0;
	while(nameArr[i] != NULL)
	{
		i++;	
	}
	return i;
}
int inode(char *dev)
{
 int i;
 fd = open(dev, O_RDONLY);
 if (fd < 0){
 printf("open failed\n"); exit(1);
 }
 /*************************************
 same code as before to check EXT2 FS
 **************************************/
 get_block(fd, 1, buf);
 sp = (SUPER *)buf; // as a super block structure
 printf("check ext2 FS : ");
 if (sp->s_magic != 0xEF53){
	 printf("NOT an EXT2 FS\n"); exit(2);
 }
 printf("OK\n");	
		
 get_block(fd, 2, buf); // get group descriptor
 gp = (GD *)buf;
 printf("GD info: %d %d %d %d %d %d\n", gp->bg_block_bitmap, gp->bg_inode_bitmap, 
	gp->bg_inode_table, gp->bg_free_blocks_count, gp->bg_free_inodes_count, gp->bg_used_dirs_count);
	
	//bmap_block, imap_block, inodes_table//
 /*printf("bmap = %d imap = %d iblock = %d\n",
 gp->bg_block_bitmap,
 gp->bg_inode_bitmap,
 gp->bg_inode_table);*/
 iblock = gp->bg_inode_table;
 printf("inodes begin block=%d\n", iblock);
 printf("---- root inode information ----\n");
 //printf("--------------------------------\n");
 get_block(fd, iblock, buf);
 ip = (INODE *)buf;
 ip++; // ip point at #2 INODE
 printf("mode = %4x ", ip->i_mode);
 printf("uid = %d gid = %d\n", ip->i_uid, ip->i_gid);
 printf("size = %d\n", ip->i_size);
 printf("ctime = %s", ctime(&ip->i_ctime));
 printf("links = %d\n", ip->i_links_count);
 for (i=0; i<15; i++){ // print disk block numbers
	 if (ip->i_block[i]) // print non-zero blocks only
	 {
		 printf("i_block[%d] = %d\n", i, ip->i_block[i]);
		 
		 //put dirStep.c code here
		 struct ext2_dir_entry_2 *dp; // dir_entry pointer
		 char *cp; // char pointer
		 int blk = ip->i_block[i];
		 // = a data block (number) of a DIR (e.g. i_block[0]);
		 char buf[BLKSIZE], temp[256];
		 get_block(fd, blk, buf); // get data block into buf[ ]
		 printf("fd = %d, blk = %d\n",fd,blk);
		 dp = (struct ext2_dir_entry_2 *)buf; // as dir_entry
		 cp = buf;
		 printf("*************************************\n");
		 printf("inode#  rec_len name_len name\n");
		 while(cp < buf + BLKSIZE)
		 {
			 strncpy(temp, dp->name, dp->name_len); // make name a string
			 temp[dp->name_len] = 0; // ensure NULL at end
			 printf("%d\t%d\t%d\t %s\n", 
					dp->inode, dp->rec_len, dp->name_len, temp);
			 cp += dp->rec_len; // advance cp by rec_len
			 dp = (struct ext2_dir_entry_2 *)cp; // pull dp to next entry
		 } 		 		 
		 //end of dirStep.c code	 
	 }
 }
}

//will need to make driver function 
//and keep the actual searching in this function
u32 search(INODE *iptr, char *name)//INODE *inodePtr,
{	
 int nameInd = 0, i;//use for pathname search
 for (i=0; i<15; i++){ // print disk block numbers
 if (iptr->i_block[i]) // print non-zero blocks only
 {
	 printf("i_block[%d] = %d\n", i, iptr->i_block[i]);

	 //put dirStep.c code here
	 struct ext2_dir_entry_2 *dp; // dir_entry pointer
	 char *cp; // char pointer
	 int blk = iptr->i_block[i];
	 // = a data block (number) of a DIR (e.g. i_block[0]);
	 char buf[BLKSIZE], temp[256];
	 get_block(fd, blk, buf); // get data block into buf[ ]
	 dp = (struct ext2_dir_entry_2 *)buf; // as dir_entry
	 cp = buf;
	 printf("*************************************\n");
	 printf("inode#  rec_len name_len name\n");
	 while(cp < buf + BLKSIZE)
	 {
		 strncpy(temp, dp->name, dp->name_len); // make name a string
		 temp[dp->name_len] = 0; // ensure NULL at end
		 if(strcmp(name, temp) == 0)	//check for name before printing data
		 {
			printf("%d\t%d\t%d\t %s\n", 
				dp->inode, dp->rec_len, dp->name_len, temp);
		  //if(strcmp(name, temp) == 0)//used instead of above 'if' 
			 //result of this -> prints up to the needed item, then returns
		 	return dp->inode;//return inode number to handler
		 }
		 cp += dp->rec_len; // advance cp by rec_len
		 dp = (struct ext2_dir_entry_2 *)cp; // pull dp to next entry
	 } 		 		 
	 //end of dirStep.c code	 
  }
 }
}
u32 traverse(char *dev, char *pathname)
{
 int i, InodeBeginBlock, numNames, blk, offset;
 char *names[32];//name token array
	
 fd = open(dev, O_RDONLY);//open disk for read
 if (fd < 0){//check that it's open
 printf("open failed\n"); exit(1);
 }

 get_block(fd, 1, buf);//get super block
 sp = (SUPER *)buf; // as a super block structure
 printf("check ext2 FS : ");
 if (sp->s_magic != 0xEF53){//checking the magic number of EXT2 FS
	 printf("NOT an EXT2 FS\n"); exit(2);
 }
 printf("OK\n");//good magic
	
 parsePath(names, pathname);//break pathname into each dirName
 numNames = getNameNum(names);
 printf("path has %d parts\n", numNames);
 get_block(fd, 2, buf); // get group descriptor
 gp = (GD *)buf;	//as group block structure
	
 InodeBeginBlock = gp->bg_inode_table;//use GD to get begin block # for inodes
 get_block(fd, InodeBeginBlock, buf);//read the corresponding block
	
 ip = (INODE *)buf;//convert to useable structure
 ip++; //increment ip to point at #2 iNode, aka ROOT
 
 u32 ino;
 for(i = 0; i < numNames; i++)
 {
	 printf("searching for %s\n", names[i]);
	 ino = search(ip, names[i]);
	 printf("after search, ino=%d\n", ino);
	 blk = (ino - 1) / 8 + InodeBeginBlock;//8 inodes per block
	 offset = (ino - 1) % 8;//8 inodes per block
	 printf("blk=%d , offset=%d\n", blk, offset);
	 if(!ino) exit(2);//err out? if no inodeNum
	 //lseek(fd, blk*BLKSIZE+offset, SEEK_SET);
 	 //read(fd, buf, BLKSIZE);
	 get_block(fd, blk, buf);
	 ip = (INODE *)buf + offset;//convert to useable inode
	 //ip++;
 }
	
}

int main(int argc, char *argv[ ])
{
	char *pathname;
 if (argc>1){
 	dev = argv[1];
	inode(dev);
 }
 if (argc>2){
	pathname = argv[2];
	traverse(dev,pathname);
 }
}