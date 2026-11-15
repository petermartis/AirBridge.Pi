/* Host-only default policy; never installed or linked into release binaries. */
#include "../upload.h"
struct json_object *upload_rpc(const char *object, const char *method, struct json_object *args)
{
	(void)object; (void)method; (void)args;
	return NULL;
}
