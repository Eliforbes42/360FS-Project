/* Collection of linux functions for an ext2 file system implementation */

//includes
#include <ext2fs/ext2_fs.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <sys/stat.h>

#include "type.h"
//#include "util.c"


//global variables
int ninodes, nblocks, bmap, imap, iblock; //from superblock & GD
int nname; //number of component strings
int dev;

char gline[25], *name[16]; //tokenized component string strings
char *names[32];
char *t1;
char *t2;
char *rootdev;

MINODE minode[NMINODE], *root;
PROC proc[NPROC], *running;
struct mntTable mtable[4];


//helper functions
//=======================================================================

//bit manipulation functions
int tst_bit(char *buf, int bit){ 
	return buf[bit/8] & (1 << (bit % 8));
}

int set_bit(char *buf, int bit){ 
	buf[bit/8] |= (1 << (bit % 8));
}

int clr_bit(char *buf, int bit){ 
	buf[bit/8] &= ~(1 << (bit % 8)); 
}

//block and inode manipulation functions
int decFreeInodes(int dev){
	SUPER *sp;
	GD *gp;
    char buf[BLKSIZE];
    
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

	get_block(dev, 1, buf); //get super
	sp = (SUPER *)buf;
    sp->s_free_blocks_count--;
    
	put_block(dev, 1, buf); //write back dirty super
    get_block(dev, 2, buf); //get group desc
    
	gp = (GD *)buf;
    gp->bg_free_blocks_count--;
    
	put_block(dev, 2, buf); //write back dirty group desc
}

int incFreeBlocks(int dev){
	SUPER *sp;
	GD *gp;
	char buf[BLKSIZE];

	get_block(dev, 1, buf); //get super
	sp = (SUPER *)buf;
    sp->s_free_blocks_count++;
    
	put_block(dev, 1, buf); //write back dirty super
    get_block(dev, 2, buf); //get group desc
    
	gp = (GD *)buf;
    gp->bg_free_blocks_count++;
    
	put_block(dev, 2, buf); //write back dirty group desc
}

//block and inode allocation/deallocation functions
int ialloc(int dev){
	int i;
	char buf[BLKSIZE];
    SUPER *sp;
    
    dev = open(rootdev, O_RDRW); //open disk for read//open device for RW

	get_block(dev, 1, buf); //read SUPER block to verify it's an EXT2 FS
	sp = (SUPER *)buf; //as a super block structure
	ninodes = sp->s_inodes_count;
	
	//assume imap, bmap are globals from superblock and GD
	get_block(dev, imap, buf);
	for (i=0; i<ninodes; i++){
		if (tst_bit(buf, i)==0){
			set_bit(buf, i);
			put_block(dev, imap, buf);
			decFreeInodes(dev);	//update free inode count in SUPER and GD
			return (i+1);
		}
	}
	return 0; //out of FREE inodes
}

int idalloc(int dev, int ino){
	int i;
	char buf[BLKSIZE];
    SUPER *sp;
    
    dev = open(rootdev, O_RDRW); //open disk for read//open device for RW
    
	get_block(dev, 1, buf); //read SUPER block to verify it's an EXT2 FS
	sp = (SUPER *)buf; //as a super block structure
    ninodes = sp->s_inodes_count;
    
	if (ino > ninodes){ //niodes global
	    printf("inumber %d out of range\n", ino);
	    return;
	}

	get_block(dev, imap, buf); //get inode bitmap block
	clr_bit(buf, ino-1);
	put_block(dev, imap, buf); //write buf back
	incFreeInodes(dev); //update free inode count in SUPER and GD
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
			decFreeInodes(dev); //update free block count in SUPER and GD
			put_block(dev, bmap, buf);
			return i;
		}
	}
	return 0; // out of FREE blocks
}

int bdalloc(int dev, int bno){
    char buf[BLKSIZE];
    
    get_block(dev, bmap, buf); //get block bitmap block
    clr_bit(buf, bno);
    put_block(dev, bmap, buf); //put updated block back
    
	incFreeBlocks(dev); //inc free block # in Super && GD blocks
}

//========== Level 1 functions ==========


//level 1 misc functions 
int verifyEmptyDir(MINODE *mip) //checks a dir to make sure its empty
{
	char buf[BLKSIZE];//block buf
	char *cp;
	DIR *dp;//dir struct
    int numChildren = 0;
    if(mip->INODE.i_links_count > 2)
		return 0;//definitely not empty if >2 links
    get_block(mip->dev, mip->INODE.i_block[0], buf); //get data block of mip->INODE
    
    dp = (DIR *)buf;
    cp = buf;
    
    while (cp + dp->rec_len < buf + BLKSIZE){
		cp += dp->rec_len;
		dp = (DIR *)cp;
		numChildren++; //keep track of how much is in dir
	}
     
    //dp NOW points at last entry in block
    
    if(numChildren > 2)
		return 0; //false, aka not empty
	else
		return 1; //true, dir is 'empty' (only has . and ..)
}

int enter_child(MINODE *pmip, int ino, char *child) //enters (ino, child) as a dir_entry to the parent inode
{
	INODE *pip = &pmip->INODE; 
	int ideal_length, name_len, need_length, remain, blk, i;
    char *cp; DIR *dp;
    char buf[BLKSIZE];
	
	printf("child inside enter_child = %s\n", child);

	for(i = 0; i < 12; i++)
	{
		if(pip->i_block[i] == 0)
			break;

		blk = pip->i_block[i];

		get_block(pmip->dev, blk, buf); //get pip->inode->i_block[], and read parent block into buf[]
        
        dp = (DIR *)buf;
		cp = buf;

		name_len = strlen(child); //calculate lengths

		need_length = 4 * ((8 + name_len + 3) / 4);

		while (cp + dp->rec_len < buf + BLKSIZE){
			cp += dp->rec_len;
			dp = (DIR *)cp;
        }
        
        cp = (char *)dp; //dp NOW points at last entry in block
        
		ideal_length = 4 * ((8 + dp->name_len + 3) / 4);
		
		remain = dp->rec_len - ideal_length; //remain = last rec_len - ideal_length

		if (remain >= need_length){
            dp->rec_len = ideal_length;

			cp += dp->rec_len; //step forward for new entry
			dp = (DIR *)cp; //enter the new entry as the LAST entry

			dp->inode = ino;
            dp->rec_len = remain;
            dp->name_len = name_len;
            
			//now set name, since have all info
            strcpy(dp->name, child);
            
			put_block(dev, blk , buf); //write data block to disk

			return 1;
		}					
    }
    
    blk = balloc(dev); //balloc a new block
    
	pip->i_block[i] = blk; //set block to next i_block[], likely i_block[1] for now
	pip->i_size += BLKSIZE; //increment parent size by BLKSIZE
	pmip->dirty = 1; //mark dirty

	get_block(pmip->dev, blk, buf);

	dp = (DIR *)buf;
	cp = buf;

	dp->inode = ino; //.
	dp->rec_len = 1024;
    dp->name_len = name_len; //..
    
	strcpy(dp->name, child); //...initialize all dirEntry data				

    put_block(dev, blk , buf); //write data block to disk
    
	return 1;
}

int enter_symlink(MINODE *pmip, int ino, char *child){
	INODE *pip = &pmip->INODE; 
	char *cp; DIR *dp;
	int name_len, blk, i;
	char buf[BLKSIZE];
	for(i = 0; i<12; i++)
	{
		if(pip->i_block[i] == 0)
			break;
		//if past above statement, now in existing block
		blk = pip->i_block[i];
		
		//get pip->inode->i_block[], and read parent block into buf[]		
		get_block(pmip->dev, blk, buf);
		dp = (DIR *)buf;
		cp = buf;

		name_len = strlen(child);//calculate length
		//and enter child as first entry with rec_len=BLKSIZE-...
		dp->inode = ino;		//.
		dp->rec_len = name_len;
		dp->name_len = name_len;//..
		strcpy(dp->name, child);//...initialize all dirEntry data	

		//write data block to disk
		put_block(dev, blk , buf);	
		return 1;
	}					
}

//ls related functions
int ls_dir(char *dirname)
{
	char *tname;
	char *cp; // char pointer
	char buf[BLKSIZE], temp[256];
	int ino, n, blk;
	MINODE *mip = malloc(sizeof(MINODE)); 
	MINODE *tmip = malloc(sizeof(MINODE));
	struct ext2_dir_entry_2 *dp; // dir_entry pointer

	mip = running->cwd;
	tmip = running->cwd; //keep cwd in temp, restore later

	if(strcmp(dirname, "/") != 0 && dirname[0])
	{
		tname = dirname;
		n = tokenize(tname);

		if(dirname[0] == '/')
			mip = root;
		
		for(int j = 0; j < n; j++){		
			blk = mip->INODE.i_block[0]; //= a data block (number) of a DIR (e.g. i_block[0]);

			get_block(dev, blk, buf); //get data block into buf[ ]

			printf("fd = %d, blk = %d\n",dev,blk);

			dp = (struct ext2_dir_entry_2 *)buf; //as dir_entry
			cp = buf;

			printf("*************************************\n");

			while(cp < buf + BLKSIZE){
				strncpy(temp, dp->name, dp->name_len); //make name a string

				temp[dp->name_len] = 0;// ensure NULL at end

				if(strcmp(temp, names[j]) ==0){ //basename(pathname)) == 0)
					ino = dp->inode;

					printf("found %s\n",temp);
				}

				cp += dp->rec_len; // advance cp by rec_len
				dp = (struct ext2_dir_entry_2 *)cp; // pull dp to next entry
			} 	

			mip = iget(dev, ino);

			iput(running->cwd);

			running->cwd = mip;
		}

		//other stuff, need lsfile first
		
		if(mip->INODE.i_block[0]){
			printf("i_block[%d] = %d\n", 0, mip->INODE.i_block[0]); 	 		 
		}
	}

	printDir(mip->INODE);
	
	running->cwd = tmip;
}

void printDir(INODE indir)
{
	struct ext2_dir_entry_2 *dp; //dir_entry pointer
	int blk, i; 
	char buf[BLKSIZE], temp[256];
	char *cp; //char pointer
	char *savename;


	blk = indir.i_block[0]; //= a data block (number) of a DIR (e.g. i_block[0]);

	get_block(dev, blk, buf); //get data block into buf[ ]
	
	printf("fd = %d, blk = %d\n",dev,blk);

 	dp = (struct ext2_dir_entry_2 *)buf; //as dir_entry
	cp = buf;
	 
	printf("*************************************\n");
	 
 	while(cp < buf + BLKSIZE)
	{
		strncpy(temp, dp->name, dp->name_len); //make name a string

		temp[dp->name_len] = 0; //ensure NULL at end

		//printf("%d\t%d\t%d\t %s\n", 
		//dp->inode, dp->rec_len, dp->name_len, temp);

		if(dp->inode == 11){
			savename = malloc(sizeof(char) * strlen(dp->name));
			strcpy(savename, dp->name);
		}

		ls_file(dp->inode);
		
		//printf("%s\n",temp);//return back to this function
		printf("%s\n", temp);
		
		cp += dp->rec_len; //advance cp by rec_len
		dp = (struct ext2_dir_entry_2 *)cp; //pull dp to next entry
	} 	
}

int ls_file(int ino)
{
	MINODE *mip;
	int r, i;
	char ftime[64]; 
	char linkname[1024];
	
	mip = iget(dev, ino); //mip points at the minode, containing the INODE

	if(mip->INODE.i_mode == 0x1A4) //show it's a file. 0x1A4 == 0o0644 == 420dec
		printf("%c",'-');
		
    else if(mip->INODE.i_mode == 040755 || mip->INODE.i_mode == 0755) //or if dir
		printf("%c",'d');
		
    else if(mip->INODE.i_mode == 0777)//or link
    	printf("%c",'l');
	
	else
		printf("%c",'-');//otherwise fill out the line
  
	//loop to print that weird token string
	  
	for(i=8; i >= 0; i--){
    	if(mip->INODE.i_mode & (1 << i))
      		printf("%c", t1[i]);
    	else
      		printf("%c", t2[i]);
  	}
	  
	printf("%4d ", mip->INODE.i_links_count);
  	printf("%4d ", mip->INODE.i_gid);
 	printf("%4d ", mip->INODE.i_uid);
  	printf("%4d ", mip->INODE.i_size);

  	strcpy(ftime, ctime(&mip->INODE.i_ctime)); //print the time
	  
	ftime[strlen(ftime)-1] = 0;
	  
	printf("%s  ", ftime); //print the name
	
	/*
	if(mip->INODE.i_mode == 0644)//show it's a file. 0x1A4 == 0o0644 == 420dec
		printf("%c",'-');
	if(mip->INODE.i_mode == 040755)//or if dir
		printf("%c",'d');
	if(mip->INODE.i_mode == 0777)//or link
		printf("%c",'l');*/

}


//pwd related functions
int pwd(MINODE *wd)
{
	printf("%dnow\n", wd->ino);

	if (wd->ino == 2){ //if we have root
		printf("/\n"); //print root dir name
		
		return 0; //successful execution
	}
	else
		rpwd(wd); //not root, so go deeper

	printf("\n"); //give us a newline, so output not on prompt line
}

int rpwd(MINODE *wd)
{
	char my_name[BLKSIZE];
	int ino, pino;
	MINODE *pip;

	if (wd==root || wd->ino == 2) 
		printf("/");
    else{
		findParent(wd, &ino, &pino); //from i_block[0] of wd->INODE: get my_ino, parent_ino		
    	pip = iget(dev, pino); //get parent ino
    	findName(pip, ino, my_name); //from pip->INODE.i_block[0]: get my_name string as LOCAL
    	rpwd(pip); //recursive call rpwd() with pip
		printf("/%s", my_name);
	}
}


//chdir related functions
int chdir(char *pathname)
{
	int ino, n, blk; 
	char *cp; //char pointer
	char buf[BLKSIZE], temp[256];
	MINODE *mip;

	mip = running->cwd;

	if(pathname[0]){
		n = tokenize(pathname);
		
		if(pathname[0] == '/')
			mip = running->cwd = root;
		
		for(int j = 0; j < n; j++){
			struct ext2_dir_entry_2 *dp; //dir_entry pointer
			
			blk = mip->INODE.i_block[0]; //= a data block (number) of a DIR (e.g. i_block[0]);
			
			get_block(dev, blk, buf); //get data block into buf[ ]
			
			printf("fd = %d, blk = %d\n",dev,blk);

		 	dp = (struct ext2_dir_entry_2 *)buf; //as dir_entry
		 	cp = buf;
			 
			printf("*************************************\n");
			 
			while(cp < buf + BLKSIZE){
				strncpy(temp, dp->name, dp->name_len); //make name a string
				
				temp[dp->name_len] = 0; //ensure NULL at end
				 
				if(strcmp(temp, names[j]) ==0){ //basename(pathname)) == 0)
					ino = dp->inode;
					printf("found %s\n",temp);
			 	}

			 	cp += dp->rec_len; //advance cp by rec_len
			 	dp = (struct ext2_dir_entry_2 *)cp; //pull dp to next entry
			} 	

			mip = iget(dev, ino);
			if((mip->INODE.i_mode & 0xF000) != 0x4000) //check pmip->INODE is a DIR
			{
				printf("(ino=%d)->%s is not a dir\n", ino, pathname);
				return -1;//break out if bad
			}
			else
				printf("(ino=%d)->%s is a dir, so cd now\n", ino, pathname);
			iput(running->cwd);
			
			running->cwd = mip;
		}
	}
	else{
		iput(running->cwd);
		running->cwd = root;
	}
}


//quit related functions
int quit()
{
	for(int i = 0; i < NMINODE; i++){
		if(minode[i].ino != 0) //if dirty
			iput(&minode[i]);
	}
  
	exit(0);
}


//mkdir/rmdir/creat functions
int mkdir(char *pathname) //my_mkdir helper function
{
	char parent[128], child[128], path[BLKSIZE], path2[BLKSIZE], temp[BLKSIZE];
    MINODE *pmip; int pino;
    
    strcpy(path, pathname); strcpy(path2, pathname); strcpy(temp, pathname);

    printf("path=%s\n", path);
    printf("path2=%s\n", path2);
	
	strcpy(child, basename(path)); //child = basename(temp);
	printf("child=%s\n",child);	
    
    strcpy(parent, dirname(path2)); //parent = dirname(temp1);
	printf("parent=%s\n",parent);
	
	pino = getino(parent); //parent must exist and is a DIR:
 	pmip = iget(dev, pino);
 	
	if((pmip->INODE.i_mode & 0xF000) != 0x4000) //check pmip->INODE is a DIR
	{
		printf("Parent(ino=%d)-> is not a dir\n", pino);
		return -1; //break out if bad
	}
	else
		printf("Parent(ino=%d)-> is a dir\n", pino);
	
	if(getino(temp) == 0){ //see if dir exists
		printf("item doesn't exist yet\n");

		//#4. 

		my_mkdir(pmip, child);
	
		
		pmip->INODE.i_links_count += 1; //#5. increment parent INODE's links_count by 1
        pmip->dirty = 1;
        
	 	iput(pmip);
	}
	else{
		printf("can't make doubledir's\n");
		return -1;	
	}
}

int my_mkdir(MINODE *pmip, char *child) //creates a directory
{
	char buf[BLKSIZE];
	char *cp;
    DIR *dp;
    MINODE *mip;
    INODE *ip;
 	int blk, ino;
	
	ino = ialloc(dev); //(4).1. Allocate an INODE and a disk block:
	blk = balloc(dev);

	printf("ino=%d\n",ino); 

    printf("myMkdir: pino=%d, ino=%d, blk=%d\n", pmip->ino, ino, blk);//(4).2 		
    
    mip = iget(dev, ino); //load INODE into a minode
	ip = &mip->INODE; //initialize mip->INODE as a DIR INODE;
	
	mip->refCount = 0;
	ip->i_mode = 0x41ED; //octal 040755: DIR type and permissions
	ip->i_uid = running->uid; //owner uid
	ip->i_gid = pmip->INODE.i_gid; //group Id
	ip->i_size = BLKSIZE; //size in bytes
	ip->i_links_count = 2; //links count=2 because of . and ..
	ip->i_atime = ip->i_ctime = ip->i_mtime = time(0L); //set all times to same
	ip->i_blocks = 2; //LINUX: Blocks count in 512-byte chunks
    ip->i_block[0] = blk; //new DIR has one data block
    	 	
	for(int j = 1; j<15; j++) 
		ip->i_block[j] = 0; //other i_block[ ] = 0;
	
	mip->dirty = 1; //mark minode dirty
	mip->dev = dev; //mip->ino = ino;
	iput(mip); //write INODE to disk
	 	
	// (4).3. 
	
	get_block(dev, blk, buf);//this may cause errors!!!!

	dp = (DIR *)buf; // convert new&empty buf to dir entry
	cp = buf;
	
	//make . entry
    dp->inode = ino; 
	dp->rec_len = 12;
	dp->name_len = 1;
	dp->name[0] = '.';
	
	cp += dp->rec_len;
    dp = (DIR *)cp;

	//make .. entry
	dp->inode = pmip->ino; // pino or pmip->ino???
	dp->rec_len = BLKSIZE-12; // rec_len spans block
	dp->name_len = 2;
	dp->name[0] = dp->name[1] = '.';
	
	put_block(dev, blk, buf); //write to blk on disk
	
	enter_child(pmip, ino, child); //(4).4. which enters (ino, child) as a dir_entry to the parent INODE;
}

int docreat(char *pathname) //creat helper function
{
	char parent[128], child[128], path[BLKSIZE], path2[BLKSIZE], temp[BLKSIZE];
    MINODE *pmip; int pino;
    
    strcpy(path, pathname);	strcpy(path2, pathname); strcpy(temp, pathname);
    
    printf("path=%s\n", path);	
    printf("path2=%s\n", path2);
	
	strcpy(child, basename(path)); //child = basename(temp);
    printf("child=%s\n",child);	
    
	strcpy(parent, dirname(path2)); //parent = dirname(temp1);
	printf("parent=%s\n",parent);
	
	pino = getino(parent); //parent must exist and is a DIR:
 	pmip = iget(dev, pino);
 	
	if((pmip->INODE.i_mode & 0xF000) != 0x4000) //check pmip->INODE is a DIR
	{
		printf("Parent(ino=%d)-> is not a dir\n", pino);
		return -1;//break out if bad
	}
	else
        printf("Parent(ino=%d)-> is a dir\n", pino);
        
	if(getino(temp) == 0){ //see if file exists
        printf("item doesn't exist yet\n");
        
        //#4.
         
		my_creat(pmip, child);
        
		//#5

        pmip->dirty = 1;
        
	 	iput(pmip);
	}
	else{
		printf("item already exists. .\n");
		return -1;	
	}	
}

int my_creat(MINODE *pmip, char *child) //creates a file
{
	char buf[BLKSIZE]; char *cp;
    DIR *dp;
    MINODE *mip;
    INODE *ip;
    int ino;

	ino = ialloc(dev); //(4).1. Allocate an INODE and a disk block:
	
    printf("ino=%d\n",ino);			
    printf("myCreat: pino=%d, ino=%d\n", pmip->ino, ino);
    
    //(4).2
    
	mip = iget(dev, ino);  //load INODE into a minode
	ip = &mip->INODE; //initialize mip->INODE as a DIR INODE;
	
	ip->i_mode = 0x81a4; //octal 0644: FILE type and permissions
	ip->i_uid = running->uid; //owner uid
	ip->i_gid = pmip->INODE.i_gid; //group Id
	ip->i_size = 0; //size in bytes, initially 0 for empty file
	ip->i_links_count = 1; //links count=1, one occurrence currently
	ip->i_atime = ip->i_ctime = ip->i_mtime = time(0L); //set all times to same

	mip->refCount = 0;
	mip->dirty = 1; //mark minode dirty
    mip->dev = dev; //mip->ino = ino;
    
	iput(mip); //write INODE to disk

	enter_child(pmip, ino, child); //(4).4. which enters (ino, child) as a FILE_entry to the parent INODE;
}

int dormdir(char *pathname) //rm_child helper function
{
	char parent[128], name[64], path[BLKSIZE];//, path1[BLKSIZE], path2[BLKSIZE];
	MINODE *pmip, *mip; 
    int pino, ino;
    
	strcpy(path, pathname); //strcpy(path1, pathname); strcpy(path2, pathname);
    printf("path=%s\n", path); //printf("path2=%s\n", path2);
        
    strcpy(parent, dirname(path));
    printf("parent=%s\n",parent);
    
    //(1).
    
	ino = getino(pathname); //get inode of pathname from memory
    mip = iget(dev, ino);
    
    //(2).
    
	if((mip->INODE.i_mode & 0xF000) != 0x4000) //check mip->INODE is a DIR
	{ //this check bitmasks the actual mode, 0x41ED, for easier check (i think)
		printf("Item (ino=%d)-> is not a dir\n", ino);
		return -1; //break out if bad
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
    
	pino = getino(parent); //get pino from .. entry in INODE.i_block[0]
    pmip = iget(mip->dev, pino);
    
    printf("before findName\n");
	findName(pmip, ino, name); //use ino# to find name in parent DIR
    printf("after findName, name=%s\n", name);
    
    //(5). remove name from parent directory
    rm_child(pmip, name);
    
	//(6). deallocate its data blocks and inode
	bdalloc(mip->dev, mip->INODE.i_block[0]);
    idalloc(mip->dev, mip->ino);
    
    iput(mip);
    
    //(7).  
    
	pmip->INODE.i_links_count--; //dec parent links_count by 1
    pmip->dirty = 1; //mark parent pimp dirty; //direct quote from 'rmdir.pdf'
    
	iput(pmip);
}

int rm_child(MINODE *pmip, char *name) //removes a child from a parent directory
{
	INODE *pip = &pmip->INODE; 
	char *cp, *cp2; 
	DIR *dp, *dp2;
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
            strncpy(temp, dp->name, dp->name_len); //make name a string
            
            temp[dp->name_len] = 0; //ensure NULL at end
            
			if(strcmp(name, temp) == 0)	//check for name before printing data
			{
				break;//break out if found item
            }
            
            last_len = dp->rec_len;
            
			cp += dp->rec_len;
			dp = (DIR *)cp;
        }
        
		cp = (char *)dp;
        dp = (DIR *)cp;
        
		name_len = strlen(name); //calculate lengths
		need_length = 4 * ((8 + name_len + 3) / 4);

		if(dp->rec_len == BLKSIZE) //if first&&only entry in data block
		{
            pip->i_size -= BLKSIZE; //reduce parent file size
            
            bdalloc(pmip->dev, blk);
            
            pmip->dirty = 1;
            pip->i_block[i] = 0;
            
			put_block(pmip->dev, pip->i_block[i], buf);	//makes infinite loop of zero entries being ls'd

			return 1;
		}
		else if(dp->rec_len > need_length){ //last entry in block , entry before it absorbs remaining space
            add_len = dp->rec_len;
            
            cp -= last_len;
			dp = (DIR *)cp;
            
            dp->rec_len += add_len;
            pmip->dirty = 1;
            
            put_block(pmip->dev, pip->i_block[i], buf);
            
			return 1;
		}
		else{ //entry is first||middle of populated block
            printf("trying to rm middle dir\n");
            
            remain = dp->rec_len; //keep, so we can add to last entry
            
            printf("remain=%d\n",remain);
            
			cp2 = cp;
			cp2 += remain; //step back, so we can copy cp(going to block end), to old spot
            
            dp2 = (DIR *)cp2;

            memcpy(cp, cp2, dp2->rec_len); //move trailing entries left
            
			cp = (char *)dp;
            dp = (DIR *)cp;

            //dp NOW points at last entry in block
            dp->rec_len += remain;//add remaining length to last item
            cp = (char *)dp;
            
			put_block(pmip->dev, pip->i_block[i], buf);
		}
	}	
}


//link, unlink, syslink, readlink functions 
void link(char * old_file, char * new_file) //creates a hard link for a specified file to another
{
    MINODE *omip;
    MINODE *pmip;
    int oino, pino;
    char *parent;
    char *child;
    char *temp_new_file = malloc(sizeof(char) * strlen(new_file));

	//printf("0: old_file = %s\n", old_file);
    oino = getino(old_file);
    omip = iget(dev, oino);

    if((omip->INODE.i_mode & 0xF000) == 0x4000) //check omip->inode file type(must not be DIR)
    {
        printf("this is a dir, you cant do that!\n");
        return;
    }

    strcpy(temp_new_file, new_file);
	//printf("1: temp_new_file = %s\n", temp_new_file);
	//printf("2: new_file = %s\n", new_file);


    parent = dirname(new_file);
    child = basename(temp_new_file);

	//printf("3: parent = %s\n", parent);
	//printf("4: child = %s\n", child);


    pino = getino(parent);
    pmip = iget(dev, pino);
    
    if((pmip->INODE.i_mode & 0xF000) != 0x4000){ //its a dir
        printf("this is a file, you cant do that!\n");
        return;
    }

    if(search(pmip, child) != 0){//maybe will work, not sure yet
		printf("error 1, returning \n");
	    return;
	}
    enter_child(pmip, oino, child);

    omip->INODE.i_links_count++;
    omip->dirty = 1;

    iput(omip);
    iput(pmip);

    return;
}

void unlink(char *file_name) //removes hard link
{
    int ino;
    int pino;
	char parent1[128], parent2[128], child1[128], child2[128], path1[BLKSIZE], path2[BLKSIZE], path3[BLKSIZE], path4[BLKSIZE], temp1[BLKSIZE],temp2[BLKSIZE];
    char *temp_file_name = malloc(sizeof(char) * strlen(file_name));
    char *temp_file_name2 = malloc(sizeof(char) * strlen(file_name));
    MINODE *mip;
    MINODE *pmip;

	strcpy(temp_file_name, file_name);
   
    strcpy(path1, file_name);
	strcpy(path2, file_name); 
	strcpy(temp1, file_name);

    //printf("path1=%s\n", path1);
    //printf("path2=%s\n", path2);
	
	strcpy(child1, basename(path1)); //child = basename(temp);
	//printf("child=%s\n",child1);	
    
    strcpy(parent1, dirname(path2)); //parent = dirname(temp1);
	//printf("parent=%s\n",parent1);

	if(file_name[0] == '/'){
		//printf("filename = %s\n", file_name);
		ino = getino(file_name);
	}
	else{
		ino = getino(child1);
	}

    mip = iget(dev, ino);

    if((mip->INODE.i_mode & 0xF000) == 0x4000){//check mip->inode file type(must not be DIR)
        printf("this is a dir, you cant do that!\n");
        return;
    }
    
    //parent = dirname(temp_file_name2);
    //child = basename(temp_file_name);

	//printf("3: parent = %s\n", parent1);
	//printf("4: child = %s\n", child1);

    pino = getino(parent1);
    pmip = iget(dev,pino);

    rm_child(pmip,child1);

    pmip->dirty = 1;
    
    iput(pmip);
	//printf("5: after iput\n");

    mip->INODE.i_links_count--;

    if(mip->INODE.i_links_count > 0)
        mip->dirty = 1;
    else
    {
        bdalloc(mip->dev, mip->INODE.i_block[0]);
        idalloc(mip->dev, mip->ino);
    }

    iput(mip);
    
    return;
}

//stat function
void mystat(char * pathName) //gives the stat output of a given file
{
	int ino, i, j, blk;
	MINODE *mip = malloc(sizeof(MINODE));
	MINODE *tmip = malloc(sizeof(MINODE));
	char *tempName = malloc(sizeof(char) * strlen(pathName));
	char *itemName;
	char atime[64], mtime[64], crtime[64]; 

	strcpy(tempName, pathName);
	itemName = dirname(tempName);

	ino = getino(pathName);
	mip = iget(dev, ino);

	j = 0;

	for(i = 0; i < strlen(pathName); i++){
		if(pathName[i] == '/')
			j = 1;
	}

	if(j == 1){
		itemName = dirname(pathName); 
	}
	else
		itemName = pathName;

	printf("	File: %s\n", itemName);
	printf("	Size: %d		Blocks:	%d", mip->INODE.i_size, mip->INODE.i_blocks);
	
	if((mip->INODE.i_mode & 0xF000) == 0x4000)
		printf("		directory\n");
    else if((mip->INODE.i_mode & 0xF000) == 0xA000)
		printf("		symbolic link\n");
	else if((mip->INODE.i_mode & 0xF000) == 0x8000)
		printf("		regular file\n");
	else
		printf("%x \n", mip->INODE.i_mode);
	
	printf("Device: %d		Inode: %d		Links: %d\n", mip->dev, mip->ino, mip->INODE.i_links_count);
	printf("Access:	(");

	for(i = 8; i >= 0; i--){
    	if(mip->INODE.i_mode & (1 << i))
      		printf("%c", t1[i]);
    	else
      		printf("%c", t2[i]);
	}
	
	printf(")	Uid: (%4d)	Gid: (%4d)\n", mip->INODE.i_uid, mip->INODE.i_gid);
	
	strcpy(crtime, ctime(&mip->INODE.i_ctime)); //created time
  	crtime[strlen(crtime)-1] = 0;
	
	strcpy(mtime, ctime(&mip->INODE.i_ctime)); //modified time
  	mtime[strlen(mtime)-1] = 0;
	
	strcpy(atime, ctime(&mip->INODE.i_ctime)); //accessed time
  	atime[strlen(atime)-1] = 0;
	
	printf("Access: %s\n", atime);
	printf("Modify: %s\n", mtime);
	printf("Birth: %s\n\n", crtime);
}

int doReadlink(char *file, char *buffer){
	MINODE *mip, *cmip; int ino, cino, blk;
	char buf[BLKSIZE]; char *cp;  DIR *dp;//for DIR walkthrough
	
	ino = getino(file);
	mip = iget(dev, ino);
	INODE *ip = &mip->INODE;//for convenience
	if(ino == 0){//make sure we got something
		printf("No link to read. . .\n");
		return -1;
	}
	//check INODE is a LNK
	if(mip->INODE.i_mode != 0777)	{
		printf("Item(ino=%d)-> is not a link\n", ino);
		return -1;//break out if not LNK
	}
	else
		printf("Item(ino=%d)-> is a link\n", ino);
	
	for(int i = 0; i<12; i++)//search direct blocks
	{
		if(ip->i_block[i] == 0)
			break;
		blk = ip->i_block[i];
		//get pip->inode->i_block[], and read parent block into buf[]		
		get_block(mip->dev, blk, buf);
		dp = (DIR *)buf;
		cp = buf;

		while (cp + dp->rec_len < buf + BLKSIZE){
			if(strcmp(file, dp->name) == 0){
				strcpy(buffer, dp->name);
				cmip = iget(dev, dp->inode);
				INODE *cip = &cmip->INODE;//get inode for file size
				cip->i_size = strlen(buffer);
				return cip->i_size;//return file size
			}				
			cp += dp->rec_len;
			dp = (DIR *)cp;
		}
	}
}
int my_symlink(MINODE *pmip, MINODE *omip, char *oldChild, char *newChild){
	char buf[BLKSIZE];  char *cp;  DIR *dp;
	char *compositeName = newChild;
	strcat(compositeName, "->");
	strcat(compositeName, oldChild);
	printf("compName=%s\n",compositeName);
	//(4).1. Allocate an INODE:
	int ino = ialloc(dev); printf("ino=%d\n",ino);	
	printf("myCreat: pino=%d, ino=%d\n", pmip->ino, ino);
	
	//(4).2 		
	MINODE *mip = iget(dev, ino);  // load INODE into a minode
	INODE *ip = &mip->INODE; //initialize mip->INODE as a DIR INODE;
	
	mip->refCount = 0;
	ip->i_mode = 0777;//need to change comparison in ls_file, so it prints the 'l'
	ip->i_uid = running->uid; // owner uid
	ip->i_gid = pmip->INODE.i_gid; // group Id
	ip->i_size = strlen(compositeName);// size in bytes == length of file name
	ip->i_links_count = 1; // links count=1, one occurrence currently
	ip->i_atime = ip->i_ctime = ip->i_mtime = time(0L); //set all times to same
	for(int j = 0; j<12; j++)
		if(ip->i_block[j] == 0){
			ip->i_block[j] = balloc(dev);
			break;
		}
	enter_symlink(mip, omip->ino, compositeName);//oldChild);   //change back after naming sorted out!!!!
	mip->dirty = 1; // mark minode dirty
	mip->dev = dev;
	iput(mip); // write INODE to disk

	//(4).4. //which enters (ino, child) 
	//as a LNK_entry to the parent INODE;
	enter_child(pmip, ino, compositeName);
}
int doSymlink(char *oldfile, char *newfile){
	char oldName[128], newName[128], temp[128], temp2[128], temp3[128], parent[128], child[128], newChild[128];
	MINODE *mip, *omip, *pmip; int ino, oino, pino;
	
	strcpy(oldName, oldfile);	
	strcpy(newName, newfile);
	strcpy(temp, newfile);
	strcpy(temp2, oldfile);
	strcpy(temp3, newfile);
	printf("oldFile=%s\n", oldName);	
	printf("newFile=%s\n", newName);
	
	strcpy(parent, dirname(temp));//use to check if newfile's parent dir exists
	strcpy(child, basename(temp2));//use to make new entry in parent dir
	strcpy(newChild, basename(temp3));
	printf("parent=%s\n",parent);
	printf("child=%s\n",child);
	
	pino = getino(parent);// parent must exist and is a DIR:
 	pmip = iget(dev, pino);
	if((pmip->INODE.i_mode & 0xF000) != 0x4000){//check parent inode is a DIR
		printf("Parent(ino=%d)-> is not a dir\n", pino);
		return -1;//break out if not DIR
	}
	else
		printf("Parent(ino=%d)-> is a dir\n", pino);
	
	if(getino(newName) == 0)//see if newfile exists -- it shouldn't
		printf("item doesn't exist yet\n");
	else{
		printf("item already exists, can't make symlink. . .\n");
		return -1;
	}
	//if here, ready to make?	
	oino = getino(oldName);// get ino & mip of oldfile
	if(oino == 0){//check that we got something real
		printf("Nothing to link to. . .\n");
		return -1;
	}
 	omip = iget(dev, oino);
	//#4. 
	my_symlink(pmip, omip, child, newChild);//make entry for oldfile in parent DIR
	//$5.mark pmip dirty;
	pmip->dirty = 1;
	iput(pmip);
}

int doChmod(int nOct, char *filename){
	int ino;
	MINODE *mip;
	char *tempPathname = filename;
	ino = getino(tempPathname);//getino #
	mip = iget(root->dev, ino);//get real minode
	
	//check if dir, then check if empty
	if((mip->INODE.i_mode & 0xF000) == 0x4000){
		if(verifyEmptyDir(mip) == 0){
			printf("Can't chmod on DIR that isn't empty. .\n");
			return -1;
		}
	}
	
	//if here, all good to change
	mip->INODE.i_mode = nOct;//change mode
	//finish up by marking dirty and putting minode back
	mip->dirty = 1;
	iput(mip);	
}

int doUtime(char *filename){
	int ino;
	MINODE *mip;
	char *tempPathname = filename;
	ino = getino(tempPathname);//getino #
	mip = iget(root->dev, ino);//get real minode
	INODE *ip = &mip->INODE;//use a direct 'ip' for convenience
	
	//no checks needed for utime?
	//set all times to same
	ip->i_atime = ip->i_ctime = ip->i_mtime = time(0L); 
	
	//finish up by marking dirty and putting minode back
	mip->dirty = 1;
	iput(mip);	
}

int doOpen(char *file, int flags){
	int ino, curFd;	
	MINODE *mip;   
	OFT *toftp = NULL;
	char *tempPathname = file;

	ino = getino(tempPathname);//getino #
	
	printf("file = %s, flags = %d\n", file, flags);

	//check for invalid file
	if(ino == 0 && O_CREAT){
		docreat(tempPathname);//if so, make file
		ino = getino(tempPathname);//getino #
	}
	mip = iget(root->dev, ino);//get real minode
	INODE *ip = &mip->INODE;//use a direct 'ip' for convenience
		

	
	//check INODE permissions
	if((mip->INODE.i_mode & 0xF000) != 0x8000){
		printf("file type is %x\n", mip->INODE.i_mode);
		printf("Error: Invalid File\n");
		return -1;
	}
	
	//check for bad flags
	if(flags != O_RDONLY && 
	   flags != O_WRONLY && 
	   flags != O_APPEND && 
	   flags != O_RDRW)
	{
		printf("Error: Invalid Flags\n");
		return -1;
	}
	
	//printf("running->fd[0]->mode = %d", running->fd[0]->mode);
	printf("1. here\n");
	//see if file already in use

	for(int i = 0; i < NFD; i++){
		if(running->fd[i] != NULL){
			if(running->fd[i]->mptr == mip){
				printf("Error: File already in use\n");
				return -1;//or return i?
			}
		}
	}

	
	
	//allocate openTable entry
	OFT *oftp = malloc(sizeof(OFT));
	//initialize entries(fill too?)
	oftp->mode = flags;
	//oftp->refCount++;
	oftp->refCount = mip->refCount;
	oftp->mptr = mip;//mptr points to file's minode

	

	//set offset according to flags
	if(flags == O_RDONLY || flags == O_WRONLY || flags == O_RDRW)
		oftp->offset = 0;
	else if(flags == O_APPEND)
		oftp->offset = ip->i_size;
	else
		oftp->offset = 0;  //default case
	
	//search for free fd in running PROC
	for(int i = 0; i < NFD; i++)	{
		if(i == NFD - 1){
			printf("***Out of File Descriptors!***\n");	
		}
		if(running->fd[i] == NULL){
			running->fd[i] = oftp;//fd[i] point to the new entry
			curFd = i;//keep it, just in case			
			break;//found free fd, break out with found index		
		}
	}
	return curFd;//return the file descriptor
}

int doClose(int fd){
	//ensure valid fd
	OFT *tof = running->fd[fd];//tof == temp open file
	if(tof != 0){
		if(--tof->refCount == 0)//if last process using this OFT
			iput(tof->mptr);//release minode
	}
	else{
		printf("Error: Bad file descriptor\n");
		return -1;
	}
	
	//clear fd, since done closing
	running->fd[fd] = 0;//0 == NULL == clear
	return 0;//return success
}

int dolSeek(int fd, int position){
	OFT *tof = running->fd[fd];
	if(tof != 0)//check valid fd
	{
		if(tof->mode == O_RDONLY)
			if(0 <= position && position <= tof->mptr->INODE.i_size)
				tof->offset = position;
	}
}

u32 mapBlk(INODE *ip, int lbk, int fd){
	int blk, dblk;
	u32 ibuf[256], dbuf[256];
	
	if(lbk < 12)//direct blocks
		blk = ip->i_block[lbk];
	else if(12 <= lbk < 12+256){//indirect blocks
		lseek(fd, 12*BLKSIZE, SEEK_SET);
    	read(fd, ibuf, 256);
		blk = ibuf[lbk-12];
	}
	else{
		lseek(fd, 13*BLKSIZE, SEEK_SET);
    	read(fd, dbuf, 256);
		lbk -= (12+256);
		dblk = dbuf[lbk / 256];
		
		lseek(fd, dblk*BLKSIZE, SEEK_SET);
		read(fd, dbuf, 256);//read dblk into dbuf[]
		blk = dbuf[lbk % 256];
	}
	return blk;
}

int doRead_file(int fd, char *buf, int nbytes, int space){
	char kbuf[BLKSIZE];
	
	OFT *tof = running->fd[fd];//easier access
	MINODE *mip = tof->mptr;
	INODE *ip = &mip->INODE;
	int blk, lbk, start, remain;
	int count = 0; //#bytes read
	int avail = tof->mptr->INODE.i_size - tof->offset;//#available bytes
	
	printf("\ninode = %d\n\n", mip->ino);

	//printf("kbuf = %s\n", kbuf);

	bzero(kbuf, BLKSIZE);

	while(nbytes > 0){//while we still need to read
		lbk = tof->offset / BLKSIZE;	//compute logical block
		start = tof->offset % BLKSIZE;	//start byte in block
		
		blk = mapBlk(ip,lbk,fd);//convert logical to physical block number
		

		printf("blk = %d\n", blk);
		get_block(dev, blk, kbuf);//use running->dev?


		char *cpLocal = kbuf + start;
		remain = BLKSIZE - start;

		printf("kbuf = %s\n", kbuf);
		printf("start = %d\n", start);
		printf("lbk = %d\n", lbk);
		printf("blk = %d\n", blk);
		printf("remain = %d\n", remain);

		while(remain){
			//(remain) ? put_ubyte(*cp++, *buf++) : 
			if(nbytes > 4){  //if > 4 bytes left to copy

				memcpy(buf, cpLocal, 4);//copy 4 bytes

				*buf++;
				*buf++;
				*buf++;
				*buf++;
				*cpLocal++;
				*cpLocal++;
				*cpLocal++;
				*cpLocal++;

				tof->offset += 4; 
				count += 4;
				remain -= 4; 
				avail -= 4; 
				nbytes -= 4;
			}
			else{

				memcpy(buf, cpLocal, 1);

				*buf++;
				*cpLocal++;
				tof->offset++; 
				count++;             //inc offset, count
				remain--; 
				avail--; 
				nbytes--;   //dec remain, avail, nbytes
			}
			if(nbytes <= 0 || avail == 0){
				break;
			}
		}
	}
	//printf("\nstring = %s\n",buf);
	return count;
}

int kread(int fd, char buf[], int nbytes, int space){ //space=K|U

	printf("inside kRead, fd = %d, char buf[] = %s, int nbytes = %d\n", fd, buf, nbytes);

	OFT *tof = running->fd[fd];
	
	//check valid fd
	if(tof != 0){
		if(tof->mode == O_RDONLY || tof->mode == O_RDRW)//ensure: open for READ|RW
			return doRead_file(fd, buf, nbytes, space);	//since reg. file
	}
	else{
		printf("Error: Invalid File Descriptor\n");//can't be open since fd not valid
		return -1;
	}
}

int doRead(int fd, char buf[], int nbytes){

	printf("inside doRead, fd = %d, char buf[] = %s, int nbytes = %d\n", fd, buf, nbytes);
	//invokes kread() immediatey
	int space = 0, res = -1; //0 == K ?	//since passed into kread
	if((res = kread(fd, buf, nbytes, space)) == -1){
		printf("Error: Couldn't read file\n");
		return -1;	
	}
	else
		return res;
}

int doWrite_file(int fd, char *buf, int nbytes){
	
	printf("fd = %d, buf = %s, nbytes = %d\n", fd, buf, nbytes);
	char kbuf[BLKSIZE];
	
	OFT *tof = running->fd[fd];//easier access
	MINODE *mip = tof->mptr;
	INODE *ip = &mip->INODE;
	int blk, lbk, start, remain;
	int count = 0; //#bytes read
	int i;
	//fprintf(stderr, "inside doWrite_file\n");
	
	bzero(kbuf, BLKSIZE);


	while(nbytes > 0){//while we still need to read
		lbk = tof->offset / BLKSIZE;	//compute logical block
		start = tof->offset % BLKSIZE;	//start byte in block
		
		blk = mapBlk(ip,lbk,fd);//convert logical to physical block number
		if(blk == 0)
			blk = ip->i_block[lbk] = balloc(mip->dev);
		get_block(dev, blk, kbuf);//use running->dev?


		char *cpLocal = kbuf + start;
		remain = BLKSIZE - start;
		

		//printf("start = %d\n", start);
		//printf("lbk = %d\n", lbk);
		//printf("blk = %d\n", blk);
		//printf("remain = %d\n", remain);

		while(remain){
			//getchar();
			if(nbytes > 4){  //if > 4 bytes left to copy
				//printf("cp = %s\n", cpLocal);
				//printf("buf = %s\n", buf);
				
				memcpy(cpLocal, buf, 4);//copy 4 bytes

				*buf++;
				*buf++;
				*buf++;
				*buf++;
				*cpLocal++;
				*cpLocal++;
				*cpLocal++;
				*cpLocal++;

				
				tof->offset += 4;
				count += 4;
				remain -= 4;
				nbytes -= 4;

			}
			else{
				//printf("cp = %s\n", cpLocal);
				//printf("buf = %s\n", buf);
				//*cpLocal++ = *buf++;//pretty much: put_ubyte(*cp++, *buf++);		
				memcpy(cpLocal, buf, 1);		
				*buf++;
				*cpLocal++;

				
				tof->offset++; 
				count++; //inc offset, count
				remain--; 
				nbytes--;     //dec remain, nbytes
			}
			if(tof->offset > ip->i_size)
				ip->i_size++;

			printf("nbytes = %d\n", nbytes);
			if(nbytes <= 0){
				//printf("this should be working\n");
				break;
			}
		}

		//printf("kbuf = %s\n", kbuf);
		put_block(dev, blk, kbuf);
	}
	mip->dirty = 1; //dirty minode - we just wrote all over it
	return count;
}

int kwrite(int fd, char *ubuf, int nbytes){
	OFT *tof = running->fd[fd];
	
	fprintf(stderr, "inside kWrite\n");

	//check valid fd
	if(tof != 0){
		if(tof->mode == O_WRONLY || tof->mode == O_RDRW)//ensure: open for WRITE|RW
			return doWrite_file(fd, ubuf, nbytes);	//since reg. file
	}
	else{
		printf("Error: Invalid File Descriptor\n");//can't be open since fd not valid
		return -1;
	}
}

int doWrite(int fd, char buf[], int nbytes){
	//invokes kwrite() immediatey

	fprintf(stderr, "inside doWrite\n");
	int res = -1;
	if((res = kwrite(fd, buf, nbytes)) == -1){
		printf("Error: Couldn't read file\n");
		return -1;	
	}
	else
		return res;
}

void myPfd(){

	int i = 0;
	int ino, pino;
	MINODE *mip;
	MINODE *pmip;
	char myName[BLKSIZE];


	//printf("1. in mypfd\n");

	for(i = 0; i < 16; i++){
		if(running->fd[i] != NULL){

			mip = running->fd[i]->mptr;
			fprintf(stderr, "before findparent\n");

			//findParent(mip, mip->ino, pino);
			//fprintf(stderr, "after findparent\n");
			pmip = iget(dev, pino);
			//printf("myname = %s\n", myName);
			//findName(pmip, mip->ino, myName);

			printf("\nfd	mode	count	offset	(dev,ino)	filename\n");
			printf("--	----	-----	------	---------	--------\n");
			printf("%d	", i);

			if(running->fd[i]->mode == 0)
				printf("RD  	");
			else if(running->fd[i]->mode == 1)
				printf("WR  	");
			else if(running->fd[i]->mode == 2)
				printf("RDWR	");
			else if(running->fd[i]->mode == 2)
				printf("AP  	");
			
			printf("%d	%d	(%d, %d)	\n", running->fd[i]->refCount, running->fd[i]->offset, mip->dev, mip->ino);
		}
		

	}
}

void myMove(char * old_file, char * new_file){
	//ex mv /x/y/z /a/b
	char *temp_old_file1 = malloc(sizeof(char) * strlen(old_file));
	char *temp_old_file_backup = malloc(sizeof(char) * strlen(old_file));
	char *temp_old_file2 = malloc(sizeof(char) * strlen(old_file));
	char *temp_new_file;

	strcpy(temp_old_file1, old_file);
	strcpy(temp_old_file_backup, old_file);

	temp_old_file2 = basename(temp_old_file1);
	temp_new_file = malloc(sizeof(char) * (strlen(old_file) + strlen(temp_old_file2)));

	strcpy(temp_new_file, new_file);

	strcat(temp_new_file, "/");
	strcat(temp_new_file, temp_old_file2);

	link(temp_old_file_backup, temp_new_file);
	unlink(old_file);
}

void myCopy(char * old_file, char * new_file){
	
	/*
	1. determine type of file to be copied
		lets not worry about dirs right now, focus on files
	2. create a new file in location
	3. open old file for read
	4. open new file for write
	5. copy over chunk by chunk the information until eof
	6. close both files
	*/
	MINODE *omip;
    MINODE *pmip;
    int oino, pino;
    char *parent;
    char *child;
	char readbuf[128];
	char writebuf[128];
	char *temp_old_file1 = malloc(sizeof(char) * strlen(old_file));
	char *temp_old_file_backup = malloc(sizeof(char) * strlen(old_file));
	char *temp_old_file2 = malloc(sizeof(char) * strlen(old_file));
	char *temp_new_file3 = malloc(sizeof(char) * strlen(old_file));
	char *temp_new_file;
	int fdRead;
	int fdWrite;


	strcpy(temp_old_file1, old_file);
	strcpy(temp_old_file_backup, old_file);

	temp_old_file2 = basename(temp_old_file1);
	temp_new_file = malloc(sizeof(char) * (strlen(old_file) + strlen(temp_old_file2)));

	strcpy(temp_new_file, new_file);

	strcat(temp_new_file, "/");
	strcat(temp_new_file, temp_old_file2);

	//link(temp_old_file_backup, temp_new_file);
	//unlink(old_file);

	//"old_file" = temp_old_file_backup
	//"new_file" = temp_new_file

	oino = getino(temp_old_file_backup);
    omip = iget(dev, oino);

	if((omip->INODE.i_mode & 0xF000) == 0x4000) //check omip->inode file type(must not be DIR)
    {
        printf("this is a dir, you cant do that!\n");
        return;
    }
	
	strcpy(temp_new_file3, temp_new_file);
	//printf("1: temp_new_file = %s\n", temp_new_file);
	//printf("2: new_file = %s\n", new_file);


    parent = dirname(temp_new_file);
    child = basename(temp_new_file3);

	//printf("3: parent = %s\n", parent);
	//printf("4: child = %s\n", child);


    pino = getino(parent);
    pmip = iget(dev, pino);
    
    if((pmip->INODE.i_mode & 0xF000) != 0x4000){ //its a dir
        printf("this is a file, you cant do that!\n");
        return;
    }

    if(search(pmip, child) != 0){//maybe will work, not sure yet
		printf("error 1, returning \n");
	    return;
	}
    enter_child(pmip, oino, child);

    omip->INODE.i_links_count++;
    omip->dirty = 1;

    iput(omip);
    iput(pmip);

	printf("old_file = %s\n", old_file);
	printf("new_file = %s\n", new_file);



	fdRead = doOpen(old_file, 2);
	fdWrite = doOpen(new_file, 1);

	doWrite(fdRead, "hello_world", 11);

	doRead(fdRead, readbuf, 128);
	doWrite(fdWrite, readbuf, 128);

	doClose(fdRead);
	doClose(fdWrite);

    return;

}

void myCat(char * option, char * file_name)
{
	int fileD, inputOffset;
	int fd1, fd2, fd3;
	char writebuf[128];
	char *readbuf = malloc(sizeof(char) * 1024);
	char localinput[1024];
	char nullstring[1024];
	int i;
	char *namecpy = malloc(sizeof(char) * strlen(file_name));
	char *trimmedString;

	if(strcmp(option, ">") == 0)//erases file and writes to it
	{
		fd1 = 0;
		unlink(file_name);


		strcpy(namecpy, file_name);
		//unlink(file_name);

		docreat(namecpy);
		

		printf("file_name = %s\n",file_name);
		printf("option = %s\n", option);
		fd1 = doOpen(file_name, 1);
		
		memset(localinput, NULL, 1024);


		printf("enter in a string:\n");
		fgets(localinput, 1024, stdin);

		localinput[strlen(localinput)-1] = NULL;

		i = 0;

		while(localinput[i] != NULL)
			i++;

		trimmedString = malloc(sizeof(char) * i);


		strncpy(trimmedString, localinput, i);
		
		printf("\n");

	
		printf("writing string :%s, of length %d\n", trimmedString, i);
		doWrite(fd1, trimmedString, i);

		doClose(fd1);

	}
	else if(strcmp(option, ">>") == 0)//append to file
	{
		fd2 = 0;
		printf("file_name = %s\n",file_name);
		printf("option = %s\n", option);
		fd2 = doOpen(file_name, 2);

		inputOffset = doRead(fd2, readbuf, -1);
		printf("inputOffset = %d\n", inputOffset);

		dolSeek(fd2, inputOffset);

		printf("enter in a string:\n");
		fgets(localinput, 1024, stdin);

		printf("\n");

		localinput[strlen(localinput)-1] = 0;

		fd2 = doOpen(file_name, 1);

		doWrite(fd2, localinput, strlen(localinput));

		doClose(fd2);
	}
	else
	{
		fd3 = 0;
		bzero(readbuf, 1024);
		int length;
		printf("file_name = %s\n",file_name);
		printf("option = %s\n", option);
		fd3 = doOpen(option, 0);

		printf("readbuf = %s\n", readbuf);
		length = doRead(fd3, readbuf, 1024);
		printf("readbuf = %s\n", readbuf);
		printf("length = %d\n", length);
		printf("\n%s\n", readbuf);
		doClose(fd3);
	}

}

void myMount()
{



}
void myUnmount()
{



}
