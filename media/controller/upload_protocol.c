#include "upload.h"
#include <math.h>
#include <string.h>

int upload_hex(const char *value, size_t length)
{
	if (!value || strlen(value)!=length) return 0;
	for (size_t i=0;i<length;++i)
		if (!((value[i]>='0' && value[i]<='9') || (value[i]>='a' && value[i]<='f'))) return 0;
	return 1;
}
int upload_decimal(const char *value, uint64_t *number)
{
	uint64_t n=0;
	if (!value || !*value || strlen(value)>16) return -1;
	for (const char *p=value;*p;++p) {
		if (*p<'0' || *p>'9' || n>(UPLOAD_MAX_SAFE-(unsigned)(*p-'0'))/10) return -1;
		n=n*10+(unsigned)(*p-'0');
	}
	*number=n;
	return 0;
}
int upload_authorization(const char *header, struct upload_auth *auth)
{
	if (!header || strlen(header)>128 || strncmp(header,"Bearer ",7)) return -1;
	const char *p=header+7;
	if (strlen(p)<99 || p[32]!=':' || p[97]!=':') return -1;
	memcpy(auth->session,p,32); auth->session[32]=0;
	memcpy(auth->token,p+33,64); auth->token[64]=0;
	if (!upload_hex(auth->session,32) || !upload_hex(auth->token,64) ||
	    upload_decimal(p+98,&auth->offset)) return -1;
	return 0;
}
const char *upload_string(struct json_object *args, const char *key)
{
	struct json_object *v;
	if (!args || !json_object_is_type(args,json_type_object) ||
	    !json_object_object_get_ex(args,key,&v) || !json_object_is_type(v,json_type_string)) return NULL;
	const char *s=json_object_get_string(v);
	return strlen(s)==(size_t)json_object_get_string_len(v) ? s : NULL;
}
int upload_number(struct json_object *args, const char *key, uint64_t *number)
{
	struct json_object *v;
	if (!args || !json_object_object_get_ex(args,key,&v) ||
	    !(json_object_is_type(v,json_type_int) || json_object_is_type(v,json_type_double))) return -1;
	double n=json_object_get_double(v);
	if (!isfinite(n) || n<0 || n>(double)UPLOAD_MAX_SAFE || floor(n)!=n) return -1;
	*number=(uint64_t)n;
	return 0;
}
struct json_object *upload_result(const char *error)
{
	struct json_object *r=json_object_new_object();
	json_object_object_add(r,"ok",json_object_new_boolean(!error));
	json_object_object_add(r,"error",error ? json_object_new_string(error) : NULL);
	return r;
}
