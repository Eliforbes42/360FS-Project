/*************** type.h file ******************/
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

typedef struct ext2_super_block SUPER;
typedef struct ext2_group_desc  GD;
typedef struct ext2_inode       INODE;
typedef struct ext2_dir_entry_2 DIR;

#define BLKSIZE     1024
#define O_RDRW        02
#define NMINODE      100
#define NFD           16
#define NPROC          2

#define COLOR_RESET  "\x1B[0m"
#define COLOR_RED  "\x1B[31m"
#define COLOR_GREEN  "\x1B[32m"
#define COLOR_YELLOW  "\x1B[33m"
#define COLOR_BLUE  "\x1B[34m"
#define COLOR_MAGENTA  "\x1B[35m"
#define COLOR_CYAN  "\x1B[36m"
#define COLOR_WHITE  "\x1B[37m"

typedef struct minode{
  INODE INODE;
  int dev, ino;
  int refCount;
  int dirty;
  int mounted;
  struct mntTable *mptr;
}MINODE;

typedef struct oft{
  int  mode;
  int  refCount;
  MINODE *mptr;
  int  offset;
}OFT;

typedef struct proc{
  struct proc *next;
  int          pid;
  int          uid;
  MINODE      *cwd;
  OFT         *fd[NFD];
}PROC;

struct mntTable{
  int dev;         // dev number: 0=FREE
  int nblock;      // s_blocks_count
  int ninodes;     // s_inodes_count
  int bmap;        // bmap block#
  int imap;        // imap block# 
  int iblock;      // inodes start block#
  MINODE *mountDirPtr;
  char deviceName[64];
  char mountedDirName[64];
}MTABLE;

//global variables
extern MINODE minode[NMINODE];
extern MINODE *root;
extern PROC proc[NPROC], *running;
extern struct mntTable mtable[4]; 

extern SUPER *sp;
extern GD    *gp;
extern INODE *ip;
extern DIR *dp;

extern char line[128], cmd[64], pathname[64], pathname2[64], str[20];
extern char gline[25], *name[16]; //tokenized component string strings
extern char *t1;
extern char *t2;
extern char *names[32];
extern char *device;
extern char *rootdev; //default root_device
extern char *cp;

extern int n;		 // number of names
extern int dev;
extern int nblocks; // from superblock
extern int ninodes; // from superblock
extern int bmap;    // bmap block 
extern int imap;    // imap block 
extern int iblock;  // inodes begin block
extern int nname;

//function prototypes
int tokenize(char *pathname);
int get_block(int fd, int blk, char buf[]);
int put_block(int dev, int blk, char buf[]);
MINODE *iget(int dev, int ino);
int iput(MINODE *mip);
int search(MINODE *mip, char *name);
int getino(char *pathname);
int mkdir(char *pathname);
//================= end of type.h ===================