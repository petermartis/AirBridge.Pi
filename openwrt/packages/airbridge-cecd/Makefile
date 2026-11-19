include $(TOPDIR)/rules.mk

PKG_NAME:=airbridge-cecd
PKG_VERSION:=1.0.0
PKG_RELEASE:=1
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
AIRBRIDGE_CECD_SOURCE ?= $(CURDIR)/../../..

include $(INCLUDE_DIR)/package.mk

define Package/airbridge-cecd
  SECTION:=multimedia
  CATEGORY:=Multimedia
  TITLE:=AirBridge.Pi HDMI-CEC TV-remote passthrough daemon
  DEPENDS:=+airbridge-media
endef

define Package/airbridge-cecd/description
  Claims CEC logical address 4 (Playback 1, "AirBridge") on the HDMI0
  adapter, announces itself as the active source and maps the TV remote's
  User Control Pressed messages to airbridge.media ubus actions. Pure libc:
  no libraries linked, one fork+exec per key through ceckey.sh.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(AIRBRIDGE_CECD_SOURCE)/media/cecd/airbridge-cecd.c $(PKG_BUILD_DIR)/
	$(CP) -r $(AIRBRIDGE_CECD_SOURCE)/media/cecd/uapi $(PKG_BUILD_DIR)/uapi
endef

# uapi/ carries the 6.12 linux/cec.h + cec-funcs.h; older SDK kernel headers
# lack the CEC_MSG_* helpers, so include it first for determinism.
define Build/Compile
	$(TARGET_CC) $(TARGET_CPPFLAGS) $(TARGET_CFLAGS) -I$(PKG_BUILD_DIR)/uapi \
		-std=gnu11 -Wall -Wextra -Werror \
		$(PKG_BUILD_DIR)/airbridge-cecd.c $(TARGET_LDFLAGS) \
		-o $(PKG_BUILD_DIR)/airbridge-cecd
endef

define Package/airbridge-cecd/install
	$(INSTALL_DIR) $(1)/usr/sbin $(1)/usr/lib/airbridge-cecd $(1)/etc/init.d
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/airbridge-cecd $(1)/usr/sbin/
	$(INSTALL_BIN) $(AIRBRIDGE_CECD_SOURCE)/media/cecd/ceckey.sh $(1)/usr/lib/airbridge-cecd/
	$(INSTALL_BIN) ./files/airbridge-cecd.init $(1)/etc/init.d/airbridge-cecd
endef

$(eval $(call BuildPackage,airbridge-cecd))
