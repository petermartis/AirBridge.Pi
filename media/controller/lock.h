#ifndef AIRBRIDGE_MEDIA_LOCK_H
#define AIRBRIDGE_MEDIA_LOCK_H
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

/* Deliberately not CLOEXEC: player retains this singleton lease across exec. */
static int media_lock(const char *path)
{
	struct stat st;
	int fd=open(path,O_WRONLY|O_CREAT|O_NOFOLLOW|O_NONBLOCK,0600);
	if (fd<0) return -1;
	if (fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_nlink != 1 ||
	    st.st_uid != geteuid() || fchmod(fd,0600) || flock(fd,LOCK_EX|LOCK_NB)) {
		close(fd);
		return -1;
	}
	return fd;
}
#endif
