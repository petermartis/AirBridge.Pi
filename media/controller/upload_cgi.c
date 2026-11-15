#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "upload.h"
#include "held_fd.h"
#include "upload_io.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int success(struct json_object *r)
{
	struct json_object *v;
	return r && json_object_object_get_ex(r,"ok",&v) &&
		json_object_is_type(v,json_type_boolean) && json_object_get_boolean(v);
}
static struct json_object *arguments(const struct upload_auth *auth)
{
	struct json_object *a=json_object_new_object();
	json_object_object_add(a,"session",json_object_new_string(auth->session));
	json_object_object_add(a,"token",json_object_new_string(auth->token));
	json_object_object_add(a,"offset",json_object_new_int64((int64_t)auth->offset));
	return a;
}
static void respond(int status, struct json_object *r)
{
	const char *reason="Internal Server Error";
	switch (status) {
	case 200: reason="OK"; break;
	case 400: reason="Bad Request"; break;
	case 401: reason="Unauthorized"; break;
	case 403: reason="Forbidden"; break;
	case 405: reason="Method Not Allowed"; break;
	case 409: reason="Conflict"; break;
	case 413: reason="Payload Too Large"; break;
	case 415: reason="Unsupported Media Type"; break;
	case 503: reason="Service Unavailable"; break;
	case 507: reason="Insufficient Storage"; break;
	}
	const char *text=json_object_to_json_string_ext(r,JSON_C_TO_STRING_PLAIN);
	printf("Status: %d %s\r\nContent-Type: application/json\r\nCache-Control: no-store\r\n"
	       "X-Content-Type-Options: nosniff\r\nContent-Length: %zu\r\n\r\n%s",
	       status,reason,strlen(text),text);
	fflush(stdout);
}
static int fail(int status, const char *error)
{
	struct json_object *r=upload_result(error);
	respond(status,r);
	json_object_put(r);
	return 0;
}
int main(void)
{
	umask(077);
	signal(SIGPIPE,SIG_IGN);
	signal(SIGXFSZ,SIG_IGN);
	signal(SIGALRM,SIG_DFL);
	alarm(60);
	errno=0;
	int priority=getpriority(PRIO_PROCESS,0);
	if ((!errno || priority!=-1) && priority<10) (void)setpriority(PRIO_PROCESS,0,10);
	long long deadline=upload_now_ms()+58000;
	const char *method=getenv("REQUEST_METHOD"), *type=getenv("CONTENT_TYPE");
	const char *query=getenv("QUERY_STRING"), *extra=getenv("PATH_INFO");
	uint64_t length;
	struct upload_auth auth;
	if (!method || strcmp(method,"POST")) return fail(405,"POST required");
	if ((query && *query) || (extra && *extra)) return fail(400,"query strings and path suffixes are not supported");
	if (!type || strcmp(type,"application/octet-stream")) return fail(415,"application/octet-stream required");
	if (upload_decimal(getenv("CONTENT_LENGTH"),&length) || !length || length>UPLOAD_CHUNK)
		return fail(413,"a nonempty chunk of at most 1048576 bytes is required");
	if (upload_authorization(getenv("HTTP_AUTHORIZATION"),&auth))
		return fail(401,"valid media upload authorization required");
	/* Authorization, capability ownership and opened-FD identity are checked
	 * before even one byte is read from CGI stdin. No generic file paths. */
	if (!upload_acl(auth.session,"upload_begin")) return fail(403,"media upload permission denied");
	struct json_object *args=arguments(&auth);
	json_object_object_add(args,"length",json_object_new_int64((int64_t)length));
	struct json_object *claim=upload_rpc("airbridge.media","_upload_claim",args);
	if (!success(claim)) {
		const char *error=upload_string(claim,"error");
		int rc=fail(claim ? 409 : 503,error ? error : "media controller unavailable");
		if (claim) json_object_put(claim);
		json_object_put(args);
		return rc;
	}
	const char *lease=upload_string(claim,"lease");
	uint64_t device,inode,parent_inode,stage_inode,total,offset,claimed;
	if (!upload_hex(lease,32) || upload_number(claim,"device",&device) ||
	    upload_number(claim,"inode",&inode) || upload_number(claim,"parent_inode",&parent_inode) ||
	    upload_number(claim,"stage_inode",&stage_inode) ||
	    upload_number(claim,"size",&total) || upload_number(claim,"offset",&offset) ||
	    upload_number(claim,"length",&claimed) || offset!=auth.offset || claimed!=length ||
	    total<offset || length>total-offset) {
		json_object_put(claim); json_object_put(args);
		return fail(503,"invalid private upload lease");
	}
	json_object_object_add(args,"lease",json_object_new_string(lease));
	int fd=upload_held_open(upload_string(claim,"fd_path"),0);
	int parent=upload_held_open(upload_string(claim,"parent_fd_path"),1);
	int stage=upload_held_open(upload_string(claim,"stage_fd_path"),1);
	struct stat st, directory, staging;
	int valid=fd>=0 && parent>=0 && stage>=0 && !fstat(fd,&st) &&
		!fstat(parent,&directory) && !fstat(stage,&staging) &&
		S_ISREG(st.st_mode) && S_ISDIR(directory.st_mode) && st.st_nlink==1 &&
		st.st_uid==geteuid() && (st.st_mode&0777)==0600 &&
		S_ISDIR(staging.st_mode) && staging.st_uid==geteuid() && (staging.st_mode&0777)==0700 &&
		(uint64_t)st.st_dev==device && (uint64_t)st.st_ino==inode &&
		(uint64_t)directory.st_dev==device && (uint64_t)directory.st_ino==parent_inode &&
		(uint64_t)staging.st_dev==device && (uint64_t)staging.st_ino==stage_inode &&
		(uint64_t)st.st_size==offset && !flock(fd,LOCK_EX|LOCK_NB);
	struct json_object *checked=valid ? upload_rpc("airbridge.media","_upload_check",args) : NULL;
	valid=valid && success(checked);
	if (checked) json_object_put(checked);
	int final=offset+length==total;
	const char *error=NULL;
	if (!valid) error="upload canceled or held file identity/lock check failed";
	else if (upload_stream_chunk(STDIN_FILENO,fd,offset,length,deadline)) error="chunk disconnected, timed out or could not be written";
	else if (final && fsync(fd)) error="final media synchronization failed";
	if (fd>=0) close(fd);
	struct json_object *reply=NULL;
	if (!error && !upload_acl(auth.session,"upload_begin")) error="media upload permission expired";
	if (!error) {
		reply=upload_rpc("airbridge.media","_upload_done",args);
		if (!success(reply)) {
			const char *why=upload_string(reply,"error");
			error=why ? why : "media controller completion unavailable";
		}
	}
	if (error) {
		struct json_object *aborted=upload_rpc("airbridge.media","_upload_abort",args);
		if (aborted) json_object_put(aborted);
		fail(409,error);
	} else if (final && (fsync(parent) || fsync(stage))) {
		json_object_object_add(reply,"ok",json_object_new_boolean(0));
		json_object_object_add(reply,"error",json_object_new_string("file published but media directory synchronization failed"));
		respond(507,reply);
	} else respond(200,reply);
	if (parent>=0) close(parent);
	if (stage>=0) close(stage);
	if (reply) json_object_put(reply);
	json_object_put(claim);
	json_object_put(args);
	return 0;
}
