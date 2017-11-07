/* mkdir & creat -- mkdirCreat.c */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ext2fs/ext2_fs.h>
#include <string.h>
#include <libgen.h>
//#include <sys/stat.h>
#include "type.h"
//#include "util.c"

// global variables
MINODE minode[NMINODE], *root;
struct mntTable mtable[4];
PROC proc[NPROC], *running;
int ninodes, nblocks, bmap, imap, iblock; // from superblock & GD
int dev;
char gline[25], *name[16]; // tokenized component string strings
int nname; // number of component strings
char *rootdev = "vdisk"; // default root_device

// tst_bit, set_bit functions
int tst_bit(char *buf, int bit){
	return buf[bit/8] & (1 << (bit % 8));
}
int set_bit(char *buf, int bit){
	buf[bit/8] |= (1 << (bit % 8));
}
int clr_bit(char *buf, int bit){ // clear bit in char buf[BLKSIZE]
	buf[bit/8] &= ~(1 << (bit % 8)); 
}
int decFreeInodes(int dev){
	SUPER *sp;
	GD *gp;
	char buf[BLKSIZE];
	// dec free inodes count in SUPER and GD
	get_block(dev, 1, buf);
	sp = (SUPER *)buf;
	sp->s_free_inodes_count--;
	put_block(dev, 1, buf);
	get_block(dev, 2, buf);
	gp = (GD *)buf;
	gp->bg_free_inodes_count--;
	put_block(dev, 2, buf);
}
int incFreeInodes(int dev){
	SUPER *sp;
	GD *gp;
	char buf[BLKSIZE];
	// inc free inodes count in SUPER and GD
	get_block(dev, 1, buf);
	sp = (SUPER *)buf;
	sp->s_free_inodes_count++;
	put_block(dev, 1, buf);
	get_block(dev, 2, buf);
	gp = (GD *)buf;
	gp->bg_free_inodes_count++;
	put_block(dev, 2, buf);
}
int decFreeBlocks(int dev){
	SUPER *sp;
	GD *gp;
	char buf[BLKSIZE];
	// dec free block count in SUPER and GD
	get_block(dev, 1, buf);//get super
	sp = (SUPER *)buf;
	sp->s_free_blocks_count--;
	put_block(dev, 1, buf);//write back dirty super
	get_block(dev, 2, buf);//get group desc
	gp = (GD *)buf;
	gp->bg_free_blocks_count--;
	put_block(dev, 2, buf);//write back dirty group desc
}
int incFreeBlocks(int dev){
	SUPER *sp;
	GD *gp;
	char buf[BLKSIZE];
	// inc free block count in SUPER and GD
	get_block(dev, 1, buf);//get super
	sp = (SUPER *)buf;
	sp->s_free_blocks_count++;
	put_block(dev, 1, buf);//write back dirty super
	get_block(dev, 2, buf);//get group desc
	gp = (GD *)buf;
	gp->bg_free_blocks_count++;
	put_block(dev, 2, buf);//write back dirty group desc
}

int ialloc(int dev){
	int i;
	char buf[BLKSIZE];
	SUPER *sp;
	dev = open(rootdev, O_RDRW);//open disk for read//open device for RW
	  get_block(dev, 1, buf);//    read SUPER block to verify it's an EXT2 FS
	  sp = (SUPER *)buf; // as a super block structure
	ninodes = sp->s_inodes_count;
	
	// assume imap, bmap are globals from superblock and GD
	get_block(dev, imap, buf);
	for (i=0; i<ninodes; i++){
		if (tst_bit(buf, i)==0){
			set_bit(buf, i);
			put_block(dev, imap, buf);
			// update free inode count in SUPER and GD
			decFreeInodes(dev);
			return (i+1);
		}
	}
	return 0; // out of FREE inodes
}
int idalloc(int dev, int ino){
	int i;
	char buf[BLKSIZE];
	SUPER *sp;
	dev = open(rootdev, O_RDRW);//open disk for read//open device for RW
	  get_block(dev, 1, buf);//    read SUPER block to verify it's an EXT2 FS
	  sp = (SUPER *)buf; // as a super block structure
	ninodes = sp->s_inodes_count;
	if (ino > ninodes){ // niodes global
	printf("inumber %d out of range\n", ino);
	return;
	}
	// get inode bitmap block
	get_block(dev, imap, buf);
	clr_bit(buf, ino-1);
	// write buf back
	put_block(dev, imap, buf);
	// update free inode count in SUPER and GD
	incFreeInodes(dev);
	
}
int balloc(int dev){
	int i;
	char buf[BLKSIZE];
	
	SUPER *sp;
	dev = open(rootdev, O_RDRW);//open disk for read//open device for RW
	  get_block(dev, 1, buf);//    read SUPER block to verify it's an EXT2 FS
	  sp = (SUPER *)buf; // as a super block structure
	nblocks = sp->s_blocks_count;
	
	// assume imap, bmap are globals from superblock and GD
	get_block(dev, bmap, buf);
	for (i=0; i<nblocks; i++){
		if (tst_bit(buf, i)==0){
			set_bit(buf, i);			
			// update free block count in SUPER and GD
			decFreeInodes(dev);
			put_block(dev, bmap, buf);
			return i;
		}
	}
	return 0; // out of FREE blocks
}
int bdalloc(int dev, int bno)
{
	char buf[BLKSIZE];
	get_block(dev, bmap, buf);//get block bitmap block
	clr_bit(buf, bno);
	put_block(dev, bmap, buf);//put updated block back
	incFreeBlocks(dev);//inc free block # in Super && GD blocks
}
int enter_child(MINODE *pmip, int ino, char *child)
{
INODE *pip = &pmip->INODE; 
char *cp; DIR *dp;
int ideal_length, name_len, need_length, remain, blk, i;

char buf[BLKSIZE];
for(i = 0; i<12; i++)
{
	if(pip->i_block[i] == 0)
		break;
	blk = pip->i_block[i];

	//get pip->inode->i_block[], and read parent block into buf[]		
	get_block(pmip->dev, blk, buf);
	dp = (DIR *)buf;
	cp = buf;

	name_len = strlen(child);//calculate lengths
	need_length = 4*( (8 + name_len + 3)/4 );

	while (cp + dp->rec_len < buf + BLKSIZE){

		cp += dp->rec_len;
		dp = (DIR *)cp;
	}
	cp = (char *)dp;
	ideal_length = 4*( (8 + name_len + 3)/4 );
	// dp NOW points at last entry in block
	remain = dp->rec_len - ideal_length;//remain = last rec_len - ideal_length

	if (remain >= need_length){
		dp->rec_len = ideal_length;

		cp += dp->rec_len;//step forward for new entry
		dp = (DIR *)cp;		

		//enter the new entry as the LAST entry
		dp->inode = ino;
		dp->rec_len = remain;//dp->rec_len = BLKSIZE - ((u32)cp - (u32)buf);
		dp->name_len = name_len;//=strlen(child)
		//now set name, since have all info
		strcpy(dp->name, child);
		put_block(dev, blk , buf);//write data block to disk

		return 1;
	}					
}
blk = balloc(dev);	//balloc a new block
pip->i_block[i] = blk;//set block to next i_block[], likely i_block[1] for now

pip->i_size += BLKSIZE;//increment parent size by BLKSIZE
pmip->dirty = 1;//mark dirty

get_block(pmip->dev, blk, buf);

dp = (DIR *)buf;
cp = buf;

//and enter child as first entry with rec_len=BLKSIZE-...
dp->inode = ino;		//.
dp->rec_len = 1024;
dp->name_len = name_len;//..
strcpy(dp->name, child);//...initialize all dirEntry data	
//	}			
//write data block to disk
put_block(dev, blk , buf);	
return 1;
}

int my_mkdir(MINODE *pmip, char *child)
{
	char buf[BLKSIZE]; char *cp;
	DIR *dp;
	//(4).1. Allocate an INODE and a disk block:
 	int ino = ialloc(dev); printf("ino=%d\n",ino);
 	int blk = balloc(dev);
	printf("myMkdir: pino=%d, ino=%d, blk=%d\n", pmip->ino, ino, blk);
	//(4).2 		
	 MINODE *mip = iget(dev, ino);  // load INODE into a minode
	 INODE *ip = &mip->INODE; //initialize mip->INODE as a DIR INODE;
	
	 mip->refCount = 0;
	 ip->i_mode = 0x41ED; // octal 040755: DIR type and permissions
	 ip->i_uid = running->uid; // owner uid
	 ip->i_gid = pmip->INODE.i_gid; // group Id
	 ip->i_size = BLKSIZE; // size in bytes
	 ip->i_links_count = 2; // links count=2 because of . and ..
	 ip->i_atime = ip->i_ctime = ip->i_mtime = time(0L); //set all times to same
	 ip->i_blocks = 2; // LINUX: Blocks count in 512-byte chunks
	 ip->i_block[0] = blk; // new DIR has one data block//mip->INODE.i_block[0] = blk;	 	
	 for(int j = 1; j<15; j++) 
	 	ip->i_block[j] = 0;  //other i_block[ ] = 0;
	
	 mip->dirty = 1; // mark minode dirty
	 mip->dev = dev;// mip->ino = ino;
	 iput(mip); // write INODE to disk
	 	
	//(4).3. 
	//make data block 0 of INODE to contain . and .. entries;
	
	dp = (DIR *)buf; //convert new&empty buf to dir entry
	cp = buf;
	// make . entry
	dp->inode = ino;
	dp->rec_len = 12;
	dp->name_len = 1;
	dp->name[0] = '.';
	
	cp += dp->rec_len;
	dp = (DIR *)cp;
    //make .. entry: pino=parent DIR ino, blk=allocated block
	dp->inode = pmip->ino;//pino;//pmip->ino???
	dp->rec_len = BLKSIZE-12; // rec_len spans block
	dp->name_len = 2;
	dp->name[0] = dp->name[1] = '.';
	
	put_block(dev, blk, buf); //write to blk on disk
	
	//(4).4. //which enters (ino, child) 
 	//as a dir_entry to the parent INODE;
	enter_child(pmip, ino, child); 
}
int mkdir(char *pathname)
{
	char parent[128], child[128], path[BLKSIZE], path2[BLKSIZE], temp[BLKSIZE];
	MINODE *pmip; int pino;
	strcpy(path, pathname);	strcpy(path2, pathname); strcpy(temp, pathname);
	printf("path=%s\n", path);	printf("path2=%s\n", path2);
	
	strcpy(child, basename(path));//child = basename(temp);
	printf("child=%s\n",child);	
	strcpy(parent, dirname(path2));//parent = dirname(temp1);
	printf("parent=%s\n",parent);
	
	pino = getino(parent);// parent must exist and is a DIR:
 	pmip = iget(dev, pino);
 	
	if((pmip->INODE.i_mode & 0xF000) != 0x4000)//check pmip->INODE is a DIR
	{
		printf("Parent(ino=%d)-> is not a dir\n", pino);
		return -1;//break out if bad
	}
	else
		printf("Parent(ino=%d)-> is a dir\n", pino);
	
	if(getino(temp) == 0){//see if dir exists
		printf("item doesn't exist yet\n");
		//#4. 
		my_mkdir(pmip, child);
	
		//$5. increment parent INODE's links_count by 1 and mark pmip dirty;
		pmip->INODE.i_links_count += 1;
		pmip->dirty = 1;
	 	iput(pmip);
	}
	else{
		printf("can't make doubledir's\n");
		return -1;	
	}
}
int my_creat(MINODE *pmip, char *child)
{
	char buf[BLKSIZE]; char *cp;
	DIR *dp;
	//(4).1. Allocate an INODE and a disk block:
 	int ino = ialloc(dev); printf("ino=%d\n",ino);
// 	int blk = balloc(dev); 			
	printf("myCreat: pino=%d, ino=%d\n", pmip->ino, ino);
	//(4).2 		
	 MINODE *mip = iget(dev, ino);  // load INODE into a minode
	 INODE *ip = &mip->INODE; //initialize mip->INODE as a DIR INODE;
	
	 mip->refCount = 0;
	 ip->i_mode = 0644; // octal 0644: FILE type and permissions
	 ip->i_uid = running->uid; // owner uid
	 ip->i_gid = pmip->INODE.i_gid; // group Id
	 ip->i_size = 0;// size in bytes, initially 0 for empty file
	 ip->i_links_count = 1; // links count=1, one occurrence currently
	 ip->i_atime = ip->i_ctime = ip->i_mtime = time(0L); //set all times to same
//	 ip->i_blocks = 2; // LINUX: Blocks count in 512-byte chunks
//	 ip->i_block[0] = blk; // new DIR has one data block//mip->INODE.i_block[0] = blk;	 	
//	 for(int j = 1; j<15; j++) 
//	 	ip->i_block[j] = 0;  //other i_block[ ] = 0;
	
	 mip->dirty = 1; // mark minode dirty
	 mip->dev = dev;// mip->ino = ino;
	 iput(mip); // write INODE to disk
	 	
	//(4).3. 
	//make data block 0 of INODE to contain . and .. entries;
	
//	bzero(buf, BLKSIZE); // optional: clear buf[ ] to 0
//	dp = (DIR *)buf; //convert new&empty buf to dir entry
//	cp = buf;
	
//?	put_block(dev, blk, buf); //write to blk on disk
	
	//(4).4. //which enters (ino, child) 
 	//as a FILE_entry to the parent INODE;
	enter_child(pmip, ino, child); 
}
int docreat(char *pathname)
{
	char parent[128], child[128], path[BLKSIZE], path2[BLKSIZE], temp[BLKSIZE];
	MINODE *pmip; int pino;
	strcpy(path, pathname);	strcpy(path2, pathname); strcpy(temp, pathname);
	printf("path=%s\n", path);	printf("path2=%s\n", path2);
	
	strcpy(child, basename(path));//child = basename(temp);
	printf("child=%s\n",child);	
	strcpy(parent, dirname(path2));//parent = dirname(temp1);
	printf("parent=%s\n",parent);
	
	pino = getino(parent);// parent must exist and is a DIR:
 	pmip = iget(dev, pino);
 	
	if((pmip->INODE.i_mode & 0xF000) != 0x4000)//check pmip->INODE is a DIR
	{
		printf("Parent(ino=%d)-> is not a dir\n", pino);
		return -1;//break out if bad
	}
	else
		printf("Parent(ino=%d)-> is a dir\n", pino);
	
	if(getino(temp) == 0){//see if file exists
		printf("item doesn't exist yet\n");
		//#4. 
		my_creat(pmip, child);///MAKE THIS!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!1
		//$5. increment parent INODE's links_count by 1 and mark pmip dirty;
		
		pmip->dirty = 1;
	 	iput(pmip);
	}
	else{
		printf("item already exists. .\n");
		return -1;	
	}	
}
int verifyEmptyDir(MINODE *mip)
{
	char buf[BLKSIZE];//block buf
	char *cp;
	DIR *dp;//dir struct
	int numChildren = 0;
	get_block(mip->dev, mip->INODE.i_block[0], buf);//get data block of mip->INODE
	dp = (DIR *)buf;
	cp = buf;
	while (cp + dp->rec_len < buf + BLKSIZE){
		cp += dp->rec_len;
		dp = (DIR *)cp;
		numChildren++;//keep track of how much is in dir
	}
 // dp NOW points at last entry in block
	if(numChildren > 2)
		return 0;//false, aka not empty
	else
		return 1;//true, dir is 'empty' (only has . and ..)
}
int rm_child(MINODE *pmip, char *name)
{
	INODE *pip = &pmip->INODE; 
	char *cp, *cp2; 
	DIR *dp;
	int blk, i, ideal_length, name_len, need_length, remain, add_len, last_len;
	char buf[BLKSIZE], temp[256];

	for(i = 0; i<12; i++)
	{
		if(pip->i_block[i] == 0)
			break;
		blk = pip->i_block[i];

		//get pip->inode->i_block[], and read parent block into buf[]		
		get_block(pmip->dev, blk, buf);
		dp = (DIR *)buf;
		cp = buf;

		while (cp + dp->rec_len < buf + BLKSIZE){
			strncpy(temp, dp->name, dp->name_len); // make name a string
			temp[dp->name_len] = 0; // ensure NULL at end
			if(strcmp(name, temp) == 0)	//check for name before printing data
			{
				//printf("%d\t%d\t%d\t %s\n", 
					//dp->inode, dp->rec_len, dp->name_len, temp);
				break;//break out if found item
			}
			last_len = dp->rec_len;
			cp += dp->rec_len;
			dp = (DIR *)cp;
		}
		cp = (char *)dp;
		dp = (DIR *)cp;
		name_len = strlen(name);//calculate lengths
		need_length = 4*( (8 + name_len + 3)/4 );

		if(dp->rec_len == BLKSIZE)//if first&&only entry in data block
		{
			pip->i_size -= BLKSIZE;//reduce parent file size
			bdalloc(pmip->dev, blk);
			pmip->dirty = 1;
			pip->i_block[i] = 0;
	//		bzero(buf, BLKSIZE);
			put_block(pmip->dev, pip->i_block[i], buf);		//makes infinite loop of zero entries being ls'd
			return 1;
		}
		else if(dp->rec_len > need_length){//last entry in block , entry before it absorbs remaining space
			add_len = dp->rec_len;
			cp -= last_len;
			dp = (DIR *)cp;
			dp->rec_len += add_len;
			pmip->dirty = 1;
			put_block(pmip->dev, pip->i_block[i], buf);
			return 1;
		}
		else{//entry is first||middle of populated block
			printf("trying to rm middle dir\n");
			remain = dp->rec_len;//keep, so we can add to last entry
			printf("remain=%d\n",remain);
			cp2 = cp;
			cp2 += remain;//step back, so we can copy cp(going to block end), to old spot
			DIR *dp2 = (DIR *)cp2;
			memcpy(cp, cp2, dp2->rec_len);//move trailing entries left
			cp = (char *)dp;
			dp = (DIR *)cp;
			printf("before loop to end\n");
	//		while (cp + dp->rec_len < buf + BLKSIZE){
	//			cp += dp->rec_len;
	//			dp = (DIR *)cp;
			//	printf("in loop, dp->rec_len=%d\n", dp->rec_len);
	//		}
			printf("after loop to end\n");// dp NOW points at last entry in block
			dp->rec_len += remain;//add remaining length to last item
			cp = (char *)dp;
			put_block(pmip->dev, pip->i_block[i], buf);
		}
	}	
}
int dormdir(char *pathname)
{
	char parent[128], name[64], path[BLKSIZE];//, path1[BLKSIZE], path2[BLKSIZE];
	MINODE *pmip, *mip; 
	int pino, ino;
	strcpy(path, pathname);//	strcpy(path1, pathname); strcpy(path2, pathname);
	printf("path=%s\n", path);//	printf("path2=%s\n", path2);
//	strcpy(child, basename(path));//child = basename(temp);
//	printf("child=%s\n",child);	
	strcpy(parent, dirname(path));//parent = dirname(temp1);
	printf("parent=%s\n",parent);
	//(1).
	ino = getino(pathname);//get inode of pathname from memory
	mip = iget(dev, ino);
	//(2).
	if((mip->INODE.i_mode & 0xF000) != 0x4000)//check mip->INODE is a DIR
	{// this check bitmasks the actual mode, 0x41ED, for easier check (i think)
		printf("Item (ino=%d)-> is not a dir\n", ino);
		return -1;//break out if bad
	}
	else
		printf("Item (ino=%d)-> is a dir\n", ino);
	if(mip->refCount > 1){
		printf("refcount=%d\n",mip->refCount);
		printf("Can't remove busy directory. . .\n");
		return -1;
	}
	else
		printf("Directory is not busy\n");
	if(verifyEmptyDir(mip) == 0){
		printf("Can't rmdir that isn't empty. .\n");
		return -1;
	}
	else
		printf("Dir is empty, lets remove\n");
	//(3).
	pino = getino(parent);//get pino from .. entry in INODE.i_block[0]
	pmip = iget(mip->dev, pino);
	printf("before findName\n");
	findName(pmip, ino, name);//use ino# to find name in parent DIR
	printf("after findName, name=%s\n", name);
	//(5). remove name from parent directory
	rm_child(pmip, name);
	//(6). deallocate its data blocks and inode
	bdalloc(mip->dev, mip->INODE.i_block[0]);
	idalloc(mip->dev, mip->ino);
	iput(mip);
	//(7).  
	pmip->INODE.i_links_count--;//dec parent links_count by 1
	pmip->dirty = 1;//mark parent pimp dirty; //direct quote from 'rmdir.pdf'
	iput(pmip);
}










