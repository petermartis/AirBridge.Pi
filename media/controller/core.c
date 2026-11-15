#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "core.h"
#include "files.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define INPUT_MAX 8192
#define RESPONSE_MAX 65536
#define TIMEOUT_MS 700
static struct json_object *result(const char *error)
{
	struct json_object *r = json_object_new_object();
	json_object_object_add(r, "ok", json_object_new_boolean(!error));
	json_object_object_add(r, "error", error ? json_object_new_string(error) : NULL);
	return r;
}
static void addstr(struct json_object *o, const char *k, const char *v)
{
	json_object_object_add(o, k, json_object_new_string(v));
}
static const char *strarg(struct json_object *a, const char *key)
{
	struct json_object *v;
	if (!a || !json_object_object_get_ex(a, key, &v) ||
	    !json_object_is_type(v, json_type_string)) return NULL;
	/* Reject embedded NUL, not just the visible prefix. */
	if ((size_t)json_object_get_string_len(v) != strlen(json_object_get_string(v))) return NULL;
	return json_object_get_string(v);
}
static int numarg(struct json_object *a, const char *key, double *n)
{
	struct json_object *v;
	if (!a || !json_object_object_get_ex(a, key, &v) ||
	    !(json_object_is_type(v, json_type_int) || json_object_is_type(v, json_type_double)))
		return -1;
	*n = json_object_get_double(v);
	return isfinite(*n) ? 0 : -1;
}
static int boolarg(struct json_object *a, const char *key, int *n)
{
	struct json_object *v;
	if (!a || !json_object_object_get_ex(a, key, &v) ||
	    !json_object_is_type(v, json_type_boolean)) return -1;
	*n = json_object_get_boolean(v);
	return 0;
}
static const char *format(const char *path)
{
	const char *ext = strrchr(path, '.');
	static const struct { const char *ext, *format; } types[] = {
		{"mkv","matroska"},{"webm","matroska"},{"mp4","mov"},{"m4v","mov"},
		{"mov","mov"},{"avi","avi"},{"ts","mpegts"},{"m2ts","mpegts"},
		{"mpg","mpeg"},{"mpeg","mpeg"},{"mp3","mp3"},{"flac","flac"},
		{"wav","wav"},{"ogg","ogg"},{"opus","ogg"},{"aac","aac"}
	};
	if (!ext) return NULL;
	for (size_t i = 0; i < sizeof(types)/sizeof(types[0]); ++i)
		if (!strcasecmp(ext + 1, types[i].ext)) return types[i].format;
	return NULL;
}
static int utf8(const char *text)
{
	const unsigned char *p=(const unsigned char *)text;
	while (*p) {
		unsigned c=*p++, cp, minimum;
		int n;
		if (c<128) continue;
		if (c>=0xc2 && c<=0xdf) { n=1; cp=c&31; minimum=128; }
		else if (c>=0xe0 && c<=0xef) { n=2; cp=c&15; minimum=2048; }
		else if (c>=0xf0 && c<=0xf4) { n=3; cp=c&7; minimum=65536; }
		else return 0;
		while (n--) {
			if ((*p&0xc0)!=0x80) return 0;
			cp=(cp<<6)|(*p++&63);
		}
		if (cp<minimum || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) return 0;
	}
	return 1;
}
static int validpath(const char *path)
{
	const char *s, *e;
	if (!path || strlen(path) >= 1024 || path[0] == '/' || strchr(path, '\\') ||
	    strchr(path, ':') || !utf8(path)) return 0;
	for (s = path; *s; s = *e ? e + 1 : e) {
		e = strchr(s, '/');
		if (!e) e = s + strlen(s);
		if (e == s || (e-s == 1 && s[0] == '.') ||
		    (e-s == 2 && s[0] == '.' && s[1] == '.')) return 0;
		for (const char *p = s; p < e; ++p)
			if ((unsigned char)*p < 32 || (unsigned char)*p == 127) return 0;
		if (*e && !e[1]) return 0;
	}
	return 1;
}
int media_path_valid(const char *path)
{
	return validpath(path);
}
int media_file_supported(const char *path)
{
	return path && format(path)!=NULL;
}
/* Resolve each component through held directories. No symlinks, devices, or
 * pathname TOCTOU: the player receives the already-open regular file. */
static int localopen(struct media *m, const char *path, int directory)
{
	char copy[1024], *part, *save;
	int fd, next;
	struct stat st;
	if (!validpath(path) || (!directory && (!*path || !format(path)))) return -1;
	fd = openat(m->rootfd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0) return -1;
	if (*path) {
		snprintf(copy, sizeof(copy), "%s", path);
		for (part = strtok_r(copy, "/", &save); part; part = strtok_r(NULL, "/", &save)) {
			int flags = O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC;
			if ((save && *save) || directory) flags |= O_DIRECTORY;
#ifdef __linux__
			/* Inspect final inode without opening a device/FIFO for I/O.
			 * Reopen the checked O_PATH handle, never the original name. */
			else {
				next=openat(fd,part,O_PATH|O_NOFOLLOW|O_CLOEXEC);
				close(fd);
				if (next<0) return -1;
				if (fstat(next,&st) || !S_ISREG(st.st_mode)) { close(next); return -1; }
				char held[64];
				snprintf(held,sizeof(held),"/proc/self/fd/%d",next);
				fd=open(held,O_RDONLY|O_CLOEXEC);
				close(next);
				if (fd<0) return -1;
				continue;
			}
#endif
			next = openat(fd, part, flags);
			close(fd);
			if (next < 0) return -1;
			fd = next;
		}
	}
	if (fstat(fd, &st) || (directory ? !S_ISDIR(st.st_mode) : !S_ISREG(st.st_mode))) {
		close(fd);
		return -1;
	}
	return fd;
}
int media_init(struct media *m, const char *root, const char *socket_path)
{
	memset(m, 0, sizeof(*m));
	m->rootfd = m->mediafd = m->ipcfd = -1;
	if (!root || root[0]!='/' || !socket_path || strlen(socket_path) >= sizeof(m->socket_path)) return -1;
	m->rootfd = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	signal(SIGPIPE, SIG_IGN);
	snprintf(m->socket_path, sizeof(m->socket_path), "%s", socket_path);
	snprintf(m->profile, sizeof(m->profile), "quality4k");
	snprintf(m->soft_mode, sizeof(m->soft_mode), "1920x1080@30");
	media_files_init(m,root);
	return 0;
}
void media_close(struct media *m)
{
	media_files_close(m);
	if (m->mediafd >= 0) close(m->mediafd);
	if (m->rootfd >= 0) close(m->rootfd);
	if (m->ipcfd >= 0) close(m->ipcfd);
}
static long long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static int ready(int fd, short events, long long deadline)
{
	struct pollfd p = { .fd = fd, .events = events };
	int remaining = (int)(deadline - now_ms());
	return remaining > 0 && poll(&p, 1, remaining) > 0 && (p.revents & events);
}
static void disconnect(struct media *m)
{
	if (m->ipcfd>=0) close(m->ipcfd);
	m->ipcfd=-1;
	m->readlen=0;
}
static void event(struct media *m, struct json_object *r)
{
	const char *name=strarg(r,"event");
	if (!name) return;
	if (!strcmp(name,"file-loaded")) m->file_loaded=1;
	if (!strcmp(name,"end-file")) {
		const char *reason=strarg(r,"reason"), *error=strarg(r,"file_error");
		if (reason && !strcmp(reason,"error"))
			snprintf(m->error,sizeof(m->error),"player: %s",error ? error : "media playback failed");
	}
}
static struct json_object *frames(struct media *m, unsigned id)
{
	struct json_object *reply=NULL;
	char *end;
	while ((end=memchr(m->readbuf,'\n',m->readlen))) {
		*end=0;
		struct json_object *r=json_tokener_parse(m->readbuf), *rid;
		if (r) {
			event(m,r);
			if (id && json_object_object_get_ex(r,"request_id",&rid) &&
			    json_object_get_int64(rid)==id && !reply) reply=r;
			else json_object_put(r);
		}
		size_t consumed=(size_t)(end-m->readbuf)+1;
		m->readlen-=consumed;
		memmove(m->readbuf,m->readbuf+consumed,m->readlen);
	}
	return reply;
}
/* Only fixed command builders call this function; no user-provided mpv JSON.
 * Keep one private connection so file-loaded/end-file events are not lost
 * between short-lived property requests. */
static struct json_object *ipc(struct media *m, struct json_object *command)
{
	struct sockaddr_un addr;
	struct json_object *request = json_object_new_object(), *reply = NULL;
	size_t sent = 0;
	unsigned id = ++m->request_id;
	long long deadline = now_ms() + TIMEOUT_MS;
	if (m->deadline && deadline > m->deadline) deadline = m->deadline;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", m->socket_path);
	json_object_object_add(request, "command", command);
	json_object_object_add(request, "request_id", json_object_new_int64(id));
	const char *wire = json_object_to_json_string_ext(request, JSON_C_TO_STRING_PLAIN);
	size_t len = strlen(wire);
	if (m->ipcfd<0) {
		m->ipcfd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (m->ipcfd<0 || fcntl(m->ipcfd,F_SETFL,O_NONBLOCK)<0) goto fail;
		if (connect(m->ipcfd,(struct sockaddr *)&addr,sizeof(addr))<0 && errno!=EINPROGRESS)
			goto fail;
	}
	/* Include the framing newline in the same bounded nonblocking write loop. */
	char output[INPUT_MAX + 1];
	if (len + 1 > sizeof(output)) goto fail;
	memcpy(output, wire, len);
	output[len++] = '\n';
	while (sent < len) {
		if (!ready(m->ipcfd, POLLOUT, deadline)) goto fail;
#ifdef MSG_NOSIGNAL
		ssize_t n = send(m->ipcfd, output + sent, len - sent, MSG_NOSIGNAL);
#else
		ssize_t n = send(m->ipcfd, output + sent, len - sent, 0);
#endif
		if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
		if (n <= 0) goto fail;
		sent += (size_t)n;
	}
	while (m->readlen < RESPONSE_MAX) {
		reply=frames(m,id);
		if (reply) {
			struct json_object *err;
			if (json_object_object_get_ex(reply,"error",&err) &&
			    json_object_is_type(err,json_type_string) &&
			    !strcmp(json_object_get_string(err),"success")) goto out;
			json_object_put(reply);
			reply=NULL;
			goto out;
		}
		if (!ready(m->ipcfd, POLLIN, deadline)) goto fail;
		ssize_t n = read(m->ipcfd,m->readbuf+m->readlen,RESPONSE_MAX-m->readlen);
		if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
		if (n <= 0) goto fail;
		m->readlen+=(size_t)n;
	}
fail:
	disconnect(m);
out:
	json_object_put(request);
	return reply;
}
static struct json_object *cmd(const char *name)
{
	struct json_object *a = json_object_new_array();
	json_object_array_add(a, json_object_new_string(name));
	return a;
}
static struct json_object *get(struct media *m, const char *name)
{
	struct json_object *a = cmd("get_property"), *r, *data = NULL;
	json_object_array_add(a, json_object_new_string(name));
	r = ipc(m, a);
	if (r) {
		if (json_object_object_get_ex(r, "data", &data)) json_object_get(data);
		json_object_put(r);
	}
	return data;
}
static int set(struct media *m, const char *name, struct json_object *value)
{
	struct json_object *a = cmd("set_property"), *r;
	json_object_array_add(a, json_object_new_string(name));
	json_object_array_add(a, value);
	r = ipc(m, a);
	if (!r) return -1;
	json_object_put(r);
	return 0;
}
static int simple(struct media *m, const char *name)
{
	struct json_object *r = ipc(m, cmd(name));
	if (!r) return -1;
	json_object_put(r);
	return 0;
}
static int stop_reason(struct media *m, const char *reason)
{
	char copy[160];
	snprintf(copy,sizeof(copy),"%s",reason);
	int failed=simple(m,"stop");
	snprintf(m->error,sizeof(m->error),"%s; %s",copy,
		failed ? "stop failed: player IPC unavailable" : "playback stopped");
	return failed;
}
static struct json_object *browse(struct media *m, struct json_object *args)
{
	const char *path = strarg(args, "path");
	double offset = 0, limit = 100;
	struct json_object *v;
	if (args && json_object_object_get_ex(args, "path", &v) && !path)
		return result("path must be a relative string");
	if (!path) path = "";
	if (args && json_object_object_get_ex(args, "offset", &v) && numarg(args,"offset",&offset))
		return result("invalid offset");
	if (args && json_object_object_get_ex(args, "limit", &v) && numarg(args,"limit",&limit))
		return result("invalid limit");
	if (offset < 0 || offset > 4096 || floor(offset) != offset ||
	    limit < 1 || limit > 200 || floor(limit) != limit) return result("pagination out of range");
	int fd = localopen(m, path, 1);
	if (fd < 0) return result("local directory unavailable or forbidden");
	DIR *d = fdopendir(fd);
	if (!d) { close(fd); return result("directory unavailable"); }
	struct json_object *r = result(NULL), *entries = json_object_new_array();
	struct dirent *entry;
	int scanned = 0, visible = 0, count = 0, more = 0;
	size_t output_bytes=4096;
	while ((entry = readdir(d))) {
		struct stat st;
		char rel[1024];
		if (++scanned > 4096) { more = 1; break; }
		if (entry->d_name[0] == '.' || !strcmp(entry->d_name,"lost+found")) continue;
		int n = snprintf(rel, sizeof(rel), "%s%s%s", path, *path ? "/" : "", entry->d_name);
		if (n < 0 || (size_t)n >= sizeof(rel) || !validpath(rel) ||
		    fstatat(fd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW)) continue;
		if (!S_ISDIR(st.st_mode) && !(S_ISREG(st.st_mode) && format(rel))) continue;
		if (visible++ < (int)offset) continue;
		if (count == (int)limit) { more = 1; break; }
		struct json_object *item = json_object_new_object();
		addstr(item, "name", entry->d_name);
		addstr(item, "path", rel);
		addstr(item, "type", S_ISDIR(st.st_mode) ? "directory" : "file");
		json_object_object_add(item,"size_bytes",S_ISREG(st.st_mode) ?
			json_object_new_int64((int64_t)st.st_size) : NULL);
		output_bytes+=strlen(json_object_to_json_string_ext(item,JSON_C_TO_STRING_PLAIN))+1;
		if (output_bytes>196608) {
			json_object_put(item);
			more=1;
			break;
		}
		json_object_array_add(entries, item);
		++count;
	}
	closedir(d);
	addstr(r, "path", path);
	json_object_object_add(r,"entries",entries);
	json_object_object_add(r,"offset",json_object_new_int((int)offset));
	json_object_object_add(r,"more",json_object_new_boolean(more));
	json_object_object_add(r,"scan_limited",json_object_new_boolean(scanned > 4096));
	return r;
}
static int profile(struct media *m, const char *name)
{
	if (!name || (strcmp(name,"balanced") && strcmp(name,"quality4k") && strcmp(name,"quiet")))
		return -2;
	/* Fixed runtime controls, no arbitrary mpv profiles or user config. */
	char selected[16];
	snprintf(selected,sizeof(selected),"%s",name);
	if (set(m,"hwdec",json_object_new_string("auto-safe")) ||
	    set(m,"hwdec-software-fallback",json_object_new_string(!strcmp(name,"quality4k") ? "no" : "yes")) ||
	    set(m,"vd-lavc-threads",json_object_new_int(!strcmp(name,"quiet") ? 2 : 0)) ||
	    set(m,"deband",json_object_new_boolean(0)) ||
	    set(m,"interpolation",json_object_new_boolean(0))) {
		stop_reason(m,"profile configuration failed");
		return -1;
	}
	snprintf(m->profile,sizeof(m->profile),"%s",selected);
	return 0;
}
/* Output mode for files that may be software decoded. Only 1080p modes at
 * 24/25/30 Hz are used: on the validated setup 4K GPU-rendered output flashed
 * and 1080p50 corrupted picture and audio, while these low-rate modes were clean. */
static const char *output_mode(double fps)
{
	if (!(fps > 1.0) || fps > 1000.0) return "1920x1080@30";
	while (fps > 31.0) fps /= 2.0;
	while (fps < 20.0) fps *= 2.0;
	if (fabs(fps - 23.976) < 0.02) return "1920x1080@23.98";
	if (fabs(fps - 24.0) < 0.3) return "1920x1080@24";
	if (fabs(fps - 25.0) < 0.3) return "1920x1080@25";
	if (fabs(fps - 29.97) < 0.02) return "1920x1080@29.97";
	return "1920x1080@30";
}
/* Frame rate of the first real video track: -1 without one, 0 if unknown. */
static double video_fps(struct media *m)
{
	struct json_object *tracks = get(m,"track-list"), *v;
	double fps = -1;
	if (tracks && json_object_is_type(tracks,json_type_array)) {
		for (size_t i = 0; i < json_object_array_length(tracks) && fps < 0; ++i) {
			struct json_object *t = json_object_array_get_idx(tracks,i);
			const char *type = strarg(t,"type");
			if (!type || strcmp(type,"video")) continue;
			if ((json_object_object_get_ex(t,"image",&v) && json_object_get_boolean(v)) ||
			    (json_object_object_get_ex(t,"albumart",&v) && json_object_get_boolean(v))) continue;
			fps = json_object_object_get_ex(t,"demux-fps",&v) ? json_object_get_double(v) : 0;
		}
	}
	if (tracks) json_object_put(tracks);
	return fps;
}
static int wait_true(struct media *m, const char *property, int ms)
{
	long long deadline = now_ms() + ms;
	while (now_ms() < deadline && !m->error[0]) {
		struct json_object *v = get(m,property);
		int yes = v && json_object_get_boolean(v);
		if (v) json_object_put(v);
		if (yes) return 1;
		struct timespec delay = {0, 50000000};
		nanosleep(&delay,NULL);
	}
	return 0;
}
/* Reopen the display of the loaded, paused file in another mode. The mode is
 * only read when the video output opens; changing "wid" (unused by DRM) makes
 * the player close and reopen it without reloading the file. */
static int reopen_output(struct media *m, const char *mode)
{
	struct json_object *wid = get(m,"wid");
	long long next = wid && json_object_get_int64(wid) == 0 ? 1 : 0;
	if (wid) json_object_put(wid);
	if (set(m,"drm-mode",json_object_new_string(mode)) ||
	    set(m,"wid",json_object_new_int64(next))) return 0;
	return wait_true(m,"vo-configured",1500);
}
static int number_property(struct media *m, const char *name, double *value)
{
	struct json_object *v = get(m,name);
	int ok = v && (json_object_is_type(v,json_type_int) || json_object_is_type(v,json_type_double));
	if (ok) *value = json_object_get_double(v);
	if (v) json_object_put(v);
	return ok;
}
/* Compare the mode the display really has open with "WxH@R" (a 23.98 Hz
 * mode reports 23.976, so the tolerance must still separate it from 24 Hz). The drm-mode
 * option only says what was requested: an output left open by the previous
 * file keeps its old mode, which once put a 4K film on a 1080p output. */
static int output_matches(struct media *m, const char *mode)
{
	int w = 0, h = 0;
	double hz = 0, dw, dh, dhz;
	if (sscanf(mode,"%dx%d@%lf",&w,&h,&hz) != 3) return 0;
	return number_property(m,"display-width",&dw) && number_property(m,"display-height",&dh) &&
		number_property(m,"display-fps",&dhz) &&
		(int)dw == w && (int)dh == h && fabs(dhz - hz) < 0.012;
}
/* Wait for the display of the loaded, paused file and make it use this mode. */
static int ensure_output(struct media *m, const char *mode)
{
	if (wait_true(m,"vo-configured",1500) && output_matches(m,mode)) return 1;
	if (m->error[0]) return 0;
	return reopen_output(m,mode) && output_matches(m,mode);
}
static struct json_object *play_file(struct media *m, const char *path, int match_rate);
static struct json_object *play(struct media *m, const char *path)
{
	return play_file(m,path,1);
}
static struct json_object *play_file(struct media *m, const char *path, int match_rate)
{
	int fd = localopen(m,path,0);
	char fdpath[80];
	if (fd < 0) return result("local media unavailable or forbidden");
	if (profile(m,m->profile)) { close(fd); return result("player unavailable or profile unsupported"); }
	int required_hw = !strcmp(m->profile,"quality4k");
	/* The display mode is fixed when the video output opens, so choose it first. */
	if (set(m,"drm-mode",json_object_new_string(required_hw ? "3840x2160@30" :
	    match_rate ? m->soft_mode : "1920x1080@30"))) {
		close(fd); return result("player unavailable or profile unsupported");
	}
	snprintf(fdpath,sizeof(fdpath),"/proc/%ld/fd/%d",(long)getpid(),fd);
	struct json_object *a = cmd("loadfile"), *opts = json_object_new_object(), *reply;
	json_object_array_add(a,json_object_new_string(fdpath));
	json_object_array_add(a,json_object_new_string("replace"));
	json_object_array_add(a,json_object_new_int(-1));
	addstr(opts,"demuxer","lavf");
	addstr(opts,"demuxer-lavf-format",format(path));
	addstr(opts,"access-references","no");
	addstr(opts,"pause","yes");
	json_object_array_add(a,opts);
	m->file_loaded=0;
	m->error[0]=0;
	reply = ipc(m,a);
	if (!reply) { close(fd); return result("player rejected load or unavailable"); }
	json_object_put(reply);
	if (m->mediafd >= 0) close(m->mediafd);
	m->mediafd = fd;
	snprintf(m->title,sizeof(m->title),"%s",strrchr(path,'/') ? strrchr(path,'/')+1 : path);
	/* Loading is asynchronous; quality4k remains paused until a hardware
	 * decoder is reported for this exact opened file, or fails closed. */
	if (!strcmp(m->profile,"quality4k")) {
		long long deadline = now_ms() + 5500;
		int output_checked = 0;
		while (now_ms() < deadline) {
			struct json_object *loaded = get(m,"path");
			struct json_object *hw = get(m,"hwdec-current");
			int active = m->file_loaded && loaded && !strcmp(json_object_get_string(loaded),fdpath) &&
				hw && json_object_is_type(hw,json_type_string) &&
				*json_object_get_string(hw) && strcmp(json_object_get_string(hw),"no");
			if (loaded) json_object_put(loaded);
			if (hw) json_object_put(hw);
			if (active && !m->error[0]) {
				if (!output_checked) {
					/* May reopen the output; verify the decoder again afterwards. */
					output_checked = 1;
					if (!ensure_output(m,"3840x2160@30")) {
						if (!m->error[0]) snprintf(m->error,sizeof(m->error),"display output mode unavailable");
						break;
					}
					continue;
				}
				if (set(m,"pause",json_object_new_boolean(0))) goto hwfail;
				return result(m->error[0] ? m->error : NULL);
			}
			if (m->error[0]) break;
			struct timespec delay = {0, 50000000};
			nanosleep(&delay,NULL);
		}
hwfail:
		if (m->error[0]) {
			char reason[160];
			snprintf(reason,sizeof(reason),"quality4k initialization failed: %.100s",m->error);
			stop_reason(m,reason);
		} else stop_reason(m,"quality4k requires an active hardware decoder");
		return result(m->error);
	}
	/* Loading is asynchronous: unpausing before the file is loaded can leave it
	 * paused at the start, because the load applies its own pause option. */
	long long deadline = now_ms() + 2500;
	int loaded_ok = 0;
	while (now_ms() < deadline && !m->error[0] && !loaded_ok) {
		struct json_object *loaded = get(m,"path");
		loaded_ok = m->file_loaded && loaded && !strcmp(json_object_get_string(loaded),fdpath);
		if (loaded) json_object_put(loaded);
		if (!loaded_ok) { struct timespec delay = {0, 50000000}; nanosleep(&delay,NULL); }
	}
	if (!loaded_ok || m->error[0]) {
		if (!m->error[0]) stop_reason(m,"player did not load media");
		else { char reason[160]; snprintf(reason,sizeof(reason),"%.150s",m->error); stop_reason(m,reason); }
		return result(m->error);
	}
	double fps = video_fps(m);
	if (fps >= 0) {
		const char *mode = match_rate ? output_mode(fps) : "1920x1080@30";
		if (!ensure_output(m,mode)) {
			/* The display may not offer the matched mode: retry once at 30 Hz. */
			if (match_rate && strcmp(mode,"1920x1080@30")) {
				simple(m,"stop");
				m->error[0]=0;
				m->deadline = now_ms() + 6500;
				return play_file(m,path,0);
			}
			if (!m->error[0]) stop_reason(m,"display output mode unavailable");
			else { char reason[160]; snprintf(reason,sizeof(reason),"%.150s",m->error); stop_reason(m,reason); }
			return result(m->error);
		}
		if (match_rate) snprintf(m->soft_mode,sizeof(m->soft_mode),"%s",mode);
	}
	if (set(m,"pause",json_object_new_boolean(0))) return result("player pause reset failed");
	return result(m->error[0] ? m->error : NULL);
}
static struct json_object *status(struct media *m)
{
	struct json_object *idle = get(m,"idle-active");
	struct json_object *r = result(NULL), *caps = json_object_new_object();
	int available = idle != NULL;
	int is_idle = idle && json_object_get_boolean(idle);
	json_object_object_add(r,"available",json_object_new_boolean(available));
	addstr(r,"state",available ? (json_object_get_boolean(idle) ? "idle" : "playing") : "unavailable");
	if (idle) json_object_put(idle);
	const struct { const char *api, *property; } fields[] = {
		{"time_pos","time-pos"},{"duration","duration"},{"volume","volume"},
		{"mute","mute"},{"track_list","track-list"},{"codec","video-codec"},
		{"width","width"},{"height","height"},{"hwdec","hwdec-current"},
		{"dropped_frames","decoder-frame-drop-count"},{"vo_dropped_frames","frame-drop-count"},{"avsync","avsync"}
	};
	for (size_t i=0;i<sizeof(fields)/sizeof(fields[0]);++i)
		json_object_object_add(r,fields[i].api,available ? get(m,fields[i].property) : NULL);
	if (available) {
		struct json_object *pause = get(m,"pause");
		if (!is_idle && pause && json_object_get_boolean(pause)) addstr(r,"state","paused");
		struct json_object *hw = NULL;
		json_object_object_get_ex(r,"hwdec",&hw);
		if (!is_idle && !strcmp(m->profile,"quality4k") &&
		    (!hw || !json_object_is_type(hw,json_type_string) || !strcmp(json_object_get_string(hw),"no") ||
		     !*json_object_get_string(hw))) {
			int failed=stop_reason(m,"required hardware decoder lost");
			addstr(r,"state",failed ? "unavailable" : "idle");
			if (failed) {
				available=0;
				json_object_object_add(r,"available",json_object_new_boolean(0));
			}
		}
		if (pause) json_object_put(pause);
	}
	if (available && m->ipcfd<0) {
		available=0;
		addstr(r,"state","unavailable");
		json_object_object_add(r,"available",json_object_new_boolean(0));
	}
	addstr(r,"title",m->title);
	addstr(r,"profile",m->profile);
	json_object_object_add(r,"error",!available ? json_object_new_string("private player IPC unavailable") :
		(m->error[0] ? json_object_new_string(m->error) : NULL));
	const char *controls[] = {"browse","play","pause","stop","seek","volume","tracks","queue","profile"};
	for (size_t i=0;i<sizeof(controls)/sizeof(controls[0]);++i)
		json_object_object_add(caps,controls[i],json_object_new_boolean(i == 0 || available));
	json_object_object_add(caps,"automatic_queue_advance",json_object_new_boolean(0));
	json_object_object_add(caps,"mute",json_object_new_boolean(available));
	json_object_object_add(caps,"hardware_validation",json_object_new_boolean(0));
	struct json_object *storage=media_files_storage(m), *writable=NULL;
	json_object_object_get_ex(storage,"writable",&writable);
	int writes=writable && json_object_get_boolean(writable);
	json_object_object_add(caps,"storage",json_object_new_boolean(1));
	json_object_object_add(caps,"remove",json_object_new_boolean(writes));
	json_object_object_add(caps,"upload_begin",json_object_new_boolean(writes));
	json_object_object_add(caps,"upload_cancel",json_object_new_boolean(1));
	json_object_put(storage);
	json_object_object_add(r,"capabilities",caps);
	json_object_object_add(r,"media_root_available",json_object_new_boolean(m->rootfd >= 0));
	json_object_object_add(r,"verified_4k",json_object_new_boolean(0));
	return r;
}
void media_guard(struct media *m)
{
	media_files_expire(m,media_files_now());
	/* Drain at most one bounded buffer per tick, even when idle. */
	if (m->ipcfd>=0) {
		frames(m,0);
		struct pollfd p={.fd=m->ipcfd,.events=POLLIN};
		if (poll(&p,1,0)>0 && (p.revents & (POLLIN|POLLHUP|POLLERR))) {
			ssize_t n=read(m->ipcfd,m->readbuf+m->readlen,RESPONSE_MAX-m->readlen);
			if (n>0) { m->readlen+=(size_t)n; frames(m,0); }
			else if (!n || (errno!=EAGAIN && errno!=EINTR)) disconnect(m);
			if (m->readlen==RESPONSE_MAX) disconnect(m);
		}
	}
	if (strcmp(m->profile,"quality4k") || m->mediafd < 0) return;
	m->deadline = now_ms() + 1800;
	struct json_object *idle=get(m,"idle-active"), *hw=get(m,"hwdec-current");
	if (!idle || (!json_object_get_boolean(idle) &&
	    (!hw || !json_object_is_type(hw,json_type_string) ||
	     !*json_object_get_string(hw) || !strcmp(json_object_get_string(hw),"no")))) {
		stop_reason(m,"required hardware decoder or IPC lost");
	}
	if (idle) json_object_put(idle);
	if (hw) json_object_put(hw);
}
struct json_object *media_call(struct media *m, const char *method, struct json_object *args)
{
	m->deadline = now_ms() + (!strcmp(method ? method : "", "play") ||
		!strcmp(method ? method : "", "queue") ? 6500 : 3000);
	if (!method || strlen(method)>32 || (args && !json_object_is_type(args,json_type_object)) ||
	    (args && strlen(json_object_to_json_string_ext(args,JSON_C_TO_STRING_PLAIN))>INPUT_MAX))
		return result("invalid or oversized request");
	struct json_object *file_reply=media_files_call(m,method,args);
	if (file_reply) return file_reply;
	if (!strcmp(method,"browse")) return browse(m,args);
	if (!strcmp(method,"status")) return status(m);
	if (!strcmp(method,"play")) return play(m,strarg(args,"path"));
	if (!strcmp(method,"stop")) {
		if (simple(m,"stop")) return result("player unavailable");
		if (m->mediafd>=0) close(m->mediafd);
		m->mediafd=-1; m->title[0]=m->error[0]=0;
		return result(NULL);
	}
	if (!strcmp(method,"pause")) {
		int paused;
		if (boolarg(args,"paused",&paused)) return result("paused must be boolean");
		if (!paused && !strcmp(m->profile,"quality4k")) {
			struct json_object *hw=get(m,"hwdec-current");
			int active=m->file_loaded && hw && json_object_is_type(hw,json_type_string) &&
				*json_object_get_string(hw) && strcmp(json_object_get_string(hw),"no");
			if (hw) json_object_put(hw);
			if (!active || m->error[0]) return result("required hardware decoder not verified; cannot unpause");
		}
		return result(set(m,"pause",json_object_new_boolean(paused)) ? "player rejected pause" : NULL);
	}
	if (!strcmp(method,"seek")) {
		double seconds; int relative;
		if (numarg(args,"seconds",&seconds) || fabs(seconds)>86400*365 ||
		    boolarg(args,"relative",&relative) || (!relative && seconds<0))
			return result("invalid seek");
		struct json_object *a=cmd("seek"), *r;
		json_object_array_add(a,json_object_new_double(seconds));
		json_object_array_add(a,json_object_new_string(relative ? "relative+exact" : "absolute+exact"));
		r=ipc(m,a);
		if (!r) return result("player rejected seek");
		json_object_put(r); return result(NULL);
	}
	if (!strcmp(method,"volume")) {
		double value; int mute; struct json_object *v;
		if (numarg(args,"value",&value) || value<0 || value>100) return result("volume must be 0..100");
		if (json_object_object_get_ex(args,"mute",&v) && boolarg(args,"mute",&mute))
			return result("mute must be boolean");
		if (set(m,"volume",json_object_new_double(value))) return result("player rejected volume");
		if (json_object_object_get_ex(args,"mute",&v) &&
		    set(m,"mute",json_object_new_boolean(json_object_get_boolean(v)))) return result("player rejected mute");
		return result(NULL);
	}
	if (!strcmp(method,"tracks")) {
		const char *type=strarg(args,"type"); double id;
		if (!type || (strcmp(type,"audio") && strcmp(type,"sub")) ||
		    numarg(args,"id",&id) || floor(id)!=id || id<0 || id>65535) return result("invalid track selection");
		return result(set(m,!strcmp(type,"audio") ? "aid" : "sid",
			id ? json_object_new_int((int)id) : json_object_new_string("no")) ?
			"player rejected track selection" : NULL);
	}
	if (!strcmp(method,"profile")) {
		int rc=profile(m,strarg(args,"name"));
		if (rc) return result(rc == -2 ? "unknown profile" : "player unavailable or profile unsupported");
		/* Changing to required HW while already playing is rejected closed. */
		if (!strcmp(m->profile,"quality4k")) {
			if (simple(m,"stop")) return result("player failed to stop for required-hardware profile");
			if (m->mediafd >= 0) close(m->mediafd);
			m->mediafd=-1;
			m->title[0]=0;
		}
		return result(NULL);
	}
	if (!strcmp(method,"queue")) {
		const char *action=strarg(args,"action");
		if (!action) return result("queue action required");
		if (!strcmp(action,"append")) {
			const char *path=strarg(args,"path");
			int fd=localopen(m,path,0);
			if (fd<0) return result("queue media unavailable or forbidden");
			close(fd);
			if (m->queue_len==128) return result("queue full");
			size_t bytes=strlen(path);
			for (size_t i=0;i<m->queue_len;++i) bytes+=strlen(m->queue[i]);
			if (bytes>65536) return result("queue path storage full");
			snprintf(m->queue[m->queue_len++],1024,"%s",path);
		} else if (!strcmp(action,"clear")) m->queue_len=0;
		else if (!strcmp(action,"next")) {
			if (!m->queue_len) return result("queue empty");
			struct json_object *r=play(m,m->queue[0]), *ok;
			if (json_object_object_get_ex(r,"ok",&ok) && json_object_get_boolean(ok)) {
				--m->queue_len;
				memmove(m->queue,m->queue+1,m->queue_len*sizeof(m->queue[0]));
			}
			return r;
		} else if (strcmp(action,"list")) return result("unknown queue action");
		struct json_object *r=result(NULL), *entries=json_object_new_array();
		for (size_t i=0;i<m->queue_len;++i) json_object_array_add(entries,json_object_new_string(m->queue[i]));
		json_object_object_add(r,"entries",entries); return r;
	}
	return result("unknown method");
}
