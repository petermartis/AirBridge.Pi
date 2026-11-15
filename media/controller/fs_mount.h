#ifndef AIRBRIDGE_FS_MOUNT_H
#define AIRBRIDGE_FS_MOUNT_H
#include <stdint.h>
#include <sys/types.h>

struct fs_mount_info {
	int mounted, writable;
	dev_t device;
	uint64_t mount_id, total, free;
	char reason[192];
};
/* Production implementation is fs_mount.c. Host fixtures link a separate fake,
 * never a runtime environment flag or a release-build bypass. */
int fs_mount_check(int rootfd, const char *configured_root, struct fs_mount_info *info);
int fs_same_mount(int rootfd, int fd);
int fs_rename_noreplace(int source_dir, const char *source, int target_dir, const char *target);
int fs_mount_devices_allowed(uint64_t media, uint64_t root, uint64_t boot, uint64_t temporary, unsigned major_number);
int fs_mount_ids_equal(uint64_t device_a, uint64_t mount_a, uint64_t device_b, uint64_t mount_b);
int fs_mount_line_matches(const char *line, uint64_t id, unsigned device_major, unsigned device_minor, const char *actual_path);
#endif
