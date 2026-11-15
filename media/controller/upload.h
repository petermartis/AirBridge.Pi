#ifndef AIRBRIDGE_UPLOAD_H
#define AIRBRIDGE_UPLOAD_H
#include <json-c/json.h>
#include <stdint.h>
#include <stddef.h>

#define UPLOAD_CHUNK 1048576U
#define UPLOAD_RESERVE 268435456ULL
#define UPLOAD_MAX_SAFE 9007199254740991ULL
#define UPLOAD_EXPIRY 900
#define UPLOAD_LEASE 90

struct upload_auth {
	char session[33], token[65];
	uint64_t offset;
};
int upload_hex(const char *value, size_t length);
int upload_decimal(const char *value, uint64_t *number);
int upload_authorization(const char *header, struct upload_auth *auth);
int upload_number(struct json_object *args, const char *key, uint64_t *number);
const char *upload_string(struct json_object *args, const char *key);
struct json_object *upload_result(const char *error);
struct json_object *upload_rpc(const char *object, const char *method, struct json_object *args);
int upload_acl(const char *session, const char *method);
#endif
