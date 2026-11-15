#ifndef AIRBRIDGE_MEDIA_FILES_H
#define AIRBRIDGE_MEDIA_FILES_H
#include "core.h"
#include <stdint.h>

void media_files_init(struct media *m, const char *root);
void media_files_close(struct media *m);
void media_files_expire(struct media *m, uint64_t now);
uint64_t media_files_now(void);
struct json_object *media_files_call(struct media *m, const char *method, struct json_object *args);
struct json_object *media_files_storage(struct media *m);
int media_path_valid(const char *path);
int media_file_supported(const char *path);
#endif
