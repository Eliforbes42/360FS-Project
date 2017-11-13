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

int bdalloc(int dev, int bno)
{
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
		
    if((mip->INODE.i_mode & 0xF000) == 0x4000) //or if dir
		printf("%c",'d');
		
    if((mip->INODE.i_mode & 0xF000) == 0xA000) //or link
    	printf("%c",'l');
  
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
	
	ip->i_mode = 0x8000; //octal 0644: FILE type and permissions
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

    oino = getino(old_file);
    omip = iget(dev, oino);

    if((omip->INODE.i_mode & 0xF000) == 0x4000) //check omip->inode file type(must not be DIR)
    {
        printf("this is a dir, you cant do that!\n");
        return;
    }

    strcpy(temp_new_file, new_file);

    parent = dirname(new_file);
    child = basename(temp_new_file);

    pino = getino(parent);
    pmip = iget(dev, pino);
    
    if((pmip->INODE.i_mode & 0xF000) != 0x4000){ //its a dir
        printf("this is a file, you cant do that!\n");
        return;
    }

    if(search(pmip, child) != 0)//maybe will work, not sure yet
        return;

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
    char *parent;
    char *child;
    char *temp_file_name = malloc(sizeof(char) * strlen(file_name));
    char *temp_file_name2 = malloc(sizeof(char) * strlen(file_name));
    MINODE *mip;
    MINODE *pmip;

    strcpy(temp_file_name, file_name);
    strcpy(temp_file_name2, file_name);

    ino = getino(file_name);
    mip = iget(dev, ino);

    if((mip->INODE.i_mode & 0xF000) == 0x4000){//check mip->inode file type(must not be DIR)
        printf("this is a dir, you cant do that!\n");
        return;
    }
    
    parent = dirname(temp_file_name2);
    child = basename(temp_file_name);

    pino = getino(parent);
    pmip = iget(dev,pino);

    rm_child(pmip,child);

    pmip->dirty = 1;
    
    iput(pmip);

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
	enter_link(mip, omip->ino, compositeName);//oldChild);   //change back after naming sorted out!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
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
