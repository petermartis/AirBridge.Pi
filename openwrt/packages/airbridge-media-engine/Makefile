include $(TOPDIR)/rules.mk

PKG_NAME:=airbridge-media-engine
PKG_VERSION:=1.0.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-or-later LGPL-2.1-or-later MIT BSD-2-Clause BSD-3-Clause ISC
# The isolated cross-build produces a stripped, checksummed private runtime.
# Explicitly supply AIRBRIDGE_MEDIA_ENGINE_ROOT=/.../engine-root.
AIRBRIDGE_MEDIA_ENGINE_ROOT ?=

include $(INCLUDE_DIR)/package.mk

define Package/airbridge-media-engine
  SECTION:=multimedia
  CATEGORY:=Multimedia
  TITLE:=AirBridge.Pi private mpv/FFmpeg/V3D media runtime
  DEPENDS:=@TARGET_bcm27xx_bcm2712 +libc +libgcc +libstdcpp +kmod-drm-vc4 +kmod-drm-v3d \
    +kmod-airbridge-hevc +kmod-airbridge-hdmi-codec
endef

define Build/Prepare
	test -n "$(AIRBRIDGE_MEDIA_ENGINE_ROOT)"
	test -f "$(AIRBRIDGE_MEDIA_ENGINE_ROOT)/usr/share/airbridge-media/build-manifest.json"
	mkdir -p $(PKG_BUILD_DIR)
endef
define Build/Compile
endef

define Package/airbridge-media-engine/install
	$(CP) $(AIRBRIDGE_MEDIA_ENGINE_ROOT)/. $(1)/
endef

$(eval $(call BuildPackage,airbridge-media-engine))
