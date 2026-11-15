#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "upload.h"
#include <libubox/blobmsg_json.h>
#include <libubus.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* libubus lookup_id has no timeout. Isolate lookup/invoke in a bounded worker
 * rather than risk wedging the playback controller event loop indefinitely. */
static void rpc_reply(struct ubus_request *req, int type, struct blob_attr *msg)
{
	(void)type;
	struct json_object **reply=req->priv;
	if (!msg || blob_len(msg)>8192 || *reply) return;
	char *text=blobmsg_format_json(msg,true);
	if (text && strlen(text)<=8192) *reply=json_tokener_parse(text);
	free(text);
}
static struct json_object *direct(const char *object, const char *method, struct json_object *args)
{
	struct ubus_context *ctx=ubus_connect(NULL);
	if (!ctx) return NULL;
	uint32_t id=0;
	int status=-1;
	struct json_object *reply=NULL;
	struct blob_buf b={0};
	blob_buf_init(&b,0);
	if (blobmsg_add_json_from_string(&b,json_object_to_json_string_ext(args,JSON_C_TO_STRING_PLAIN)) &&
	    !ubus_lookup_id(ctx,object,&id) && id)
		status=ubus_invoke(ctx,id,method,b.head,rpc_reply,&reply,500);
	blob_buf_free(&b);
	ubus_free(ctx);
	if (status && reply) { json_object_put(reply); reply=NULL; }
	return reply;
}
static long long milliseconds(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC,&ts);
	return (long long)ts.tv_sec*1000+ts.tv_nsec/1000000;
}
struct json_object *upload_rpc(const char *object, const char *method, struct json_object *args)
{
	if (!args || strlen(json_object_to_json_string_ext(args,JSON_C_TO_STRING_PLAIN))>8192) return NULL;
	int pipefd[2];
	if (pipe(pipefd)) return NULL;
	fcntl(pipefd[0],F_SETFD,FD_CLOEXEC);
	fcntl(pipefd[1],F_SETFD,FD_CLOEXEC);
	pid_t child=fork();
	if (child<0) { close(pipefd[0]); close(pipefd[1]); return NULL; }
	if (!child) {
		close(pipefd[0]);
		signal(SIGALRM,SIG_DFL);
		signal(SIGTERM,SIG_DFL);
		signal(SIGPIPE,SIG_DFL);
		alarm(2);
		struct json_object *r=direct(object,method,args);
		if (!r) _exit(1);
		const char *text=json_object_to_json_string_ext(r,JSON_C_TO_STRING_PLAIN);
		size_t length=strlen(text), done=0;
		if (length>8192) _exit(1);
		while (done<length) {
			ssize_t n=write(pipefd[1],text+done,length-done);
			if (n<0 && errno==EINTR) continue;
			if (n<=0) _exit(1);
			done+=(size_t)n;
		}
		close(pipefd[1]);
		_exit(0);
	}
	close(pipefd[1]);
	char data[8193];
	size_t used=0;
	int eof=0;
	long long deadline=milliseconds()+1300;
	while (used<sizeof(data)-1) {
		int left=(int)(deadline-milliseconds());
		if (left<=0) break;
		struct pollfd p={.fd=pipefd[0],.events=POLLIN};
		int polled=poll(&p,1,left);
		if (polled<0 && errno==EINTR) continue;
		if (polled<=0) break;
		ssize_t n=read(pipefd[0],data+used,sizeof(data)-1-used);
		if (n<0 && errno==EINTR) continue;
		if (n<=0) { eof=n==0; break; }
		used+=(size_t)n;
	}
	close(pipefd[0]);
	if (!eof) kill(child,SIGKILL);
	int status=0;
	while (waitpid(child,&status,0)<0 && errno==EINTR) {}
	if (!eof || !WIFEXITED(status) || WEXITSTATUS(status) || !used) return NULL;
	data[used]=0;
	struct json_object *r=json_tokener_parse(data);
	if (r && !json_object_is_type(r,json_type_object)) { json_object_put(r); r=NULL; }
	return r;
}
