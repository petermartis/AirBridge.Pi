#ifndef AIRBRIDGE_MEDIA_CORE_H
#define AIRBRIDGE_MEDIA_CORE_H
#include <json-c/json.h>
#include <stddef.h>

struct media_files;
struct media {
	struct media_files *files;
	int rootfd, mediafd, ipcfd, file_loaded;
	unsigned request_id;
	long long deadline;
	char socket_path[108], profile[16], title[1024], error[256];
	char queue[128][1024];
	size_t queue_len;
	size_t readlen;
	char readbuf[65537];
};
int media_init(struct media *m, const char *root, const char *socket_path);
void media_close(struct media *m);
void media_guard(struct media *m);
struct json_object *media_call(struct media *m, const char *method, struct json_object *args);
#endif
