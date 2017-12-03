/** mroot.c -- mount_root() file **/
#include <ext2fs/ext2_fs.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <sys/stat.h>

#include "type.h"
//#include "util.c"


MINODE minode[NMINODE];
MINODE *root;
PROC proc[NPROC], *running;
struct mntTable mtable[4]; 

SUPER *sp;
GD    *gp;
INODE *ip;

char line[128], cmd[64], pathname[64], pathname2[64]; char str[20];
char *t1;
char *t2;
char *names[32];
char *device;
char *rootdev;
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
	for(int i = 0; i < 32; i++)
		names[i] = NULL;
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
	
	ip = &pmip->INODE; //get actual inode
	
	
	if((ip->i_mode & 0xF000) != 0x4000){ //check directory
		printf("%s isn't a directory\n", dp->name);
		return 1;
	}

	//go through direct blocks
	
	for(int i = 0; i < 12; i++){
		
		//block not empty
		
		if(ip->i_block[i]){
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

    
	if(!mip) //exists?
		return 1;

	ip = &mip->INODE; //set ip

    
	if((ip->i_mode & 0xF000) != 0x4000){ //check if directory
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

void init() //Initialize data structures of LEVEL-1:
{
	root = malloc(sizeof(MINODE));
	running = malloc(sizeof(PROC));
	
	proc[0].pid = 1;
	proc[0].uid = 0;
	proc[0].cwd = 0;
	for(int i = 0; i < NFD; i++)
		proc[0].fd[i] = 0;
	
	proc[1].pid = 2;
	proc[1].uid = 1;
	proc[1].cwd = 0;
	for(int i = 0; i < NFD; i++)
		proc[1].fd[i] = 0;
	
	for(int i = 0; i < NMINODE; i++)
		minode[i].refCount = 0;	
	
	//root = 0;
}

void mount_root() //mount root file system, establish / and CWDs
{
	char buf[BLKSIZE];

	dev = open(device, O_RDRW); //open disk for read//open device for RW
	
	get_block(dev, 1, buf); //read SUPER block to verify it's an EXT2 FS
	
	sp = (SUPER *)buf; //as a super block structure
	
	printf("check ext2 FS : ");
	  
    if (sp->s_magic != 0xEF53){
		printf("NOT an EXT2 FS\n\n");
		exit(2);
	}
	
    printf("OK\n\n");	
	  
	get_block(dev, 2, buf); //get group descriptor

	gp = (GD *)buf; //so we can get inode start point
	
	iblock = gp->bg_inode_table;
	
 	printf("inodes begin block=%d\n", iblock); 
	   
	root = iget(dev, 2); //get root inode
	 
	printf("root->dev=%d, root->ino=%d\n",root->dev, root->ino);

	//Use mtable[0] to record
	//--------------------------

	mtable[0].dev = dev; //dev = fd;

	nblocks = mtable[0].nblock = sp->s_blocks_count;
	ninodes = mtable[0].ninodes = sp->s_inodes_count; //ninodes, nblocks from superblock

	bmap = mtable[0].bmap = gp->bg_block_bitmap;
	imap = mtable[0].imap = gp->bg_inode_bitmap;

	iblock = mtable[0].iblock = gp->bg_inode_table; //bmap, imap, iblock from GD

	mtable[0].mountDirPtr = root; //mount point DIR pointer = root;

	//printf("before string crap\n");

	//strcpy(mtable[0].deviceName, device); //device name = "YOUR DISK name"	   
	//printf("between string crap\n");

	strcpy(mtable[0].mountedDirName, "/"); //mntPointDirName = "/";

	//------------------------------------------------------------------------------

	//printf("before proc igets\n"); //Let cwd of both P0 and P1 point at the root minode (refCount=3)
	printf("\ngetting P0\n");

	proc[0].cwd = iget(dev, 2); //P0.cwd = iget(dev, 2); 

	printf("getting P1\n\n");

	proc[1].cwd = iget(dev, 2); //P1.cwd = iget(dev, 2);

	//Let running -> P0.
	//printf("try running\n");
	//running->cwd = proc[0].cwd;//done in main
	
	running = &proc[0];//set running to P0
	
	printf("end of mount_root\n");
}

void printMenu()
{
	printf("\nFunction choices (%sred not yet implemented%s)\n", COLOR_RED,COLOR_RESET);
	printf("---------------------------------------------------\n");
	printf("| %sLevel 1%s |                                       |\n", COLOR_BLUE, COLOR_RESET);
	printf("|-------------------------------------------------|\n");
	printf("| ls    | cd    | pwd   | stat | chmod  | utime   |\n");
	printf("| mkdir | creat | rmdir | link | unlink | symlink |\n");
	printf("|                                       | readlink|\n");
	printf("|-------------------------------------------------|\n");
	printf("| %sLevel 2%s |                                       |\n", COLOR_BLUE, COLOR_RESET);
	printf("|-------------------------------------------------|\n");
	printf("| %sopen%s | %sclose%s | %sread%s | %swrite%s | %slseek%s |           |\n", COLOR_RED,COLOR_RESET,COLOR_RED,COLOR_RESET,COLOR_RED,COLOR_RESET,COLOR_RED,COLOR_RESET,COLOR_RED,COLOR_RESET);
	printf("| %scat%s  | %scp%s    | %smv%s   |                           |\n", COLOR_RED,COLOR_RESET,COLOR_RED,COLOR_RESET,COLOR_RED,COLOR_RESET);
	printf("|-------------------------------------------------|\n");
	printf("| %sLevel 3%s |                                       |\n", COLOR_BLUE, COLOR_RESET);
	printf("|-------------------------------------------------|\n");
	printf("| %smount%s | %sunmount%s | %sfilePermissionChecking%s |      |\n", COLOR_RED,COLOR_RESET,COLOR_RED,COLOR_RESET,COLOR_RED,COLOR_RESET);
	printf("---------------------------------------------------\n");
	
}

int main(int argc, char *argv[])
{	
	char *temppath;
	int i;
	t1 = "xwrxwrxwr-------";
	t2 = "----------------";
	rootdev = "vdisk"; 

	device = argv[1];

	printf("\ndevice = %s\n\n",device);

	init();
	mount_root();

	//running = &proc[0];//set running to P0

	printf("\n");

	//int exit = 0;

	//processing loop//
	while(1){

		//printMenu();
		
		printf("\nEnter '%shelp%s' for list of functions\n", COLOR_GREEN, COLOR_RESET);
		printf("%sinput%s: ", COLOR_GREEN, COLOR_RESET);

		bzero(pathname, 63);
		bzero(pathname2, 63);

		clearNames();

		fgets(line,128,stdin); //get input

		printf("\n");
		//printf("line=%s",line);

		line[strlen(line)-1] = 0; //get rid of '\n'
	
		sscanf(line, "%s %s %s", cmd, pathname, pathname2);//put into separate strings	

		if(strcmp(cmd, "ls") == 0)
			ls_dir(pathname);

		else if(strcmp(cmd, "cd") == 0)
			chdir(pathname);

		else if(strcmp(cmd, "pwd") == 0){
			printf("cwd=");
			pwd(running->cwd);
		}

		else if(strcmp(cmd, "mkdir") == 0)
			mkdir(pathname);

		else if(strcmp(cmd, "creat") == 0)
			docreat(pathname); 

		else if(strcmp(cmd, "rmdir") == 0)
			dormdir(pathname); 

		else if(strcmp(cmd, "link") == 0)
			link(pathname, pathname2);

		else if(strcmp(cmd, "unlink") == 0)
			unlink(pathname);

		else if(strcmp(cmd, "stat") == 0)
			mystat(pathname);
		else if(strcmp(cmd, "symlink") == 0){
			printf("try cmd=%s old=%s, new=%s\n", cmd, pathname, pathname2);
			doSymlink(pathname, pathname2);
		}
		else if(strcmp(cmd, "readlink") == 0){
			char buffer[BLKSIZE];
			int sz = doReadlink(pathname, buffer);
			printf("buffer=%s, sz=%d\n", buffer, sz);
		}
		else if(strcmp(cmd, "chmod") == 0){
			char *eptr;
			int octNum = strtol(pathname, &eptr, 8);
			//convert input str to real num(octal)-credit:techonthenet.com
			doChmod(octNum, pathname2);
		}
		else if(strcmp(cmd, "utime") == 0)
			doUtime(pathname);
		
		else if(strcmp(cmd, "quit") == 0){
			printf("quitting. . .\n");
			exit(0);
		}
		else if(strcmp(cmd, "help") == 0){
			printMenu();
		}

		else
			printf("\n%sCommand not supported. . .%s\n\n", COLOR_RED,COLOR_RESET);
	}
	
	return 0;	
}
