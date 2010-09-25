/* $Id$
 * jfifextract  acb  25/12/2003
 * program for extracting chunks of JFIF data from a file or block device. 
 * Useful for recovering photos from a dodgy or corrupted memory card.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>


/* PowerShot JPEG files start with FF D8 FF E1, even though the standard 
 * says FF D8 FF E0 */

const char* JFIF_START="\xff\xd8\xff\xe1";

char* whoami;
int dry_run = 0;
int verbosity = 0;

void
usage()
{
	fprintf(stderr, "usage: %s [-d] [-o outdir] infile\n", whoami);
	exit(1);
}

void
creatDirOrDie(const char* dir)
{
	struct stat stbuf;
	if(!stat(dir, &stbuf)) {
		/* dir exists; check if it's what we want */
		if(! S_ISDIR(stbuf.st_mode)) {
			fprintf(stderr, "%s: '%s': not a directory\n");
			exit(2);
		}
	} else {
		/* couldn't stat it; try to create it */
		if(mkdir(dir, 0755)) {
			perror(whoami);
			exit(4);
		}
	}
}

/* return pointer if found, NULL if none */
void *
findNextJFIFHdr(const void* buf, size_t bufsize)
{
	void* nextchr = memchr(buf, JFIF_START[0], bufsize);
	if(!nextchr)  return NULL;
	if((bufsize-(nextchr-buf))<4) return NULL;
	while(strncmp(nextchr, JFIF_START, 4)!=0) {
		nextchr = memchr(nextchr+1, JFIF_START[0], bufsize-(nextchr+1-buf));
		if((!nextchr) || ((bufsize-(nextchr-buf))<4)) return NULL;
	}
	return nextchr;
}

/* do what we need to do with a block we have found; typically, either 
 * write it to a file or just report on its existence */
void
dispatchBlock(const void* block, size_t blksize, int blknum)
{
	if(dry_run) {
		printf("found JFIF data block #%d, with size %d\n", blknum, blksize);
	} else {
		/* if there are more than 8 digits in the image number, we 
		 * could suffer a buffer overflow. In practice, this won't 
		 * happen unless we're either reading an extremely large
		 * storage device (not to mention very large filesystems for
		 * writing recovered data to) or someone's doing something 
		 * malicious; just in case, we refuse to write more than 
		 * 100 million files. */
		if(blknum <= 99999999) {
			char filename[16];
			int fd_out;
			sprintf(filename, "fnd%05d.jpg", blknum);
			if(verbosity>0)
				printf("writing %d bytes to %s\n", blksize,filename);
			if(!(fd_out = creat(filename, 0644))) {
				perror(whoami);
			} else {
				ssize_t written;
				written=write(fd_out, block, blksize);
				if(written<0) {
					perror(whoami);
				} else if(written<blksize) {
					fprintf(stderr, "%s: %s: only %d bytes written\n", whoami, filename, written);
				}
				close(fd_out);
			}
		}
	}
}

void
processBuf(const void* buf, size_t bufsize)
{
	int found_count = 0;
	size_t remain;
	const void* curpos = findNextJFIFHdr(buf, bufsize), *nextpos;
	if(!curpos) return;	/* nothing to see here */
	remain = bufsize-(curpos-buf);

	nextpos = findNextJFIFHdr(curpos+4, remain-4);
	while(nextpos) {
		int blocksize = nextpos-curpos;
		dispatchBlock(curpos, blocksize, found_count++);
		curpos = nextpos;
		remain -= blocksize;
	        nextpos = findNextJFIFHdr(curpos+4, remain-4);
	}
	/* curpos -> end is the last block */
	dispatchBlock(curpos, remain, found_count++);
	
}

void
processFile(const char* infile, const char* outdir)
{
	int fd_in;
	struct stat stbuf;
	void* mmapped;
	/* make sure outdir exists */
	creatDirOrDie(outdir);

	if((fd_in = open(infile, O_RDONLY))==-1) {
		perror(whoami);
		exit(8);
	}
	if(fstat(fd_in, &stbuf)==-1) {
		perror(whoami);
		exit(8);
	}
	if((mmapped = mmap(0, stbuf.st_size, PROT_READ, MAP_SHARED, fd_in, 0))==MAP_FAILED) {
		perror(whoami);
		exit(8);
	}

	if(chdir(outdir)!=0) {
		perror(whoami);
		exit(2);
	}
	/* now handle the block of memory */
	processBuf(mmapped, stbuf.st_size);

	/* tidy up */
	munmap(mmapped, stbuf.st_size);
	close(fd_in);
}

int
main(int argc, char* argv[])
{
	int c;
	char* outdir = "/tmp/jfif.recovered";
	char* infile;

	whoami = argv[0];

	while((c=getopt(argc,argv,"do:v"))!=EOF) {
		switch(c) {
		case 'o':
			outdir = optarg;
			break;
		case 'd': dry_run = 1; break;
		case 'v': verbosity++; break;
		};
	}
	if(optind>=argc) {
		usage();
	}
	infile = argv[optind];
	processFile(infile,outdir);
	return 0;
}

