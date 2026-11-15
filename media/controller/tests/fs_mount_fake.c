/* Tempmedia-only host fixture. Release builds link fs_mount.c instead. */
#include "../fs_mount.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif

static int exists(int fd, const char *name)
{
	struct stat st;
	return !fstatat(fd,name,&st,AT_SYMLINK_NOFOLLOW);
}
int fs_same_mount(int rootfd, int fd)
{
	struct stat root, child;
	return !fstat(rootfd,&root) && !fstat(fd,&child) && root.st_dev==child.st_dev &&
		!exists(rootfd,".test-cross-mount");
}
int fs_mount_check(int rootfd, const char *configured_root, struct fs_mount_info *info)
{
	memset(info,0,sizeof(*info));
	snprintf(info->reason,sizeof(info->reason),"fixture mount unavailable");
	struct stat held, fresh;
	if (fstat(rootfd,&held) || stat(configured_root,&fresh) ||
	    held.st_dev!=fresh.st_dev || held.st_ino!=fresh.st_ino ||
	    !exists(rootfd,".test-mounted") || exists(rootfd,".test-unmounted")) return -1;
	info->mounted=1;
	info->device=held.st_dev;
	info->mount_id=1;
	struct statvfs capacity;
	if (fstatvfs(rootfd,&capacity)) return -1;
	info->total=(uint64_t)capacity.f_blocks*capacity.f_frsize;
	info->free=(uint64_t)capacity.f_bavail*capacity.f_frsize;
	int limit=openat(rootfd,".test-free",O_RDONLY|O_NOFOLLOW);
	if (limit>=0) {
		char text[64]={0};
		ssize_t n=read(limit,text,sizeof(text)-1);
		close(limit);
		if (n>0) info->free=strtoull(text,NULL,10);
	}
	if (exists(rootfd,".test-readonly")) return -1;
	info->writable=1; info->reason[0]=0;
	return 0;
}
int fs_rename_noreplace(int source_dir, const char *source, int target_dir, const char *target)
{
#ifdef __APPLE__
	return renameatx_np(source_dir,source,target_dir,target,RENAME_EXCL);
#elif defined(__linux__)
	return (int)syscall(SYS_renameat2,source_dir,source,target_dir,target,1);
#else
	(void)source_dir; (void)source; (void)target_dir; (void)target;
	errno=ENOTSUP; return -1;
#endif
}
