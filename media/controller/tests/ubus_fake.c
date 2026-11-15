#include "ubus/libubus.h"
#include "ubus/libubox/blobmsg_json.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int fake_rpc_mode;
struct ubus_context *ubus_connect(const char *path)
{
	(void)path;
	return calloc(1,sizeof(struct ubus_context));
}
void ubus_free(struct ubus_context *ctx) { free(ctx); }
int ubus_lookup_id(struct ubus_context *ctx, const char *path, uint32_t *id)
{
	(void)ctx; (void)path;
	if (fake_rpc_mode==3) return 1;
	if (fake_rpc_mode==4) for (;;) pause();
	if (fake_rpc_mode!=2) *id=42;
	return 0;
}
int ubus_invoke(struct ubus_context *ctx, uint32_t id, const char *method, struct blob_attr *msg,
		ubus_data_handler_t callback, void *priv, int timeout)
{
	(void)ctx; (void)method; (void)timeout;
	if (id!=42) return 1;
	struct ubus_request request={.priv=priv};
	callback(&request,0,msg);
	return fake_rpc_mode==1 ? 1 : 0;
}
void blob_buf_init(struct blob_buf *b, int id)
{
	(void)id;
	b->head=calloc(1,sizeof(*b->head));
	b->head->length=16;
}
void blob_buf_free(struct blob_buf *b) { free(b->head); }
bool blobmsg_add_json_from_string(struct blob_buf *b, const char *text)
{
	(void)b;
	return text && *text;
}
char *blobmsg_format_json(struct blob_attr *a, bool list)
{
	(void)a; (void)list;
	if (fake_rpc_mode==5) return strdup("{\"access\":\"true\"}");
	if (fake_rpc_mode==6) return strdup("[]");
	if (fake_rpc_mode==7) {
		char *large=malloc(9001);
		memset(large,'x',9000); large[9000]=0;
		return large;
	}
	return strdup("{\"access\":true}");
}
