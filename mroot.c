/** mroot.c -- mount_root() file **/
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ext2fs/ext2_fs.h>
#include <string.h>
#include <libgen.h>
//#include <sys/stat.h>
#include "type.h"
//#include "util.c"
char line[128], cmd[64], pathname[64], pathname2[64]; char str[20];

MINODE minode[NMINODE];
MINODE *root;
PROC   proc[NPROC], *running;
struct mntTable mtable[4]; 

SUPER *sp;
GD    *gp;
INODE *ip;

char *t1 = "xwrxwrxwr-------";
char *t2 = "----------------";
char *names[32];
char *device;
char buffer[128];
//char *cmdln[32];
int n;		 // number of names
int dev;
int nblocks; // from superblock
int ninodes; // from superblock
int bmap;    // bmap block 
int imap;    // imap block 
int iblock;  // inodes begin block

void clearNames()
{
	for(int i = 0; i<32; i++)
		names[i] = NULL;
}
void printDir(INODE indir)
{
 struct ext2_dir_entry_2 *dp; // dir_entry pointer
 char *cp; // char pointer
 int blk = indir.i_block[0];
 // = a data block (number) of a DIR (e.g. i_block[0]);
 char buf[BLKSIZE], temp[256];
 get_block(dev, blk, buf); // get data block into buf[ ]
 printf("fd = %d, blk = %d\n",dev,blk);

 dp = (struct ext2_dir_entry_2 *)buf; // as dir_entry
 cp = buf;
 printf("*************************************\n");
 while(cp < buf + BLKSIZE)
 {
	 strncpy(temp, dp->name, dp->name_len); // make name a string
	 temp[dp->name_len] = 0; // ensure NULL at end
	 //printf("%d\t%d\t%d\t %s\n", 
		//	dp->inode, dp->rec_len, dp->name_len, temp);
	 ls_file(dp->inode);
	 printf("%s\n",temp);
	 cp += dp->rec_len; // advance cp by rec_len
	 dp = (struct ext2_dir_entry_2 *)cp; // pull dp to next entry
 } 	
}
int ls_dir(char *dirname)
{
	char *tname;
	int ino, n; MINODE *mip = malloc(sizeof(MINODE)); 
	MINODE *tmip = malloc(sizeof(MINODE));
	mip = running->cwd;
	tmip = running->cwd;//keep cwd in temp, restore later
	if(strcmp(dirname, "/") != 0 && dirname[0])
	{
		tname = dirname;
		n = tokenize(tname);
		if(dirname[0] == '/')
			mip = root;
		for(int j = 0; j < n; j++)	
		{		struct ext2_dir_entry_2 *dp; // dir_entry pointer
			 char *cp; // char pointer
			 int blk = mip->INODE.i_block[0];
			 // = a data block (number) of a DIR (e.g. i_block[0]);
			 char buf[BLKSIZE], temp[256];
			 get_block(dev, blk, buf); // get data block into buf[ ]
			 printf("fd = %d, blk = %d\n",dev,blk);

			 dp = (struct ext2_dir_entry_2 *)buf; // as dir_entry
			 cp = buf;
			 printf("*************************************\n");
			 while(cp < buf + BLKSIZE)
			 {
				 strncpy(temp, dp->name, dp->name_len); // make name a string
				 temp[dp->name_len] = 0; // ensure NULL at end
				 //printf("%d\t%d\t%d\t %s\n", 
					//	dp->inode, dp->rec_len, dp->name_len, temp);
				// ls_file(dp->inode);
				 if(strcmp(temp, names[j]) ==0)//basename(pathname)) == 0)
				 {
					 ino = dp->inode;
					 printf("found %s\n",temp);
				 }
				 cp += dp->rec_len; // advance cp by rec_len
				 dp = (struct ext2_dir_entry_2 *)cp; // pull dp to next entry
			 } 	
			//	for(int i = 0; i<n; i++)
			//	{
			//		ino = getino(names[i]);
					mip = iget(dev, ino);
			//	}
				iput(running->cwd);
				running->cwd = mip;
			}
	/*	for(int i = 0; i<n; i++)
		{
			ino = getino(names[i]);
			mip = iget(dev, ino);
		}*/
		//other stuff, need lsfile first
		if(mip->INODE.i_block[0]){
			printf("i_block[%d] = %d\n", 0, mip->INODE.i_block[0]); 	 		 
		}
	}
	printDir(mip->INODE);
	running->cwd = tmip;
}
int ls_file(int ino)
{
	MINODE *mip;
	int r, i;
    char ftime[64]; char linkname[1024];
	
	mip = iget(dev, ino);//mip points at the minode, containing the INODE
//	if(mip->INODE.i_size == 0)
//		return -1;
	if(mip->INODE.i_mode == 0644)//show it's a file. 0x1A4 == 0o0644 == 420dec
    printf("%c",'-');
    if(mip->INODE.i_mode == 040755)//or if dir
    printf("%c",'d');
    if(mip->INODE.i_mode == 0777)//or link
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
	
	//print the time
  strcpy(ftime, ctime(&mip->INODE.i_ctime));//convert time(?)
  ftime[strlen(ftime)-1] = 0;
  printf("%s  ", ftime);

  //print the name
  //printf("%s", basename(fname));//print name in ls_dir?
}
int findName(MINODE *pmip, int ino, char myname[64])
{
	INODE *ip; DIR *dp;
	char buf[BLKSIZE]; char *cp;

    //check for root
	if(ino == root->ino){
		strcpy(myname, "/");
		return 0;
	}
    //check parent
	if(!pmip)
		return 1;
	
	ip = &pmip->INODE;//get actual inode
    //check directory
	if((ip->i_mode & 0xF000) != 0x4000){//or if dir//if(!S_ISDIR(ip->i_mode)){
		printf("%s isn't a directory\n", dp->name);
		return 1;
	}

    //go through direct blocks
	for(int i = 0; i < 12; i++){
        //block not empty
		if(ip->i_block[i])
		{
			get_block(dev, ip->i_block[i], buf);
			dp = (DIR*)buf;
			cp = buf;

			while(cp < buf + BLKSIZE){
				if(dp->inode == ino){
					strncpy(myname, dp->name, dp->name_len);
					myname[dp->name_len] = 0;
					return 0;
				}
				else{
					cp += dp->rec_len;
					dp = (DIR*)cp;
				}
			}
		}
	}
	return 1;
}
int findParent(MINODE *mip, int *ino, int *pip)
{
	INODE *ip; DIR *dp;
	char buf[BLKSIZE]; char *cp;

    //exists?
	if(!mip)
		return 1;

	ip = &mip->INODE;//set ip

    //check if directory
	if((ip->i_mode & 0xF000) != 0x4000){//if(!S_ISDIR(ip->i_mode)){
		printf("%s isn't a directory\n", dp->name);
		return 1;
	}

    //get dirs
	get_block(dev, ip->i_block[0], buf);
	dp = (DIR*)buf;
	cp = buf;

	//at .
	*ino = dp->inode;

    //increment to get ..
	cp += dp->rec_len;
	dp = (DIR*)cp;

	// for ..
	*pip = dp->inode;

	return 0;
}

int rpwd(MINODE *wd)
{
	char my_name[BLKSIZE];
	int ino, pino;
	MINODE *pip;

	if (wd==root || wd->ino == 2) printf("/");
    else
	{
	  findParent(wd, &ino, &pino);//from i_block[0] of wd->INODE: get my_ino, parent_ino		
      pip = iget(dev, pino);//get parent ino
      findName(pip, ino, my_name);//from pip->INODE.i_block[0]: get my_name string as LOCAL
      rpwd(pip);  // recursive call rpwd() with pip
	  printf("/%s", my_name);
	}
}

int pwd(MINODE *wd)
{
	printf("%dnow\n", wd->ino);
	if (wd->ino == 2){//if we have root
	  printf("/\n");//print root dir name, /
	  return 0;//successful execution
	}
	else
	  rpwd(wd);//not root, so go deeper
	printf("\n");//give us a newline, so output not on prompt line
}

int chdir(char *pathname)
{
	int ino, n; MINODE *mip;
	mip = running->cwd;
	if(pathname[0]){
		n = tokenize(pathname);
		if(pathname[0] == '/')
			mip = running->cwd = root;
		for(int j = 0; j < n; j++)	
		{		struct ext2_dir_entry_2 *dp; // dir_entry pointer
			 char *cp; // char pointer
			 int blk = mip->INODE.i_block[0];
			 // = a data block (number) of a DIR (e.g. i_block[0]);
			 char buf[BLKSIZE], temp[256];
			 get_block(dev, blk, buf); // get data block into buf[ ]
			 printf("fd = %d, blk = %d\n",dev,blk);

			 dp = (struct ext2_dir_entry_2 *)buf; // as dir_entry
			 cp = buf;
			 printf("*************************************\n");
			 while(cp < buf + BLKSIZE)
			 {
				 strncpy(temp, dp->name, dp->name_len); // make name a string
				 temp[dp->name_len] = 0; // ensure NULL at end
				 //printf("%d\t%d\t%d\t %s\n", 
					//	dp->inode, dp->rec_len, dp->name_len, temp);
				// ls_file(dp->inode);
				 if(strcmp(temp, names[j]) ==0)//basename(pathname)) == 0)
				 {
					 ino = dp->inode;
					 printf("found %s\n",temp);
				 }
				 cp += dp->rec_len; // advance cp by rec_len
				 dp = (struct ext2_dir_entry_2 *)cp; // pull dp to next entry
			 } 	
			//	for(int i = 0; i<n; i++)
			//	{
			//		ino = getino(names[i]);
					mip = iget(dev, ino);
			//	}
				iput(running->cwd);
				running->cwd = mip;
			}
	}
	else{
		iput(running->cwd);
		running->cwd = root;
	}
}
int quit()
{
  for(int i = 0; i < NMINODE; i++)
	{
		if(minode[i].ino != 0)//if dirty
		{
			iput(&minode[i]);
		}
	}
  exit(0);
}
void init()// Initialize data structures of LEVEL-1:
{
	root = malloc(sizeof(MINODE));
	running = malloc(sizeof(PROC));
//	proc[0].next = 0;
	proc[0].pid = 1;
	proc[0].uid = 0;
	proc[0].cwd = 0;
	proc[0].fd[NFD] = 0;
//	proc[1].next = 0;
	proc[1].pid = 2;
	proc[1].uid = 1;
	proc[1].cwd = 0;
	proc[0].fd[NFD] = 0;
	
	for(int i = 0; i < NMINODE; i++)
		minode[i].refCount = 0;	
//	root = 0;
}
void mount_root()  // mount root file system, establish / and CWDs
{
	  char buf[BLKSIZE];

	  dev = open(device, O_RDRW);//open disk for read//open device for RW
	  get_block(dev, 1, buf);//    read SUPER block to verify it's an EXT2 FS
	  sp = (SUPER *)buf; // as a super block structure
      printf("check ext2 FS : ");
      if (sp->s_magic != 0xEF53){
	     printf("NOT an EXT2 FS\n"); exit(2);
      }
      printf("OK\n");	
	  
	  get_block(dev, 2, buf); // get group descriptor
      gp = (GD *)buf;  		//so we can get inode start point
      iblock = gp->bg_inode_table;
 	  printf("inodes begin block=%d\n", iblock); 
	   
      root = iget(dev, 2);    //get root inode 
   printf("root->dev=%d, root->ino=%d\n",root->dev, root->ino);
//    Use mtable[0] to record
//        --------------------------
	  mtable[0].dev = dev;//dev = fd;
	  nblocks = mtable[0].nblock = sp->s_blocks_count;
	  ninodes = mtable[0].ninodes = sp->s_inodes_count;//ninodes, nblocks from superblock
	  bmap = mtable[0].bmap = gp->bg_block_bitmap;
	  imap = mtable[0].imap = gp->bg_inode_bitmap;
	  iblock = mtable[0].iblock = gp->bg_inode_table;//bmap, imap, iblock from GD
	  mtable[0].mountDirPtr = root;//mount point DIR pointer = root;
	   printf("before string crap\n");
	  //strcpy(mtable[0].deviceName, device);//device name = "YOUR DISK name"	   
	   //printf("between string crap\n");
	  strcpy(mtable[0].mountedDirName, "/");//mntPointDirName = "/";
	
// ------------------------------------------------------------------------------
	   printf("before proc igets\n");
//    Let cwd of both P0 and P1 point at the root minode (refCount=3)
	printf("\ngetting P0\n");
	  proc[0].cwd = iget(dev, 2);//    P0.cwd = iget(dev, 2); 
	printf("\ngetting P1\n");
	  proc[1].cwd = iget(dev, 2);//    P1.cwd = iget(dev, 2);

//    Let running -> P0.
//	printf("try running\n");
	//   running->cwd = proc[0].cwd;//done in main
	running = &proc[0];//set running to P0
printf("end of mount_root\n");
}

int main(int argc, char *argv[])
{	
device = argv[1];
printf("device=%s\n",device);

init();
mount_root();
//running = &proc[0];//set running to P0
printf("\n");

//int exit = 0;

while(1){
//processing loop//
	printf("input command : [ls|cd|pwd|mkdir|rmdir|symlink|readlink|chmod|utime|quit] ");
	bzero(pathname,63);
	clearNames();
	fgets(line,128,stdin);     //get input
	printf("line=%s",line);
	//printf("\ncmd=%s\n", cmd);
	line[strlen(line)-1] = 0;  //get rid of '\n'
	sscanf(line, "%s %s %s", cmd, pathname, pathname2);//put into separate strings

	if(strcmp(cmd, "ls") == 0)
		ls_dir(pathname);
		//printf("ls %s\n",pathname);
	else if(strcmp(cmd, "cd") == 0)
		chdir(pathname);
		//printf("cd %s\n",pathname);
	else if(strcmp(cmd, "pwd") == 0){
		printf("cwd=");
		pwd(running->cwd);
		//printf("pwd %s\n",pathname);
	}
	else if(strcmp(cmd, "mkdir") == 0)
		mkdir(pathname);
		//printf("mkdir %s\n",pathname);
	else if(strcmp(cmd, "creat") == 0)
		docreat(pathname);//'creat' name taken by fcntl.h
	else if(strcmp(cmd, "rmdir") == 0)
		dormdir(pathname);//'rmdir' name (probably) taken by fcntl.h
	else if(strcmp(cmd, "symlink") == 0){
		printf("try cmd=%s old=%s, new=%s\n", cmd, pathname, pathname2);
		doSymlink(pathname, pathname2);
	}
	else if(strcmp(cmd, "readlink") == 0){
		int sz;
		sz = doReadlink(pathname, buffer);
		printf("buffer=%s, sz=%d\n", buffer, sz);
	}
	else if(strcmp(cmd, "chmod") == 0){
		char *eptr;
		int octNum = strtol(pathname, &eptr, 8);
		//convert input str to real num(octal)-credit:techonthenet.com
		doChmod(octNum, pathname2);
	}
	else if(strcmp(cmd, "utime") == 0){
		doUtime(pathname);
	}
	else if(strcmp(cmd, "quit") == 0){
		printf("quitting. . .\n");
		//exit = 1;
		exit(0);
	}
	else{
		printf("Command not supported. . .\n");
	}

}
return 0;	
}





















