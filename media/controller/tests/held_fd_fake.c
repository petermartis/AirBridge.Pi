/* Portably duplicate inherited test FDs; never linked into the Linux release. */
#include "../held_fd.h"
#include <limits.h>
#include <stdio.h>
#include <unistd.h>
int upload_held_open(const char *path, int directory)
{
	long pid;
	unsigned long fd;
	int end=0;
	(void)directory;
	if (!path || sscanf(path,"/proc/%ld/fd/%lu%n",&pid,&fd,&end)!=2 ||
	    pid!=(long)getpid() || fd>INT_MAX || path[end]) return -1;
	return dup((int)fd);
}
