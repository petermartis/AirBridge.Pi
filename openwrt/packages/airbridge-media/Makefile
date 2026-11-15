include $(TOPDIR)/rules.mk

PKG_NAME:=airbridge-media
PKG_VERSION:=1.0.0
PKG_RELEASE:=5
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
# This recipe is copied into an SDK package directory by the lead's build
# helper. Explicit source path avoids assuming the SDK's working directory.
AIRBRIDGE_MEDIA_SOURCE ?= $(CURDIR)/../../..
# Must match private mpv: -Dlua=disabled -Djavascript=disabled -Dcplugins=disabled.
AIRBRIDGE_MEDIA_MPV_SCRIPTING ?= 0
AIRBRIDGE_MEDIA_MPV_LUA ?= 0

include $(INCLUDE_DIR)/package.mk

define Package/airbridge-media
  SECTION:=multimedia
  CATEGORY:=Multimedia
  TITLE:=AirBridge.Pi native local media controller
  DEPENDS:=+libubus +libubox +libblobmsg-json +libjson-c +airbridge-media-engine
endef

define Package/airbridge-media/description
  Typed local-file-only ubus control of a private mpv engine. Disabled by
  default; does not change networking, Wi-Fi drivers, LCD or boot settings.
endef

define Package/airbridge-media/conffiles
/etc/config/airbridge-media
/etc/airbridge-media/mpv.conf
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(AIRBRIDGE_MEDIA_SOURCE)/media/controller/core.c $(PKG_BUILD_DIR)/
	$(CP) $(AIRBRIDGE_MEDIA_SOURCE)/media/controller/core.h $(PKG_BUILD_DIR)/
	$(CP) $(AIRBRIDGE_MEDIA_SOURCE)/media/controller/ubus.c $(PKG_BUILD_DIR)/
	$(CP) $(AIRBRIDGE_MEDIA_SOURCE)/media/controller/player.c $(PKG_BUILD_DIR)/
	$(CP) $(AIRBRIDGE_MEDIA_SOURCE)/media/controller/lock.h $(PKG_BUILD_DIR)/
	$(CP) $(AIRBRIDGE_MEDIA_SOURCE)/media/controller/files.c $(PKG_BUILD_DIR)/
	$(CP) $(AIRBRIDGE_MEDIA_SOURCE)/media/controller/files.h $(PKG_BUILD_DIR)/
	$(CP) $(AIRBRIDGE_MEDIA_SOURCE)/media/controller/fs_mount.c $(PKG_BUILD_DIR)/
	$(CP) $(AIRBRIDGE_MEDIA_SOURCE)/media/controller/fs_mount.h $(PKG_BUILD_DIR)/
	$(CP) $(AIRBRIDGE_MEDIA_SOURCE)/media/controller/upload.h $(PKG_BUILD_DIR)/
	$(CP) $(AIRBRIDGE_MEDIA_SOURCE)/media/controller/upload_protocol.c $(PKG_BUILD_DIR)/
	$(CP) $(AIRBRIDGE_MEDIA_SOURCE)/media/controller/upload_auth.c $(PKG_BUILD_DIR)/
	$(CP) $(AIRBRIDGE_MEDIA_SOURCE)/media/controller/upload_rpc.c $(PKG_BUILD_DIR)/
	$(CP) $(AIRBRIDGE_MEDIA_SOURCE)/media/controller/upload_io.c $(PKG_BUILD_DIR)/
	$(CP) $(AIRBRIDGE_MEDIA_SOURCE)/media/controller/upload_io.h $(PKG_BUILD_DIR)/
	$(CP) $(AIRBRIDGE_MEDIA_SOURCE)/media/controller/upload_cgi.c $(PKG_BUILD_DIR)/
	$(CP) $(AIRBRIDGE_MEDIA_SOURCE)/media/controller/held_fd.c $(PKG_BUILD_DIR)/
	$(CP) $(AIRBRIDGE_MEDIA_SOURCE)/media/controller/held_fd.h $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CPPFLAGS) $(TARGET_CFLAGS) \
		-std=c11 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
		$(PKG_BUILD_DIR)/core.c $(PKG_BUILD_DIR)/files.c $(PKG_BUILD_DIR)/fs_mount.c \
		$(PKG_BUILD_DIR)/upload_protocol.c $(PKG_BUILD_DIR)/upload_auth.c \
		$(PKG_BUILD_DIR)/upload_rpc.c $(PKG_BUILD_DIR)/ubus.c \
		$(TARGET_LDFLAGS) -lubus -lubox -lblobmsg_json -ljson-c -lm \
		-o $(PKG_BUILD_DIR)/airbridge-media-controller
	$(TARGET_CC) $(TARGET_CPPFLAGS) $(TARGET_CFLAGS) \
		-std=c11 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
		$(PKG_BUILD_DIR)/upload_cgi.c $(PKG_BUILD_DIR)/upload_io.c $(PKG_BUILD_DIR)/held_fd.c \
		$(PKG_BUILD_DIR)/upload_protocol.c $(PKG_BUILD_DIR)/upload_auth.c $(PKG_BUILD_DIR)/upload_rpc.c \
		$(TARGET_LDFLAGS) -lubus -lubox -lblobmsg_json -ljson-c -lm \
		-o $(PKG_BUILD_DIR)/airbridge-media-upload
	$(TARGET_CC) $(TARGET_CPPFLAGS) $(TARGET_CFLAGS) \
		-std=c11 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
		-DAIRBRIDGE_MEDIA_MPV_SCRIPTING=$(AIRBRIDGE_MEDIA_MPV_SCRIPTING) \
		-DAIRBRIDGE_MEDIA_MPV_LUA=$(AIRBRIDGE_MEDIA_MPV_LUA) \
		$(PKG_BUILD_DIR)/player.c $(TARGET_LDFLAGS) -o $(PKG_BUILD_DIR)/airbridge-media-player
endef

define Package/airbridge-media/install
	$(INSTALL_DIR) $(1)/usr/sbin $(1)/usr/libexec $(1)/etc/init.d \
		$(1)/etc/config $(1)/etc/airbridge-media $(1)/mnt/media $(1)/www/cgi-bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/airbridge-media-controller $(1)/usr/sbin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/airbridge-media-upload $(1)/www/cgi-bin/
	$(INSTALL_BIN) ./files/airbridge-media.init $(1)/etc/init.d/airbridge-media
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/airbridge-media-player $(1)/usr/libexec/
	$(INSTALL_CONF) ./files/airbridge-media.config $(1)/etc/config/airbridge-media
	$(INSTALL_CONF) ./files/mpv.conf $(1)/etc/airbridge-media/mpv.conf
endef

$(eval $(call BuildPackage,airbridge-media))
