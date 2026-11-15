CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE
JSON_CFLAGS := $(shell pkg-config --cflags json-c)
JSON_LIBS := $(shell pkg-config --libs json-c)

CORE_SOURCES = core.c files.c upload_protocol.c upload_auth.c
all: media-host media-files-host media-cgi-host media-upload-unit media-rpc-unit
media-host: $(CORE_SOURCES) core.h files.h upload.h fs_mount.h host.c lock.h fs_mount.c tests/rpc_deny.c
	$(CC) $(CFLAGS) $(JSON_CFLAGS) $(CORE_SOURCES) fs_mount.c tests/rpc_deny.c host.c $(LDFLAGS) $(JSON_LIBS) -lm -o $@
media-files-host: $(CORE_SOURCES) core.h files.h upload.h fs_mount.h host.c lock.h tests/fs_mount_fake.c tests/rpc_acl_fake.c
	$(CC) $(CFLAGS) $(JSON_CFLAGS) $(CORE_SOURCES) tests/fs_mount_fake.c tests/rpc_acl_fake.c host.c $(LDFLAGS) $(JSON_LIBS) -lm -o $@
media-cgi-host: upload_cgi.c upload_io.c upload_io.h upload_protocol.c upload_auth.c upload.h held_fd.h tests/cgi_rpc_fake.c tests/held_fd_fake.c
	$(CC) $(CFLAGS) $(JSON_CFLAGS) upload_cgi.c upload_io.c upload_protocol.c upload_auth.c tests/cgi_rpc_fake.c tests/held_fd_fake.c $(LDFLAGS) $(JSON_LIBS) -lm -o $@
media-upload-unit: tests/upload_unit.c upload_protocol.c upload_io.c held_fd.c fs_mount.c upload.h upload_io.h held_fd.h fs_mount.h
	$(CC) $(CFLAGS) $(JSON_CFLAGS) tests/upload_unit.c upload_protocol.c upload_io.c held_fd.c fs_mount.c $(LDFLAGS) $(JSON_LIBS) -lm -o $@
media-rpc-unit: tests/rpc_unit.c tests/ubus_fake.c tests/ubus/libubus.h tests/ubus/libubox/blobmsg_json.h upload_rpc.c upload_protocol.c upload_auth.c upload_io.c upload.h
	$(CC) $(CFLAGS) $(JSON_CFLAGS) -Itests/ubus tests/rpc_unit.c tests/ubus_fake.c upload_rpc.c upload_protocol.c upload_auth.c upload_io.c $(LDFLAGS) $(JSON_LIBS) -lm -o $@
check: all
	$(CC) $(CFLAGS) -fsyntax-only player.c
	$(CC) $(CFLAGS) -DAIRBRIDGE_MEDIA_MPV_SCRIPTING=1 -DAIRBRIDGE_MEDIA_MPV_LUA=1 -fsyntax-only player.c
	python3 tests.py ./media-host
	./media-upload-unit
	./media-rpc-unit
	python3 test_files.py ./media-files-host ./media-cgi-host ./media-host
clean:
	rm -f media-host media-files-host media-cgi-host media-upload-unit media-rpc-unit
.PHONY: all check clean
