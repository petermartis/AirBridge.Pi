#include "upload.h"
#include <string.h>

int upload_acl(const char *session, const char *method)
{
	if (!upload_hex(session,32) || (strcmp(method,"upload_begin") &&
	    strcmp(method,"upload_cancel") && strcmp(method,"remove"))) return 0;
	struct json_object *args=json_object_new_object(), *access=NULL;
	json_object_object_add(args,"ubus_rpc_session",json_object_new_string(session));
	json_object_object_add(args,"scope",json_object_new_string("ubus"));
	json_object_object_add(args,"object",json_object_new_string("airbridge.media"));
	json_object_object_add(args,"function",json_object_new_string(method));
	struct json_object *r=upload_rpc("session","access",args);
	json_object_put(args);
	int allowed=r && json_object_object_get_ex(r,"access",&access) &&
		json_object_is_type(access,json_type_boolean) && json_object_get_boolean(access);
	if (r) json_object_put(r);
	return allowed;
}
