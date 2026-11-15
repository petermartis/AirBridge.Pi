/* Native CGI fixture: only RPC transport is fake; real ACL and body I/O run. */
#include "../upload.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <unistd.h>

static void trace(const char *method)
{
	const char *text=getenv("AB_TEST_TRACE_FD");
	if (!text) return;
	int fd=atoi(text);
	(void)write(fd,method,strlen(method));
	(void)write(fd,"\n",1);
}
static void number(struct json_object *o, const char *key, uint64_t n)
{
	json_object_object_add(o,key,json_object_new_int64((int64_t)n));
}
struct json_object *upload_rpc(const char *object, const char *method, struct json_object *args)
{
	trace(method);
	if (!strcmp(object,"session") && !strcmp(method,"access")) {
		static unsigned calls;
		++calls;
		const char *sid=upload_string(args,"ubus_rpc_session");
		const char *scope=upload_string(args,"scope"), *target=upload_string(args,"object");
		const char *function=upload_string(args,"function");
		int allowed=upload_hex(sid,32) && sid[0]=='a' && scope && !strcmp(scope,"ubus") &&
			target && !strcmp(target,"airbridge.media") && function && !strcmp(function,"upload_begin");
		if (getenv("AB_TEST_DENY_ACL") || (calls>1 && getenv("AB_TEST_EXPIRE_ACL"))) allowed=0;
		struct json_object *r=json_object_new_object();
		json_object_object_add(r,"access",getenv("AB_TEST_BAD_ACL_TYPE") ?
			json_object_new_string("true") : json_object_new_boolean(allowed));
		return r;
	}
	if (strcmp(object,"airbridge.media")) return NULL;
	if (!strcmp(method,"_upload_abort")) return upload_result(NULL);
	if (!strcmp(method,"_upload_check"))
		return upload_result(getenv("AB_TEST_CANCELED") ? "fixture upload canceled" : NULL);
	if (!strcmp(method,"_upload_claim")) {
		if (getenv("AB_TEST_BAD_TOKEN")) return upload_result("fixture token not owned");
		const char *data=getenv("AB_TEST_DATA_FD"), *directory=getenv("AB_TEST_PARENT_FD");
		if (!data || !directory) return upload_result("fixture missing descriptors");
		int fd=atoi(data), parent=atoi(directory);
		struct stat st, ps;
		uint64_t offset,length,total;
		if (fstat(fd,&st) || fstat(parent,&ps) || upload_number(args,"offset",&offset) ||
		    upload_number(args,"length",&length) || upload_decimal(getenv("AB_TEST_TOTAL"),&total))
			return upload_result("fixture metadata invalid");
		struct json_object *r=upload_result(NULL);
		char path[80], parentpath[80];
		snprintf(path,sizeof(path),"/proc/%ld/fd/%d",(long)getpid(),fd);
		snprintf(parentpath,sizeof(parentpath),"/proc/%ld/fd/%d",(long)getpid(),parent);
		json_object_object_add(r,"fd_path",json_object_new_string(getenv("AB_TEST_BAD_PATH") ? "/etc/passwd" : path));
		json_object_object_add(r,"parent_fd_path",json_object_new_string(parentpath));
		json_object_object_add(r,"stage_fd_path",json_object_new_string(parentpath));
		json_object_object_add(r,"lease",json_object_new_string("dddddddddddddddddddddddddddddddd"));
		number(r,"device",(uint64_t)st.st_dev);
		number(r,"inode",(uint64_t)st.st_ino+(getenv("AB_TEST_BAD_ID")!=NULL));
		number(r,"parent_inode",(uint64_t)ps.st_ino);
		number(r,"stage_inode",(uint64_t)ps.st_ino);
		number(r,"offset",offset); number(r,"length",length); number(r,"size",total);
		return r;
	}
	if (!strcmp(method,"_upload_done")) {
		if (getenv("AB_TEST_DONE_FAIL")) return upload_result("fixture publication refused");
		uint64_t offset,length,total;
		if (upload_number(args,"offset",&offset) || upload_number(args,"length",&length) ||
		    upload_decimal(getenv("AB_TEST_TOTAL"),&total)) return upload_result("fixture invalid offset");
		struct json_object *r=upload_result(NULL);
		number(r,"offset",offset+length); number(r,"size",total);
		number(r,"worker_nice",(uint64_t)getpriority(PRIO_PROCESS,0));
		json_object_object_add(r,"complete",json_object_new_boolean(offset+length==total));
		json_object_object_add(r,"path",json_object_new_string("fixture.mkv"));
		return r;
	}
	return NULL;
}
