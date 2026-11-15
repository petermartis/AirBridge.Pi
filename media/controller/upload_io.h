#ifndef AIRBRIDGE_UPLOAD_IO_H
#define AIRBRIDGE_UPLOAD_IO_H
#include <stdint.h>
long long upload_now_ms(void);
int upload_stream_chunk(int input_fd, int output_fd, uint64_t offset, uint64_t length, long long deadline);
#endif
