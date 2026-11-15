#include "held_fd.h"
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

int upload_held_open(const char *path, int directory)
{
	long pid;
	unsigned long fd;
	int end=0;
	if (!path || strlen(path)>79 || sscanf(path,"/proc/%ld/fd/%lu%n",&pid,&fd,&end)!=2 ||
	    pid<=1 || fd>INT_MAX || !end || path[end]) return -1;
	return open(path,(directory ? O_RDONLY|O_DIRECTORY : O_WRONLY)|O_CLOEXEC);
}
