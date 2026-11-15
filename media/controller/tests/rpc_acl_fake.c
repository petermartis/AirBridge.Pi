/* Explicit native test object, never a release flag or environment bypass. */
#include "../upload.h"
#include <string.h>
struct json_object *upload_rpc(const char *object, const char *method, struct json_object *args)
{
	const char *sid=upload_string(args,"ubus_rpc_session");
	const char *scope=upload_string(args,"scope"), *target=upload_string(args,"object");
	const char *function=upload_string(args,"function");
	if (strcmp(object,"session") || strcmp(method,"access") || !scope || strcmp(scope,"ubus") ||
	    !target || strcmp(target,"airbridge.media") || !function) return NULL;
	struct json_object *r=json_object_new_object();
	int allowed=upload_hex(sid,32) && sid[0]=='a' &&
		(!strcmp(function,"upload_begin") || !strcmp(function,"upload_cancel") || !strcmp(function,"remove"));
	json_object_object_add(r,"access",sid && sid[0]=='c' ?
		json_object_new_string("true") : json_object_new_boolean(allowed));
	return r;
}
