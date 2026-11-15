#include "core.h"
#include "lock.h"
#include <libubox/blobmsg_json.h>
#include <libubox/uloop.h>
#include <libubus.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct media controller;
static struct blob_buf reply;
static struct uloop_timeout watchdog;
static void guard(struct uloop_timeout *timer)
{
	media_guard(&controller);
	uloop_timeout_set(timer,1000);
}
static int handle(struct ubus_context *ctx, struct ubus_object *obj,
		  struct ubus_request_data *req, const char *method,
		  struct blob_attr *msg)
{
	(void)obj;
	struct json_object *args = NULL, *res;
	char *json = NULL;
	if (msg && blob_len(msg) > 8192) return UBUS_STATUS_INVALID_ARGUMENT;
	if (msg) {
		json = blobmsg_format_json(msg, true);
		if (!json) return UBUS_STATUS_INVALID_ARGUMENT;
		args = json_tokener_parse(json);
		free(json);
		if (!args) return UBUS_STATUS_INVALID_ARGUMENT;
	}
	res = media_call(&controller, method, args);
	if (args) json_object_put(args);
	const char *wire = json_object_to_json_string_ext(res, JSON_C_TO_STRING_PLAIN);
	blob_buf_init(&reply, 0);
	if (strlen(wire) > 262144 || !blobmsg_add_json_from_string(&reply, wire)) {
		json_object_put(res);
		blob_buf_free(&reply);
		return UBUS_STATUS_UNKNOWN_ERROR;
	}
	int rc = ubus_send_reply(ctx, req, reply.head);
	json_object_put(res);
	blob_buf_free(&reply);
	return rc;
}
/* Policies describe rpcd's discoverable typed interface; the core independently
 * validates every field. Blob double permits fractional seconds and volume. */
static const struct blobmsg_policy browse_policy[] = {
	{ .name="path", .type=BLOBMSG_TYPE_STRING },
	{ .name="offset", .type=BLOBMSG_TYPE_INT32 },
	{ .name="limit", .type=BLOBMSG_TYPE_INT32 }
};
static const struct blobmsg_policy path_policy[] = {{ .name="path", .type=BLOBMSG_TYPE_STRING }};
static const struct blobmsg_policy pause_policy[] = {{ .name="paused", .type=BLOBMSG_TYPE_BOOL }};
static const struct blobmsg_policy seek_policy[] = {
	{ .name="seconds", .type=BLOBMSG_TYPE_UNSPEC },
	{ .name="relative", .type=BLOBMSG_TYPE_BOOL }
};
static const struct blobmsg_policy volume_policy[] = {
	{ .name="value", .type=BLOBMSG_TYPE_UNSPEC },
	{ .name="mute", .type=BLOBMSG_TYPE_BOOL }
};
static const struct blobmsg_policy tracks_policy[] = {
	{ .name="type", .type=BLOBMSG_TYPE_STRING },
	{ .name="id", .type=BLOBMSG_TYPE_INT32 }
};
static const struct blobmsg_policy queue_policy[] = {
	{ .name="action", .type=BLOBMSG_TYPE_STRING },
	{ .name="path", .type=BLOBMSG_TYPE_STRING }
};
static const struct blobmsg_policy profile_policy[] = {{ .name="name", .type=BLOBMSG_TYPE_STRING }};
static const struct blobmsg_policy upload_begin_policy[] = {
	{ .name="path", .type=BLOBMSG_TYPE_STRING },
	{ .name="size", .type=BLOBMSG_TYPE_UNSPEC },
	{ .name="ubus_rpc_session", .type=BLOBMSG_TYPE_STRING }
};
static const struct blobmsg_policy file_remove_policy[] = {
	{ .name="path", .type=BLOBMSG_TYPE_STRING },
	{ .name="ubus_rpc_session", .type=BLOBMSG_TYPE_STRING }
};
static const struct blobmsg_policy upload_cancel_policy[] = {
	{ .name="token", .type=BLOBMSG_TYPE_STRING },
	{ .name="ubus_rpc_session", .type=BLOBMSG_TYPE_STRING }
};
static const struct blobmsg_policy upload_private_policy[] = {
	{ .name="session", .type=BLOBMSG_TYPE_STRING },
	{ .name="token", .type=BLOBMSG_TYPE_STRING },
	{ .name="lease", .type=BLOBMSG_TYPE_STRING },
	{ .name="offset", .type=BLOBMSG_TYPE_UNSPEC },
	{ .name="length", .type=BLOBMSG_TYPE_UNSPEC }
};
#define PRIVATE_UPLOAD_METHOD(name_) { .name=name_, .handler=handle, .tags=UBUS_TAG_PRIVATE, \
	.policy=upload_private_policy, .n_policy=sizeof(upload_private_policy)/sizeof(upload_private_policy[0]) }
static const struct ubus_method methods[] = {
	UBUS_METHOD("browse",handle,browse_policy),
	UBUS_METHOD("play",handle,path_policy),
	UBUS_METHOD("pause",handle,pause_policy),
	UBUS_METHOD_NOARG("stop",handle),
	UBUS_METHOD("seek",handle,seek_policy),
	UBUS_METHOD("volume",handle,volume_policy),
	UBUS_METHOD("tracks",handle,tracks_policy),
	UBUS_METHOD("queue",handle,queue_policy),
	UBUS_METHOD("profile",handle,profile_policy),
	UBUS_METHOD_NOARG("status",handle),
	UBUS_METHOD_NOARG("storage",handle),
	UBUS_METHOD("remove",handle,file_remove_policy),
	UBUS_METHOD("upload_begin",handle,upload_begin_policy),
	UBUS_METHOD("upload_cancel",handle,upload_cancel_policy),
	PRIVATE_UPLOAD_METHOD("_upload_claim"),
	PRIVATE_UPLOAD_METHOD("_upload_check"),
	PRIVATE_UPLOAD_METHOD("_upload_done"),
	PRIVATE_UPLOAD_METHOD("_upload_abort")
};
static struct ubus_object_type type = UBUS_OBJECT_TYPE("airbridge.media",methods);
static struct ubus_object object = {
	.name="airbridge.media", .type=&type,
	.methods=methods, .n_methods=sizeof(methods)/sizeof(methods[0])
};
static void shutdown_handler(int signal_number)
{
	(void)signal_number;
	uloop_end();
}
int main(int argc, char **argv)
{
	umask(077);
	int lockfd=media_lock("/var/run/airbridge-media/controller.lock");
	if (lockfd<0) {
		fprintf(stderr,"airbridge-media: controller already running or private lock unavailable\n");
		return 1;
	}
	if (argc != 2 || media_init(&controller,argv[1],"/var/run/airbridge-media/mpv.sock")) {
		fprintf(stderr,"usage: airbridge-media-controller /absolute/media/root\n");
		return 2;
	}
	if (uloop_init()) { media_close(&controller); return 1; }
	struct ubus_context *ctx=ubus_connect(NULL);
	if (!ctx) { media_close(&controller); uloop_done(); return 1; }
	ubus_add_uloop(ctx);
	if (ubus_add_object(ctx,&object)) {
		ubus_free(ctx); media_close(&controller); uloop_done(); return 1;
	}
	signal(SIGTERM,shutdown_handler);
	signal(SIGINT,shutdown_handler);
	watchdog.cb=guard;
	uloop_timeout_set(&watchdog,1000);
	uloop_run();
	uloop_timeout_cancel(&watchdog);
	ubus_free(ctx);
	media_close(&controller);
	uloop_done();
	close(lockfd);
	return 0;
}
