/* Test-only transport model, not a substitute for target SDK ABI headers. */
#ifndef AIRBRIDGE_TEST_BLOBMSG_H
#define AIRBRIDGE_TEST_BLOBMSG_H
#include <stdbool.h>
#include <stddef.h>
struct blob_attr { size_t length; };
struct blob_buf { struct blob_attr *head; };
static inline size_t blob_len(const struct blob_attr *a) { return a->length; }
void blob_buf_init(struct blob_buf *b, int id);
void blob_buf_free(struct blob_buf *b);
bool blobmsg_add_json_from_string(struct blob_buf *b, const char *text);
char *blobmsg_format_json(struct blob_attr *a, bool list);
#endif
