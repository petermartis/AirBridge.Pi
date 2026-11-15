#include "upload_io.h"
#include <errno.h>
#include <poll.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

long long upload_now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC,&ts);
	return (long long)ts.tv_sec*1000+ts.tv_nsec/1000000;
}
int upload_stream_chunk(int input_fd, int fd, uint64_t offset, uint64_t length, long long deadline)
{
	unsigned char buffer[65536];
	uint64_t received=0;
	while (received<length) {
		int left=(int)(deadline-upload_now_ms());
		if (left<=0) return -1;
		if (left>30000) left=30000;
		struct pollfd input={.fd=input_fd,.events=POLLIN};
		int ready=poll(&input,1,left);
		if (ready<0 && errno==EINTR) continue;
		if (ready<=0) return -1;
		size_t count=(length-received)<sizeof(buffer) ? (size_t)(length-received) : sizeof(buffer);
		ssize_t n=read(input_fd,buffer,count);
		if (n<0 && errno==EINTR) continue;
		if (n<=0) return -1;
		struct stat st;
		if (fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_nlink!=1) return -1;
		size_t written=0;
		while (written<(size_t)n) {
			if (upload_now_ms()>=deadline) return -1;
			ssize_t amount=pwrite(fd,buffer+written,(size_t)n-written,(off_t)(offset+received+written));
			if (amount<0 && errno==EINTR) continue;
			if (amount<=0) return -1;
			written+=(size_t)amount;
		}
		received+=(uint64_t)n;
	}
	return 0;
}
