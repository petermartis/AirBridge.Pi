#include "core.h"
#include "lock.h"
#include "files.h"
#include "upload.h"
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

/* Test harness only; never installed on-device. One bounded request per line. */
int main(int argc, char **argv)
{
	struct media m;
	char line[8194];
	if (argc==3 && !strcmp(argv[1],"--lock")) {
		int fd=media_lock(argv[2]);
		if (fd<0) return 1;
		puts("locked");
		fflush(stdout);
		(void)getchar();
		close(fd);
		return 0;
	}
	if (argc != 3 || media_init(&m, argv[1], argv[2])) return 2;
	while (fgets(line, sizeof(line), stdin)) {
		struct json_object *req, *method, *args, *reply;
		if (!strchr(line, '\n')) break;
		req = json_tokener_parse(line);
		if (!req || !json_object_object_get_ex(req, "method", &method) ||
		    !json_object_is_type(method, json_type_string)) {
			if (req) json_object_put(req);
			break;
		}
		args = NULL;
		json_object_object_get_ex(req, "args", &args);
		const char *name=json_object_get_string(method);
		if (!strcmp(name,"_test_expire")) {
			media_files_expire(&m,UINT64_MAX);
			reply=media_files_storage(&m);
		} else if (!strcmp(name,"_test_fd_identity")) {
			uint64_t fd;
			struct stat st;
			if (upload_number(args,"fd",&fd) || fd>INT_MAX || fstat((int)fd,&st)) reply=upload_result("descriptor unavailable");
			else {
				reply=upload_result(NULL);
				json_object_object_add(reply,"device",json_object_new_int64((int64_t)st.st_dev));
				json_object_object_add(reply,"inode",json_object_new_int64((int64_t)st.st_ino));
				json_object_object_add(reply,"links",json_object_new_int64((int64_t)st.st_nlink));
			}
		} else if (!strcmp(name,"_test_hold")) {
			const char *path=upload_string(args,"path");
			int fd=path && media_path_valid(path) && !strchr(path,'/') ?
				openat(m.rootfd,path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC) : -1;
			struct stat st;
			if (fd<0 || fstat(fd,&st) || !S_ISREG(st.st_mode)) {
				if (fd>=0) close(fd);
				reply=upload_result("fixture file unavailable");
			} else {
				if (m.mediafd>=0) close(m.mediafd);
				m.mediafd=fd;
				reply=upload_result(NULL);
			}
		} else if (!strcmp(name,"_test_release")) {
			if (m.mediafd>=0) close(m.mediafd);
			m.mediafd=-1;
			reply=upload_result(NULL);
		} else if (!strcmp(name,"_test_guard")) {
			media_guard(&m);
			reply=media_call(&m,"status",args);
		} else reply = media_call(&m, json_object_get_string(method), args);
		puts(json_object_to_json_string_ext(reply, JSON_C_TO_STRING_PLAIN));
		fflush(stdout);
		json_object_put(reply);
		json_object_put(req);
	}
	media_close(&m);
	return 0;
}
