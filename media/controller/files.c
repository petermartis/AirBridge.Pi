#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "files.h"
#include "fs_mount.h"
#include "upload.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define STAGE ".airbridge-upload"
#define OWNER_TEXT "AirBridge.Pi private media staging v1\n"
_Static_assert(UPLOAD_LEASE>60,"upload lease must outlive the CGI hard lifetime");
struct upload_slot {
	int used, canceled, fd, parent;
	char token[65], session[33], lease[33], partial[80], path[1024], name[256];
	uint64_t size, received, claimed, expires, lease_expires;
};
struct media_files {
	char root[4096], blocked[192];
	int stage;
	struct upload_slot slots[2];
};
static int stage_open(struct media *m, int create);
static void string(struct json_object *r, const char *key, const char *value)
{
	json_object_object_add(r,key,json_object_new_string(value));
}
static void number(struct json_object *r, const char *key, uint64_t n)
{
	json_object_object_add(r,key,json_object_new_int64((int64_t)n));
}
uint64_t media_files_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC,&ts);
	return (uint64_t)ts.tv_sec;
}
static int equal(const char *a, const char *b, size_t length)
{
	unsigned different=0;
	for (size_t i=0;i<length;++i) different|=(unsigned char)a[i]^(unsigned char)b[i];
	return !different;
}
static int random_hex(char *out, size_t bytes)
{
	unsigned char data[32];
	if (bytes>sizeof(data)) return -1;
	int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
	if (fd<0) return -1;
	size_t done=0;
	while (done<bytes) {
		ssize_t n=read(fd,data+done,bytes-done);
		if (n<0 && errno==EINTR) continue;
		if (n<=0) { close(fd); return -1; }
		done+=(size_t)n;
	}
	close(fd);
	const char *hex="0123456789abcdef";
	for (size_t i=0;i<bytes;++i) { out[i*2]=hex[data[i]>>4]; out[i*2+1]=hex[data[i]&15]; }
	out[bytes*2]=0;
	memset(data,0,sizeof(data));
	return 0;
}
static int writable_path(const char *path)
{
	if (!path || !*path || !media_path_valid(path) || !media_file_supported(path)) return 0;
	for (const char *p=path;*p;) {
		const char *end=strchr(p,'/');
		size_t size=end ? (size_t)(end-p) : strlen(p);
		if (*p=='.' || (size==10 && !memcmp(p,"lost+found",10))) return 0;
		p=end ? end+1 : p+size;
	}
	return 1;
}
static int parent_open(struct media *m, const char *path, char *name)
{
	char copy[1024], *save=NULL;
	if (!writable_path(path)) return -1;
	snprintf(copy,sizeof(copy),"%s",path);
	char *last=strrchr(copy,'/');
	const char *base=last ? last+1 : copy;
	if (!*base || strlen(base)>255) return -1;
	snprintf(name,256,"%s",base);
	if (last) *last=0;
	int fd=openat(m->rootfd,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
	if (fd<0 || !fs_same_mount(m->rootfd,fd)) { if (fd>=0) close(fd); return -1; }
	if (!last) return fd;
	for (char *part=strtok_r(copy,"/",&save);part;part=strtok_r(NULL,"/",&save)) {
		int next=openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
		close(fd);
		if (next<0) return -1;
		if (!fs_same_mount(m->rootfd,next)) { close(next); return -1; }
		fd=next;
	}
	return fd;
}
static int information(struct media *m, struct fs_mount_info *info)
{
	struct media_files *f=m->files;
	if (!f) { memset(info,0,sizeof(*info)); snprintf(info->reason,sizeof(info->reason),"file management unavailable"); return -1; }
	int rc=fs_mount_check(m->rootfd,f->root,info);
	if (!rc) {
		struct stat existing;
		if ((!fstatat(m->rootfd,STAGE,&existing,AT_SYMLINK_NOFOLLOW) || f->stage>=0) &&
		    stage_open(m,0)) {
			info->writable=0;
			snprintf(info->reason,sizeof(info->reason),"private media staging is unavailable or untrusted");
			return -1;
		}
	}
	if (f->blocked[0]) {
		info->writable=0;
		snprintf(info->reason,sizeof(info->reason),"%s",f->blocked);
		return -1;
	}
	return rc;
}
static void release_slot(struct media *m, struct upload_slot *slot)
{
	if (!slot->used) return;
	if (m->files->stage>=0) unlinkat(m->files->stage,slot->partial,0);
	if (slot->fd>=0) close(slot->fd);
	if (slot->parent>=0) close(slot->parent);
	memset(slot,0,sizeof(*slot));
	slot->fd=slot->parent=-1;
}
static void discard(struct media *m, struct upload_slot *slot)
{
	if (!slot->used) return;
	if (m->files->stage>=0) unlinkat(m->files->stage,slot->partial,0);
	if (slot->lease[0]) {
		/* Never reuse claimed /proc FD numbers/inodes before worker ack.
		 * Lease is 90s, longer than the CGI's hard 60s lifetime. */
		slot->canceled=1;
		return;
	}
	release_slot(m,slot);
}
void media_files_expire(struct media *m, uint64_t now)
{
	if (!m->files) return;
	for (size_t i=0;i<2;++i) {
		struct upload_slot *s=&m->files->slots[i];
		if (s->used && s->lease[0] && now>=s->lease_expires) release_slot(m,s);
		else if (s->used && now>=s->expires) discard(m,s);
	}
}
static int safe_stage_file(int fd, struct stat *st)
{
	return fd>=0 && !fstat(fd,st) && S_ISREG(st->st_mode) && st->st_uid==geteuid() &&
		st->st_nlink==1 && (st->st_mode&0777)==0600;
}
static int stage_open(struct media *m, int create)
{
	struct media_files *f=m->files;
	struct stat st, current;
	if (f->stage>=0) {
		if (!fstat(f->stage,&st) &&
		    !fstatat(m->rootfd,STAGE,&current,AT_SYMLINK_NOFOLLOW) &&
		    st.st_dev==current.st_dev && st.st_ino==current.st_ino &&
		    st.st_uid==geteuid() && (st.st_mode&0777)==0700 &&
		    fs_same_mount(m->rootfd,f->stage)) return 0;
		return -1;
	}
	int made=0;
	if (create && !mkdirat(m->rootfd,STAGE,0700)) made=1;
	else if (create && errno!=EEXIST) return -1;
	int fd=openat(m->rootfd,STAGE,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
	if (fd<0) return -1;
	if (fstat(fd,&st) || st.st_uid!=geteuid() || (st.st_mode&0777)!=0700 ||
	    !fs_same_mount(m->rootfd,fd)) { close(fd); return -1; }
	if (made) {
		int owner=openat(fd,".owner",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
		if (owner<0) { close(fd); return -1; }
		ssize_t n=write(owner,OWNER_TEXT,sizeof(OWNER_TEXT)-1);
		close(owner);
		if (n!=(ssize_t)(sizeof(OWNER_TEXT)-1)) { close(fd); return -1; }
	}
	int owner=openat(fd,".owner",O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
	char marker[sizeof(OWNER_TEXT)];
	if (owner<0) { close(fd); return -1; }
	int valid=safe_stage_file(owner,&st) && st.st_size==(off_t)(sizeof(OWNER_TEXT)-1) &&
		read(owner,marker,sizeof(marker))==(ssize_t)(sizeof(OWNER_TEXT)-1) &&
		!memcmp(marker,OWNER_TEXT,sizeof(OWNER_TEXT)-1);
	close(owner);
	if (!valid) { close(fd); return -1; }
	f->stage=fd;
	return 0;
}
static void orphan_cleanup(struct media *m)
{
	struct media_files *f=m->files;
	int fd=openat(f->stage,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
	if (fd<0) return;
	DIR *dir=fdopendir(fd);
	if (!dir) { close(fd); return; }
	struct dirent *entry;
	int scanned=0;
	while ((entry=readdir(dir))) {
		if (++scanned>256) { snprintf(f->blocked,sizeof(f->blocked),"staging cleanup limit reached; operator review required"); break; }
		if (!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..") ||
		    !strcmp(entry->d_name,".owner")) continue;
		struct stat st;
		char token[65];
		size_t size=strlen(entry->d_name);
		if (size==69) { memcpy(token,entry->d_name,64); token[64]=0; }
		if (size!=69 || strcmp(entry->d_name+64,".part") || !upload_hex(token,64) ||
		    fstatat(fd,entry->d_name,&st,AT_SYMLINK_NOFOLLOW) || !S_ISREG(st.st_mode) ||
		    st.st_uid!=geteuid() || st.st_nlink!=1 || (st.st_mode&0777)!=0600 || st.st_dev==0 ||
		    !fs_same_mount(m->rootfd,fd) || unlinkat(fd,entry->d_name,0)) {
			snprintf(f->blocked,sizeof(f->blocked),"unexpected private staging entry; operator review required");
			break;
		}
	}
	closedir(dir);
}
void media_files_init(struct media *m, const char *root)
{
	m->files=calloc(1,sizeof(*m->files));
	if (!m->files) return;
	m->files->stage=-1;
	for (size_t i=0;i<2;++i) m->files->slots[i].fd=m->files->slots[i].parent=-1;
	if (strlen(root)>=sizeof(m->files->root)) {
		snprintf(m->files->blocked,sizeof(m->files->blocked),"configured media root is too long");
		return;
	}
	snprintf(m->files->root,sizeof(m->files->root),"%s",root);
	struct fs_mount_info info;
	if (!information(m,&info)) {
		struct stat existing;
		if (!fstatat(m->rootfd,STAGE,&existing,AT_SYMLINK_NOFOLLOW)) {
			if (stage_open(m,0)) snprintf(m->files->blocked,sizeof(m->files->blocked),"untrusted media staging directory");
			else orphan_cleanup(m);
		}
	}
}
void media_files_close(struct media *m)
{
	if (!m->files) return;
	for (size_t i=0;i<2;++i) release_slot(m,&m->files->slots[i]);
	if (m->files->stage>=0) close(m->files->stage);
	free(m->files);
	m->files=NULL;
}
static uint64_t remaining(struct media *m)
{
	uint64_t reserved=0;
	for (size_t i=0;i<2;++i) {
		struct upload_slot *s=&m->files->slots[i];
		if (s->used && !s->canceled) reserved+=s->size-s->received;
	}
	return reserved;
}
static uint64_t capacity(struct media *m, const struct fs_mount_info *info)
{
	uint64_t reserved=remaining(m);
	if (info->free<UPLOAD_RESERVE || info->free-UPLOAD_RESERVE<reserved) return 0;
	uint64_t available=info->free-UPLOAD_RESERVE-reserved;
	return available>UPLOAD_MAX_SAFE ? UPLOAD_MAX_SAFE : available;
}
struct json_object *media_files_storage(struct media *m)
{
	struct fs_mount_info info;
	media_files_expire(m,media_files_now());
	int writable=!information(m,&info);
	struct json_object *r=upload_result(NULL);
	json_object_object_add(r,"mounted",json_object_new_boolean(info.mounted));
	const char *flags[]={"writable","upload","delete"};
	for (size_t i=0;i<3;++i) json_object_object_add(r,flags[i],json_object_new_boolean(writable));
	json_object_object_add(r,"reason",writable ? NULL : json_object_new_string(info.reason));
	number(r,"total_bytes",info.total>UPLOAD_MAX_SAFE ? UPLOAD_MAX_SAFE : info.total);
	number(r,"free_bytes",info.free>UPLOAD_MAX_SAFE ? UPLOAD_MAX_SAFE : info.free);
	uint64_t usable=writable ? capacity(m,&info) : 0;
	number(r,"usable_bytes",usable);
	number(r,"max_file_bytes",usable);
	number(r,"reserve_bytes",UPLOAD_RESERVE);
	number(r,"chunk_size",UPLOAD_CHUNK);
	number(r,"max_uploads",2);
	unsigned active=0;
	if (m->files) for (size_t i=0;i<2;++i) active+=m->files->slots[i].used!=0;
	number(r,"active_uploads",active);
	return r;
}
static struct upload_slot *find(struct media *m, const char *session, const char *token)
{
	if (!m->files || !upload_hex(session,32) || !upload_hex(token,64)) return NULL;
	for (size_t i=0;i<2;++i) {
		struct upload_slot *s=&m->files->slots[i];
		if (s->used && equal(s->token,token,64) && equal(s->session,session,32)) return s;
	}
	return NULL;
}
static struct json_object *begin(struct media *m, struct json_object *args, const char *session)
{
	const char *path=upload_string(args,"path");
	uint64_t size;
	struct fs_mount_info info;
	if (!writable_path(path) || upload_number(args,"size",&size) || !size)
		return upload_result("invalid relative media path or size");
	if (information(m,&info)) return upload_result(info.reason);
	uint64_t max=capacity(m,&info);
	if (size>max) return upload_result("insufficient media space after safety reserve");
	struct upload_slot *slot=NULL;
	for (size_t i=0;i<2;++i) {
		struct upload_slot *s=&m->files->slots[i];
		if (s->used && !s->canceled && !strcmp(s->path,path)) return upload_result("upload already targets this path");
		if (!s->used && !slot) slot=s;
	}
	if (!slot) return upload_result("two pending uploads already exist");
	char name[256];
	int parent=parent_open(m,path,name);
	if (parent<0) return upload_result("destination parent unavailable or crosses media mount");
	struct stat st;
	if (!fstatat(parent,name,&st,AT_SYMLINK_NOFOLLOW) || errno!=ENOENT) {
		close(parent); return upload_result("destination already exists or is unavailable");
	}
	if (stage_open(m,1)) { close(parent); return upload_result("private staging unavailable or untrusted"); }
	char token[65];
	if (random_hex(token,32)) { close(parent); return upload_result("secure upload capability unavailable"); }
	char partial[80];
	snprintf(partial,sizeof(partial),"%s.part",token);
	int fd=openat(m->files->stage,partial,O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
	if (fd<0) { close(parent); return upload_result("cannot create private media staging file"); }
	if (!safe_stage_file(fd,&st) || !fs_same_mount(m->rootfd,fd)) {
		close(fd); close(parent); unlinkat(m->files->stage,partial,0);
		return upload_result("unsafe media staging file");
	}
	memset(slot,0,sizeof(*slot));
	slot->used=1; slot->fd=fd; slot->parent=parent; slot->size=size;
	slot->expires=media_files_now()+UPLOAD_EXPIRY;
	snprintf(slot->session,sizeof(slot->session),"%s",session);
	snprintf(slot->token,sizeof(slot->token),"%s",token);
	snprintf(slot->partial,sizeof(slot->partial),"%s",partial);
	snprintf(slot->name,sizeof(slot->name),"%s",name);
	snprintf(slot->path,sizeof(slot->path),"%s",path);
	struct json_object *r=upload_result(NULL);
	string(r,"token",token); string(r,"path",path);
	number(r,"offset",0); number(r,"size",size); number(r,"max_size",max);
	number(r,"chunk_size",UPLOAD_CHUNK); number(r,"expires_in",UPLOAD_EXPIRY);
	return r;
}
static struct json_object *claim(struct media *m, struct upload_slot *s, struct json_object *args)
{
	uint64_t offset,length;
	struct fs_mount_info info;
	if (s->canceled) return upload_result("upload was canceled");
	if (upload_number(args,"offset",&offset) || upload_number(args,"length",&length) ||
	    !length || length>UPLOAD_CHUNK || offset!=s->received ||
	    length>s->size-s->received) return upload_result("invalid or nonsequential upload chunk");
	if (s->lease[0]) return upload_result("an upload chunk is already in progress");
	if (information(m,&info) || stage_open(m,0) || !fs_same_mount(m->rootfd,s->parent)) {
		discard(m,s); return upload_result("media mount changed or became unavailable");
	}
	if (info.free<UPLOAD_RESERVE || info.free-UPLOAD_RESERVE<remaining(m)) {
		discard(m,s); return upload_result("media space changed; upload canceled to preserve reserve");
	}
	struct stat st, parent, staging;
	if (!safe_stage_file(s->fd,&st) || (uint64_t)st.st_size!=s->received ||
	    fstat(s->parent,&parent) || fstat(m->files->stage,&staging) || flock(s->fd,LOCK_EX|LOCK_NB)) {
		return upload_result("upload staging identity, size or lock mismatch");
	}
	flock(s->fd,LOCK_UN);
	if (random_hex(s->lease,16)) return upload_result("secure chunk lease unavailable");
	s->claimed=length;
	s->lease_expires=media_files_now()+UPLOAD_LEASE;
	s->expires=media_files_now()+UPLOAD_EXPIRY;
	char fdpath[80], parentpath[80], stagepath[80];
	snprintf(fdpath,sizeof(fdpath),"/proc/%ld/fd/%d",(long)getpid(),s->fd);
	snprintf(parentpath,sizeof(parentpath),"/proc/%ld/fd/%d",(long)getpid(),s->parent);
	snprintf(stagepath,sizeof(stagepath),"/proc/%ld/fd/%d",(long)getpid(),m->files->stage);
	struct json_object *r=upload_result(NULL);
	string(r,"fd_path",fdpath); string(r,"parent_fd_path",parentpath);
	string(r,"stage_fd_path",stagepath);
	string(r,"lease",s->lease); string(r,"path",s->path);
	number(r,"device",(uint64_t)st.st_dev); number(r,"inode",(uint64_t)st.st_ino);
	number(r,"parent_inode",(uint64_t)parent.st_ino);
	number(r,"stage_inode",(uint64_t)staging.st_ino);
	number(r,"offset",s->received); number(r,"length",length); number(r,"size",s->size);
	return r;
}
static struct json_object *done(struct media *m, struct upload_slot *s, struct json_object *args)
{
	const char *lease=upload_string(args,"lease");
	uint64_t offset,length;
	if (!upload_hex(lease,32) || !s->lease[0] || !equal(s->lease,lease,32) ||
	    upload_number(args,"offset",&offset) || offset!=s->received ||
	    upload_number(args,"length",&length) || length!=s->claimed)
		return upload_result("upload lease or completion offset mismatch");
	if (s->canceled) { release_slot(m,s); return upload_result("upload was canceled; nothing published"); }
	struct fs_mount_info info;
	struct stat st, named;
	if (information(m,&info) || stage_open(m,0) || !fs_same_mount(m->rootfd,s->parent) ||
	    !safe_stage_file(s->fd,&st) || (uint64_t)st.st_size!=s->received+length ||
	    fstatat(m->files->stage,s->partial,&named,AT_SYMLINK_NOFOLLOW) ||
	    named.st_ino!=st.st_ino || named.st_dev!=st.st_dev || !S_ISREG(named.st_mode)) {
		release_slot(m,s); return upload_result("upload storage or file identity changed; canceled");
	}
	if (flock(s->fd,LOCK_EX|LOCK_NB)) return upload_result("upload writer still holds the chunk lock");
	flock(s->fd,LOCK_UN);
	s->received+=length;
	s->lease[0]=0; s->claimed=0;
	s->expires=media_files_now()+UPLOAD_EXPIRY;
	int complete=s->received==s->size;
	struct json_object *r=upload_result(NULL);
	number(r,"offset",s->received); number(r,"size",s->size); string(r,"path",s->path);
	json_object_object_add(r,"complete",json_object_new_boolean(complete));
	if (complete) {
		if (info.free<UPLOAD_RESERVE ||
		    linkat(m->files->stage,s->partial,s->parent,s->name,0)) {
			json_object_put(r);
			discard(m,s);
			return upload_result("destination exists or atomic publication failed; no existing file overwritten");
		}
		discard(m,s);
	}
	return r;
}
static int active_file(struct media *m, const struct stat *target)
{
	struct stat playing;
	return m->mediafd>=0 && !fstat(m->mediafd,&playing) &&
		playing.st_dev==target->st_dev && playing.st_ino==target->st_ino;
}
static struct json_object *remove_file(struct media *m, const char *path)
{
	struct fs_mount_info info;
	if (information(m,&info)) return upload_result(info.reason);
	char name[256];
	int parent=parent_open(m,path,name);
	if (parent<0) return upload_result("invalid media path or cross-mount parent");
	struct stat st, moved;
#ifdef __linux__
	int fd=openat(parent,name,O_PATH|O_NOFOLLOW|O_CLOEXEC);
#else
	int fd=openat(parent,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
#endif
	if (fd<0 || fstat(fd,&st) || !S_ISREG(st.st_mode) || !fs_same_mount(m->rootfd,fd)) {
		if (fd>=0) close(fd);
		close(parent); return upload_result("only existing regular media files can be removed");
	}
	if (active_file(m,&st)) { close(fd); close(parent); return upload_result("file is held for playback; Stop first"); }
	if (stage_open(m,1)) { close(fd); close(parent); return upload_result("private media staging unavailable"); }
	char random[33], hold[48];
	if (random_hex(random,16)) { close(fd); close(parent); return upload_result("secure removal unavailable"); }
	snprintf(hold,sizeof(hold),"rm-%s.hold",random);
	if (fs_rename_noreplace(parent,name,m->files->stage,hold)) {
		close(fd); close(parent); return upload_result("media removal could not claim the requested file");
	}
	int valid=!fstatat(m->files->stage,hold,&moved,AT_SYMLINK_NOFOLLOW) &&
		S_ISREG(moved.st_mode) && st.st_dev==moved.st_dev && st.st_ino==moved.st_ino &&
		!active_file(m,&moved);
	if (!valid || unlinkat(m->files->stage,hold,0)) {
		if (fs_rename_noreplace(m->files->stage,hold,parent,name))
			snprintf(m->files->blocked,sizeof(m->files->blocked),"remove race: protected staging requires operator review");
		close(fd); close(parent); return upload_result("file changed during removal; not deleted");
	}
	close(fd); close(parent);
	for (size_t i=0;i<m->queue_len;) {
		if (!strcmp(m->queue[i],path)) {
			--m->queue_len;
			memmove(m->queue+i,m->queue+i+1,(m->queue_len-i)*sizeof(m->queue[0]));
		} else ++i;
	}
	struct json_object *r=upload_result(NULL);
	string(r,"path",path);
	return r;
}
struct json_object *media_files_call(struct media *m, const char *method, struct json_object *args)
{
	if (!strcmp(method,"storage")) return media_files_storage(m);
	int public_write=!strcmp(method,"upload_begin") || !strcmp(method,"upload_cancel") || !strcmp(method,"remove");
	int internal=!strcmp(method,"_upload_claim") || !strcmp(method,"_upload_done") ||
		!strcmp(method,"_upload_abort") || !strcmp(method,"_upload_check");
	if (!public_write && !internal) return NULL;
	struct json_object *injected;
	if (internal && args && json_object_object_get_ex(args,"ubus_rpc_session",&injected))
		return upload_result("private upload coordination is not available through HTTP RPC");
	const char *session=upload_string(args,internal ? "session" : "ubus_rpc_session");
	if (!upload_hex(session,32) || (public_write && !upload_acl(session,method)))
		return upload_result("authenticated media write permission required");
	if (!m->files) return upload_result("file management unavailable");
	media_files_expire(m,media_files_now());
	if (!strcmp(method,"upload_begin")) return begin(m,args,session);
	if (!strcmp(method,"remove")) return remove_file(m,upload_string(args,"path"));
	const char *token=upload_string(args,"token");
	if (!upload_hex(token,64)) return upload_result("invalid upload capability");
	struct upload_slot *s=find(m,session,token);
	if (!strcmp(method,"upload_cancel")) {
		if (!s) {
			for (size_t i=0;i<2;++i)
				if (m->files->slots[i].used && equal(m->files->slots[i].token,token,64))
					return upload_result("upload belongs to another session");
		}
		struct json_object *r=upload_result(NULL);
		json_object_object_add(r,"canceled",json_object_new_boolean(s && !s->canceled));
		if (s) discard(m,s);
		return r;
	}
	if (!s) return upload_result("upload expired, canceled or belongs to another session");
	if (!strcmp(method,"_upload_claim")) return claim(m,s,args);
	if (!strcmp(method,"_upload_done")) return done(m,s,args);
	const char *lease=upload_string(args,"lease");
	if (!upload_hex(lease,32) || !s->lease[0] || !equal(s->lease,lease,32))
		return upload_result("upload abort lease mismatch");
	if (!strcmp(method,"_upload_check")) {
		struct fs_mount_info info;
		struct stat st, named;
		if (s->canceled || information(m,&info) || stage_open(m,0) ||
		    !safe_stage_file(s->fd,&st) || (uint64_t)st.st_size!=s->received ||
		    fstatat(m->files->stage,s->partial,&named,AT_SYMLINK_NOFOLLOW) ||
		    !S_ISREG(named.st_mode) || named.st_dev!=st.st_dev || named.st_ino!=st.st_ino)
			return upload_result("upload canceled or stage identity changed before body read");
		return upload_result(NULL);
	}
	release_slot(m,s);
	return upload_result(NULL);
}
