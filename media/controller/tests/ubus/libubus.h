/* Test transport declarations only. Never in release include paths. */
#ifndef AIRBRIDGE_TEST_LIBUBUS_H
#define AIRBRIDGE_TEST_LIBUBUS_H
#include <stdint.h>
struct blob_attr;
struct ubus_context { int placeholder; };
struct ubus_request { void *priv; };
typedef void (*ubus_data_handler_t)(struct ubus_request *, int, struct blob_attr *);
struct ubus_context *ubus_connect(const char *path);
void ubus_free(struct ubus_context *ctx);
int ubus_lookup_id(struct ubus_context *ctx, const char *path, uint32_t *id);
int ubus_invoke(struct ubus_context *ctx, uint32_t id, const char *method, struct blob_attr *msg,
		ubus_data_handler_t callback, void *priv, int timeout);
#endif
