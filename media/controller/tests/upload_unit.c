#include "../upload.h"
#include "../upload_io.h"
#include "../held_fd.h"
#include "../fs_mount.h"
#include <fcntl.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
	struct upload_auth auth;
	char header[160];
	const char *sid="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", *token="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
	snprintf(header,sizeof(header),"Bearer %s:%s:0",sid,token);
	assert(!upload_authorization(header,&auth) && auth.offset==0);
	snprintf(header,sizeof(header),"Bearer %s:%s:4294967297",sid,token);
	assert(!upload_authorization(header,&auth) && auth.offset==4294967297ULL);
	const char *invalid[]={"","Bearer x","Basic abc","Bearer a:b:-1","Bearer a:b:1","Bearer a:b:1?path=/x"};
	for (size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) assert(upload_authorization(invalid[i],&auth));
	uint64_t n=0;
	assert(!upload_decimal("9007199254740991",&n) && n==UPLOAD_MAX_SAFE);
	assert(upload_decimal("9007199254740992",&n));
	assert(upload_decimal("-1",&n) && upload_decimal("+1",&n) && upload_decimal(" 1",&n));
	struct json_object *args=json_tokener_parse("{\"size\":true,\"path\":\"a\\u0000b\"}");
	assert(upload_number(args,"size",&n));
	assert(!upload_string(args,"path"));
	json_object_put(args);
	assert(upload_held_open("/etc/passwd",0)<0);
	assert(upload_held_open("/proc/1/fd/3/../../../etc/passwd",0)<0);
	assert(fs_mount_devices_allowed(1793,1792,1791,0,179));
	assert(!fs_mount_devices_allowed(1792,1792,1791,0,179));
	assert(!fs_mount_devices_allowed(1791,1792,1791,0,179));
	assert(!fs_mount_devices_allowed(8,1792,1791,8,179));
	assert(!fs_mount_devices_allowed(9,1792,1791,8,0));
	assert(fs_mount_ids_equal(1793,50,1793,50));
	assert(!fs_mount_ids_equal(1793,50,1793,51));
	assert(!fs_mount_ids_equal(1793,50,1794,50));
	assert(fs_mount_line_matches("50 9 179:3 / /mnt/media rw - ext4 /dev/mmcblk0p3 rw\n",50,179,3,"/mnt/media"));
	assert(!fs_mount_line_matches("50 9 179:3 / /mnt/media rw - tmpfs tmpfs rw\n",50,179,3,"/mnt/media"));
	assert(!fs_mount_line_matches("50 9 179:3 /sub /mnt/media rw - ext4 /dev/mmcblk0p3 rw\n",50,179,3,"/mnt/media"));
	assert(!fs_mount_line_matches("50 9 179:3 / /mnt/media rw - ext4 /dev/mmcblk0p3 rw\n",51,179,3,"/mnt/media"));
	assert(!fs_mount_line_matches("50 9 179:2 / /mnt/media rw - ext4 /dev/mmcblk0p3 rw\n",50,179,3,"/mnt/media"));
	assert(fs_mount_line_matches("50 9 179:3 / /mnt/my\\040media rw - ext4 /dev/mmcblk0p3 rw\n",50,179,3,"/mnt/my media"));
	const char *protected_roots[]={"/","/tmp","/boot"};
	for (size_t i=0;i<3;++i) {
		int fd=open(protected_roots[i],O_RDONLY|O_DIRECTORY);
		if (fd>=0) {
			struct fs_mount_info info;
			assert(fs_mount_check(fd,protected_roots[i],&info));
			assert(!info.writable);
			close(fd);
		}
	}
	int input[2];
	assert(!pipe(input));
	FILE *output=tmpfile();
	assert(output);
	long long start=upload_now_ms();
	assert(upload_stream_chunk(input[0],fileno(output),0,1,start+40));
	assert(upload_now_ms()-start>=20 && upload_now_ms()-start<1000);
	close(input[0]); close(input[1]); fclose(output);
	puts("upload protocol, production mount-denial policy and bounded body deadline tests passed");
	return 0;
}
