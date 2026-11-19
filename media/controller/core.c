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
		{"m4a","mov"},{"mov","mov"},{"avi","avi"},{"ts","mpegts"},{"m2ts","mpegts"},
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
	snprintf(m->hw_mode, sizeof(m->hw_mode), "3840x2160@30");
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
static struct json_object *cmd(const char *name)
{
	struct json_object *a = json_object_new_array();
	json_object_array_add(a, json_object_new_string(name));
	return a;
}
/* Write one command to the private player connection (opening it if needed).
 * Bounded nonblocking write loop, framing newline included. Returns 0 on
 * failure and disconnects, so a later call starts fresh. */
static int ipc_write(struct media *m, struct json_object *command, unsigned id, long long deadline)
{
	struct sockaddr_un addr;
	struct json_object *request = json_object_new_object();
	size_t sent = 0;
	int ok = 0;
	json_object_object_add(request, "command", command);
	json_object_object_add(request, "request_id", json_object_new_int64(id));
	const char *wire = json_object_to_json_string_ext(request, JSON_C_TO_STRING_PLAIN);
	size_t len = strlen(wire);
	if (len + 1 <= INPUT_MAX + 1) {
		if (m->ipcfd<0) {
			m->ipcfd = socket(AF_UNIX, SOCK_STREAM, 0);
			if (m->ipcfd<0 || fcntl(m->ipcfd,F_SETFL,O_NONBLOCK)<0) goto out;
			memset(&addr, 0, sizeof(addr));
			addr.sun_family = AF_UNIX;
			snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", m->socket_path);
			if (connect(m->ipcfd,(struct sockaddr *)&addr,sizeof(addr))<0 && errno!=EINPROGRESS)
				goto out;
		}
		char output[INPUT_MAX + 1];
		memcpy(output, wire, len);
		output[len++] = '\n';
		while (sent < len) {
			if (!ready(m->ipcfd, POLLOUT, deadline)) goto out;
#ifdef MSG_NOSIGNAL
			ssize_t n = send(m->ipcfd, output + sent, len - sent, MSG_NOSIGNAL);
#else
			ssize_t n = send(m->ipcfd, output + sent, len - sent, 0);
#endif
			if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
			if (n <= 0) goto out;
			sent += (size_t)n;
		}
		ok = 1;
	}
out:
	if (!ok) disconnect(m);
	json_object_put(request);
	return ok;
}
/* Send one command and wait for its reply. */
static struct json_object *ipc(struct media *m, struct json_object *command)
{
	struct json_object *reply = NULL;
	unsigned id = ++m->request_id;
	long long deadline = now_ms() + TIMEOUT_MS;
	if (m->deadline && deadline > m->deadline) deadline = m->deadline;
	if (!ipc_write(m, command, id, deadline)) return NULL;
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
	return reply;
}
/* Issue several get_property requests in one exchange: write all commands,
 * then harvest the replies. The player answers in order, but by the time an
 * early reply is matched, replies for later commands are often already
 * buffered; every reply whose id belongs to this batch must be kept (frames()
 * drops non-matching ids, which would lose the rest of the snapshot). A hung
 * IPC costs one timeout, not one per property, and status stays under the
 * call deadline. Returns an object with one entry per successfully answered
 * property; missing entries mean "unavailable", as with a failed get(). */
static struct json_object *get_many(struct media *m, const char *const *names, size_t count)
{
	unsigned ids[16];
	int have[16] = {0};
	long long deadline = now_ms() + TIMEOUT_MS;
	size_t written = 0, collected = 0;
	struct json_object *out;
	if (count > 16) return NULL;
	if (m->deadline && deadline > m->deadline) deadline = m->deadline;
	out = json_object_new_object();
	for (size_t i = 0; i < count; ++i) {
		struct json_object *a = cmd("get_property");
		json_object_array_add(a, json_object_new_string(names[i]));
		ids[i] = ++m->request_id;
		/* ipc_write takes ownership of the command object. */
		if (ipc_write(m, a, ids[i], deadline)) ++written;
	}
	while (collected < written) {
		char *end;
		while ((end = memchr(m->readbuf, '\n', m->readlen))) {
			*end = 0;
			struct json_object *r = json_tokener_parse(m->readbuf), *rid = NULL;
			if (r) {
				event(m, r);
				if (json_object_object_get_ex(r, "request_id", &rid)) {
					int64_t want = json_object_get_int64(rid);
					for (size_t i = 0; i < written; ++i) {
						if (ids[i] == (unsigned)want && !have[i]) {
							struct json_object *err, *data = NULL;
							int good = json_object_object_get_ex(r, "error", &err) &&
								json_object_is_type(err, json_type_string) &&
								!strcmp(json_object_get_string(err), "success");
							if (good && json_object_object_get_ex(r, "data", &data))
								json_object_get(data);
							json_object_object_add(out, names[i], data);
							have[i] = 1;
							++collected;
							break;
						}
					}
				}
				json_object_put(r);
			}
			size_t consumed = (size_t)(end - m->readbuf) + 1;
			m->readlen -= consumed;
			memmove(m->readbuf, m->readbuf + consumed, m->readlen);
		}
		if (collected >= written) break;
		if (m->readlen >= RESPONSE_MAX || !ready(m->ipcfd, POLLIN, deadline)) break;
		ssize_t n = read(m->ipcfd, m->readbuf + m->readlen, RESPONSE_MAX - m->readlen);
		if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
		if (n <= 0) break;
		m->readlen += (size_t)n;
	}
	if (collected < written) disconnect(m);
	return out;
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
	if (set(m,"hwdec",json_object_new_string(!strcmp(name,"quality4k") ? "auto-safe" : "no")) ||
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
/* Output mode for the loaded file at the given resolution.
 *
 * 1080p output runs at the content's native rate, verified live 2026-11-18
 * on the LG C4 (OLED42C44LA): 23.976, 24.0 and 25 Hz lock and play smoothly
 * (zero frame drops) through the normal mpv DRM output; earlier reports that
 * sub-30 modes would not lock were taken while the TV's HDMI input state was
 * wedged by invalid-signal probing and are retracted. The TMDS clock stays
 * in the clean ~74 MHz band for all of these modes. 1080p50/60 (148.5 MHz)
 * remains unusable on this CM5->C4 link: the VC6 PHY driver programs the
 * sub-222 MHz band with term_ctl=0 and the internal current source off, and
 * the C4 will not lock the resulting 148.5 MHz drive (an Apple TV locks the
 * same TV/cable fine, so it is CM5 TX output quality, not the TV). 4K stays
 * at 30 Hz. */
static const char *output_mode(double fps, int fourk)
{
	if (!fourk) {
		if (fps > 23.7 && fps < 23.99) return "1920x1080@23.976";
		if (fps >= 23.99 && fps < 24.3) return "1920x1080@24";
		if (fps >= 24.6 && fps < 25.5) return "1920x1080@25";
		return "1920x1080@30";
	}
	return "3840x2160@30";
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
/* Probe the opened file for its native video dimensions and frame rate through
 * the already-open descriptor (no path on the command line). Returns 1 on
 * success with fps, width and height set, 0 otherwise so the caller can fall
 * back to the profile's default mode. */
static int probe_native(int fd, double *fps, int *width, int *height)
{
	char cmd[256], path[64], buf[16384];
	FILE *p;
	struct json_object *root, *streams, *s, *v;
	int ok = 0;
	*fps = 0; *width = 0; *height = 0;
	snprintf(path, sizeof(path), "/proc/%ld/fd/%d", (long)getpid(), fd);
	snprintf(cmd, sizeof(cmd),
	         "/usr/lib/airbridge-media/bin/ffprobe -v error -select_streams v:0 "
	         "-show_entries stream=width,height,r_frame_rate -of json '%s' 2>/dev/null",
	         path);
	p = popen(cmd, "r");
	if (!p) return 0;
	size_t n = fread(buf, 1, sizeof(buf) - 1, p);
	int rc = pclose(p);
	buf[n] = 0;
	if (rc != 0) return 0;
	root = json_tokener_parse(buf);
	if (!root) return 0;
	if (json_object_object_get_ex(root, "streams", &streams) &&
	    json_object_is_type(streams, json_type_array) &&
	    json_object_array_length(streams) > 0) {
		s = json_object_array_get_idx(streams, 0);
		if (json_object_object_get_ex(s, "width", &v) &&
		    json_object_is_type(v, json_type_int)) *width = json_object_get_int(v);
		if (json_object_object_get_ex(s, "height", &v) &&
		    json_object_is_type(v, json_type_int)) *height = json_object_get_int(v);
		if (json_object_object_get_ex(s, "r_frame_rate", &v) &&
		    json_object_is_type(v, json_type_string)) {
			const char *rate = json_object_get_string(v);
			int num, den;
			if (sscanf(rate, "%d/%d", &num, &den) == 2 && den > 0)
				*fps = (double)num / den;
			else if (sscanf(rate, "%lf", fps) == 1 && *fps <= 0) *fps = 0;
		}
		ok = (*fps > 0 && *width > 0 && *height > 0) ? 1 : 0;
	}
	json_object_put(root);
	return ok;
}
/* Restart the player so it opens the display at "mode" from scratch. A live
 * reopen of the DRM output (changing the mode on a running player) drops the
 * HDMI link on the V3D/VC4 driver; a fresh start at the target mode is clean.
 * Updates the player config's drm-mode and respawns the player, then waits for
 * it to answer at the requested mode. Returns 1 on success. */
static int restart_at_mode(struct media *m, const char *mode)
{
	char cmd[160];
	snprintf(cmd, sizeof(cmd),
	         "sed -i 's/^drm-mode=.*/drm-mode=%s/' /etc/airbridge-media/mpv.conf 2>/dev/null",
	         mode);
	if (system(cmd) != 0) return 0;
	if (simple(m, "quit")) return 0;
	disconnect(m);
	long long deadline = now_ms() + 12000;
	while (now_ms() < deadline) {
		struct json_object *v = get(m, "drm-mode");
		int ok = v && json_object_is_type(v, json_type_string) &&
		         !strcmp(json_object_get_string(v), mode);
		if (v) json_object_put(v);
		if (ok) return 1;
		struct timespec delay = {0, 250000000};
		nanosleep(&delay, NULL);
	}
	return 0;
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
	/* Pick the profile from the file's native resolution when the user has not
	 * pinned one: 4K source → quality4k (hardware decode), everything else →
	 * balanced (software decode). */
	double fps = 0;
	int width = 0, height = 0;
	if (!m->profile_forced && probe_native(fd,&fps,&width,&height))
		snprintf(m->profile,sizeof(m->profile),"%s",
		         (width >= 3840 || height >= 2000) ? "quality4k" : "balanced");
	if (profile(m,m->profile)) { close(fd); return result("player unavailable or profile unsupported"); }
	int required_hw = !strcmp(m->profile,"quality4k");
	/* Choose the display mode before the video output opens: the content's
	 * native rate at the profile's resolution (4K for quality4k, 1080p for the
	 * software path). */
	const char *mode = fps > 0 ? output_mode(fps,required_hw) :
		(required_hw ? "3840x2160@30" : "1920x1080@30");
	if (required_hw) snprintf(m->hw_mode,sizeof(m->hw_mode),"%s",mode);
	else if (match_rate) snprintf(m->soft_mode,sizeof(m->soft_mode),"%s",mode);
	/* A player that already has the display open keeps its old mode across a
	 * "replace" load, and changing that mode live (the old reopen path) drops
	 * the HDMI link on the V3D/VC4 driver. When the open mode differs from the
	 * target, restart the player so the output opens fresh at the target mode.
	 * An idle player has no output open, so its load already opens fresh. */
	struct json_object *idle = get(m,"idle-active");
	int is_idle = idle && json_object_get_boolean(idle);
	if (idle) json_object_put(idle);
	if (!is_idle && !output_matches(m,mode)) {
		simple(m,"stop");
		if (m->mediafd >= 0) { close(m->mediafd); m->mediafd=-1; }
		if (!restart_at_mode(m,mode)) { close(fd); return result("display mode change failed"); }
		if (profile(m,m->profile)) { close(fd); return result("player unavailable or profile unsupported"); }
	}
	/* The display mode is fixed when the video output opens, so request it
	 * before the load. */
	if (set(m,"drm-mode",json_object_new_string(mode)) ||
	    set(m,"drm-draw-plane",json_object_new_string(required_hw ? "overlay" : "primary"))) {
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
	/* The output opens at the target mode; verify it before releasing pause. */
	if (!wait_true(m,"vo-configured",3000) || !output_matches(m,mode)) {
		if (!m->error[0]) stop_reason(m,"display output mode unavailable");
		else { char reason[160]; snprintf(reason,sizeof(reason),"%.150s",m->error); stop_reason(m,reason); }
		return result(m->error);
	}
	/* quality4k remains paused until a hardware decoder is reported for this
	 * exact opened file, or fails closed. */
	if (required_hw) {
		long long deadline = now_ms() + 5500;
		int active = 0;
		while (now_ms() < deadline && !active && !m->error[0]) {
			struct json_object *loaded = get(m,"path");
			struct json_object *hw = get(m,"hwdec-current");
			active = m->file_loaded && loaded && !strcmp(json_object_get_string(loaded),fdpath) &&
				hw && json_object_is_type(hw,json_type_string) &&
				*json_object_get_string(hw) && strcmp(json_object_get_string(hw),"no");
			if (loaded) json_object_put(loaded);
			if (hw) json_object_put(hw);
			if (!active) { struct timespec delay = {0, 50000000}; nanosleep(&delay,NULL); }
		}
		if (!active) {
			char reason[160];
			snprintf(reason,sizeof(reason),"quality4k initialization failed: %.100s",
			         m->error[0] ? m->error : "no hardware decoder");
			stop_reason(m,reason);
			return result(m->error);
		}
	}
	if (set(m,"pause",json_object_new_boolean(0))) return result("player pause reset failed");
	return result(m->error[0] ? m->error : NULL);
}
static struct json_object *status(struct media *m)
{
	/* One exchange for the whole snapshot: a hung IPC costs a single
	 * timeout instead of one per property, and the response stays inside
	 * the call deadline even when the player is slow. */
	static const char *const props[] = {
		"idle-active", "time-pos", "duration", "volume", "mute", "track-list",
		"video-codec", "width", "height", "hwdec-current",
		"decoder-frame-drop-count", "frame-drop-count", "avsync", "pause"
	};
	struct json_object *all = get_many(m, props, 14);
	struct json_object *r = result(NULL), *caps = json_object_new_object();
	struct json_object *idle = NULL, *pause = NULL;
	int available = all && json_object_object_get_ex(all, "idle-active", &idle);
	int is_idle = available && json_object_get_boolean(idle);
	json_object_object_add(r, "available", json_object_new_boolean(available));
	addstr(r, "state", available ? (is_idle ? "idle" : "playing") : "unavailable");
	const struct { const char *api, *property; } fields[] = {
		{"time_pos","time-pos"},{"duration","duration"},{"volume","volume"},
		{"mute","mute"},{"track_list","track-list"},{"codec","video-codec"},
		{"width","width"},{"height","height"},{"hwdec","hwdec-current"},
		{"dropped_frames","decoder-frame-drop-count"},{"vo_dropped_frames","frame-drop-count"},{"avsync","avsync"}
	};
	for (size_t i = 0; i < sizeof(fields)/sizeof(fields[0]); ++i) {
		struct json_object *v = NULL;
		json_object_object_get_ex(all, fields[i].property, &v);
		json_object_object_add(r, fields[i].api, v ? json_object_get(v) : NULL);
	}
	if (available) {
		json_object_object_get_ex(all, "pause", &pause);
		if (!is_idle && pause && json_object_get_boolean(pause)) addstr(r, "state", "paused");
		struct json_object *hw = NULL;
		json_object_object_get_ex(r, "hwdec", &hw);
		if (!is_idle && !strcmp(m->profile, "quality4k") &&
		    (!hw || !json_object_is_type(hw, json_type_string) || !strcmp(json_object_get_string(hw), "no") ||
		     !*json_object_get_string(hw))) {
			int failed=stop_reason(m,"required hardware decoder lost");
			addstr(r,"state",failed ? "unavailable" : "idle");
			if (failed) {
				available=0;
				json_object_object_add(r,"available",json_object_new_boolean(0));
			}
		}
	}
	json_object_put(all);
	if (available && m->ipcfd<0) {
		available=0;
		addstr(r,"state","unavailable");
		json_object_object_add(r,"available",json_object_new_boolean(0));
	}
	addstr(r,"title",m->title);
	addstr(r,"profile",m->profile_forced ? m->profile : "auto");
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
		const char *want = strarg(args,"name");
		/* "auto" releases the pin: each play re-selects from the file. */
		if (want && !strcmp(want,"auto")) { m->profile_forced=0; return result(NULL); }
		int rc=profile(m,want);
		if (rc) return result(rc == -2 ? "unknown profile" : "player unavailable or profile unsupported");
		/* User pinned a profile: auto-select from the file's native format is
		 * off until they stop pinning. */
		m->profile_forced=1;
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
