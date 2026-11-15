#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "fs_mount.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <stddef.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>

/* musl in the matching SDK defines an older struct statx without stx_mnt_id;
 * including linux/stat.h too causes a type redefinition. This bounded wire
 * buffer uses the stable Linux statx UAPI offsets, not libc's private pads. */
struct media_statx_wire {
	uint32_t mask;
	unsigned char before_mount_id[140];
	uint64_t mount_id;
	unsigned char reserved[104];
};
_Static_assert(sizeof(struct media_statx_wire)==256,"Linux statx UAPI size");
_Static_assert(offsetof(struct media_statx_wire,mount_id)==144,"Linux statx mount-ID offset");
static int mount_id(int fd, uint64_t *id)
{
	struct media_statx_wire sx;
	memset(&sx,0,sizeof(sx));
	if (syscall(SYS_statx,fd,"",AT_EMPTY_PATH|AT_SYMLINK_NOFOLLOW,0x1000U,&sx) ||
	    !(sx.mask & 0x1000U)) return -1;
	*id=sx.mount_id;
	return 0;
}
#endif
int fs_mount_devices_allowed(uint64_t media, uint64_t root, uint64_t boot, uint64_t temporary, unsigned major_number)
{
	return major_number!=0 && media!=root && media!=boot && media!=temporary;
}
int fs_mount_ids_equal(uint64_t device_a, uint64_t mount_a, uint64_t device_b, uint64_t mount_b)
{
	return device_a==device_b && mount_a==mount_b;
}
static int unescape(char *text)
{
	char *out=text;
	for (char *p=text;*p;++p) {
		if (*p=='\\') {
			if (!p[1] || !p[2] || !p[3] || p[1]<'0' || p[1]>'7' ||
			    p[2]<'0' || p[2]>'7' || p[3]<'0' || p[3]>'7') return -1;
			int v=(p[1]-'0')*64+(p[2]-'0')*8+p[3]-'0';
			if (!v) return -1;
			*out++=(char)v;
			p+=3;
		} else *out++=*p;
	}
	*out=0;
	return 0;
}
int fs_mount_line_matches(const char *line, uint64_t id, unsigned device_major, unsigned device_minor, const char *actual_path)
{
	unsigned long long mid;
	unsigned parent, maj, min;
	char root[4096], path[4096], options[1024], type[64];
	if (!line || strlen(line)>8191 || !strchr(line,'\n') ||
	    sscanf(line,"%llu %u %u:%u %4095s %4095s %1023s",
	           &mid,&parent,&maj,&min,root,path,options)!=7 || mid!=id) return 0;
	const char *separator=strstr(line," - ");
	if (!separator || sscanf(separator+3,"%63s",type)!=1 ||
	    unescape(root) || unescape(path)) return 0;
	return maj==device_major && min==device_minor && !strcmp(root,"/") &&
		!strcmp(path,actual_path) && !strcmp(type,"ext4");
}
#ifdef __linux__
static int actual_mount(int fd, uint64_t id, dev_t device)
{
	char proc[64], actual[4096], line[8192];
	snprintf(proc,sizeof(proc),"/proc/self/fd/%d",fd);
	ssize_t len=readlink(proc,actual,sizeof(actual)-1);
	if (len<0 || (size_t)len>=sizeof(actual)-1) return -1;
	actual[len]=0;
	FILE *f=fopen("/proc/self/mountinfo","re");
	if (!f) return -1;
	size_t bytes=0;
	int found=0;
	while (fgets(line,sizeof(line),f)) {
		bytes+=strlen(line);
		if (bytes>1048576 || !strchr(line,'\n')) break;
		if (fs_mount_line_matches(line,id,major(device),minor(device),actual)) { found=1; break; }
	}
	fclose(f);
	return found ? 0 : -1;
}
#endif

int fs_rename_noreplace(int source_dir, const char *source, int target_dir, const char *target)
{
#ifdef __linux__
	return (int)syscall(SYS_renameat2,source_dir,source,target_dir,target,1);
#else
	(void)source_dir; (void)source; (void)target_dir; (void)target;
	errno=ENOTSUP;
	return -1;
#endif
}
int fs_same_mount(int rootfd, int fd)
{
	struct stat root, child;
	if (fstat(rootfd,&root) || fstat(fd,&child) || root.st_dev!=child.st_dev) return 0;
#ifdef __linux__
	uint64_t a,b;
	return !mount_id(rootfd,&a) && !mount_id(fd,&b) &&
		fs_mount_ids_equal((uint64_t)root.st_dev,a,(uint64_t)child.st_dev,b);
#else
	return 0;
#endif
}
int fs_mount_check(int rootfd, const char *configured_root, struct fs_mount_info *info)
{
	memset(info,0,sizeof(*info));
	snprintf(info->reason,sizeof(info->reason),"a dedicated mounted writable ext4 media filesystem is required");
#ifdef __linux__
	struct stat held, fresh, system;
	struct statfs fs;
	struct statvfs capacity;
	int fd=open(configured_root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
	if (fd<0) return -1;
	int same=!fstat(rootfd,&held) && !fstat(fd,&fresh) &&
		held.st_dev==fresh.st_dev && held.st_ino==fresh.st_ino && fs_same_mount(rootfd,fd);
	close(fd);
	if (!same || stat("/",&system)) return -1;
	uint64_t root_device=(uint64_t)system.st_dev, protected[2]={UINT64_MAX,UINT64_MAX};
	const char *protected_paths[]={"/boot","/tmp"};
	for (size_t i=0;i<sizeof(protected_paths)/sizeof(protected_paths[0]);++i)
		if (!stat(protected_paths[i],&system)) protected[i]=(uint64_t)system.st_dev;
	if (!fs_mount_devices_allowed((uint64_t)held.st_dev,root_device,protected[0],protected[1],major(held.st_dev))) return -1;
	if (fstatfs(rootfd,&fs) || (unsigned long)fs.f_type!=0xef53UL ||
	    mount_id(rootfd,&info->mount_id) || actual_mount(rootfd,info->mount_id,held.st_dev)) return -1;
	info->mounted=1;
	info->device=held.st_dev;
	if (fstatvfs(rootfd,&capacity) || !capacity.f_frsize ||
	    capacity.f_blocks>UINT64_MAX/capacity.f_frsize ||
	    capacity.f_bavail>UINT64_MAX/capacity.f_frsize) return -1;
	info->total=(uint64_t)capacity.f_blocks*capacity.f_frsize;
	info->free=(uint64_t)capacity.f_bavail*capacity.f_frsize;
	if (capacity.f_flag & ST_RDONLY) {
		snprintf(info->reason,sizeof(info->reason),"media filesystem is read-only");
		return -1;
	}
	info->writable=1;
	info->reason[0]=0;
	return 0;
#else
	(void)rootfd; (void)configured_root;
	snprintf(info->reason,sizeof(info->reason),"media writes require Linux mount identity validation");
	return -1;
#endif
}
