/*Preliminary Implementations - Not yet optimized
--copy paste these into fileFunctions.c*/

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
	
	while(nbytes){//while we still need to read
		lbk = tof->offset / BLKSIZE;	//compute logical block
		start = tof->offset % BLKSIZE;	//start byte in block
		
		blk = mapBlk(ip,lbk,fd);//convert logical to physical block number
		
		get_block(dev, blk, kbuf);//use running->dev?
		char *cp = kbuf + start;
		remain = BLKSIZE - start;
		while(remain){
			//(remain) ? put_ubyte(*cp++, *buf++) : 
			*buf++ = *cp++;
			tof->offset++; count++;             //inc offset, count
			remain--; avail--; nbytes--;   //dec remain, avail, nbytes
			if(nbytes == 0 || avail == 0)
				break;
		}
	}
	return count;
}

int kread(int fd, char buf[], int nbytes, int space){ //space=K|U
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
	char kbuf[BLKSIZE];
	
	OFT *tof = running->fd[fd];//easier access
	MINODE *mip = tof->mptr;
	INODE *ip = &mip->INODE;
	int blk, lbk, start, remain;
	int count = 0; //#bytes read
	
	while(nbytes){//while we still need to read
		lbk = tof->offset / BLKSIZE;	//compute logical block
		start = tof->offset % BLKSIZE;	//start byte in block
		
		blk = mapBlk(ip,lbk,fd);//convert logical to physical block number
		
		get_block(dev, blk, kbuf);//use running->dev?
		char *cp = kbuf + start;
		remain = BLKSIZE - start;
		while(remain){
			*cp++ = *buf++;
			//put_ubyte(*cp++, *buf++);
			tof->offset++; count++; //inc offset, count
			remain--; nbytes--;     //dec remain, nbytes
			
			if(tof->offset > ip->i_size)
				ip->i_size++;
			if(nbytes <= 0)
				break;
		}
		put_block(dev, blk, kbuf);
	}
	mip->dirty = 1; //dirty minode - we just wrote all over it
	return count;
}

int kwrite(int fd, char *ubuf, int nbytes){
	OFT *tof = running->fd[fd];
	
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
