#include "../upload.h"
#include "../upload_io.h"
#include <assert.h>
#include <stdio.h>
extern int fake_rpc_mode;

int main(void)
{
	const char *sid="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	fake_rpc_mode=0;
	assert(upload_acl(sid,"upload_begin"));
	assert(!upload_acl("not-session","upload_begin"));
	assert(!upload_acl(sid,"exec"));
	for (int mode=1;mode<=7;++mode) {
		fake_rpc_mode=mode;
		long long start=upload_now_ms();
		assert(!upload_acl(sid,"upload_begin"));
		assert(upload_now_ms()-start<3000);
		if (mode==4) assert(upload_now_ms()-start>=1000);
	}
	puts("actual RPC/auth code rejects status errors, missing IDs, malformed grants and bounded hung lookup");
	return 0;
}
