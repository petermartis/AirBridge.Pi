include $(TOPDIR)/rules.mk

PKG_NAME:=airbridge-hdmi-codec
PKG_VERSION:=1.0.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-only
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
AIRBRIDGE_MEDIA_SOURCE ?= $(CURDIR)/../../..
HOST_PYTHON3 ?= python3

include $(INCLUDE_DIR)/kernel.mk
include $(INCLUDE_DIR)/package.mk

ifneq ($(DUMP),1)
  ifneq ($(BOARD)/$(SUBTARGET),bcm27xx/bcm2712)
    $(error airbridge-hdmi-codec requires bcm27xx/bcm2712)
  endif
  ifneq ($(LINUX_VERSION),6.12.112)
    $(error airbridge-hdmi-codec requires kernel 6.12.112)
  endif
  ifneq ($(LINUX_VERMAGIC),294a8151df6d94c4f262106e0654471f)
    $(error airbridge-hdmi-codec requires unchanged release ABI)
  endif
  ifneq ($(LINUX_RELEASE),1)
    $(error airbridge-hdmi-codec requires kernel release 1)
  endif
endif

define KernelPackage/airbridge-hdmi-codec
  SUBMENU:=Sound Support
  TITLE:=AirBridge.Pi exact-release HDMI ASoC codec
  DEPENDS:=@TARGET_bcm27xx_bcm2712 +kmod-sound-soc-core
  FILES:=$(PKG_BUILD_DIR)/driver/snd-soc-hdmi-codec.ko
  # Existing CONFIG_SND_SOC_HDMI_CODEC=m; never change the kernel configuration.
  # Standard KernelPackage adds the exact kernel version/ABI dependency.
  AUTOLOAD:=
endef

define KernelPackage/airbridge-hdmi-codec/description
  Exact unmodified Linux 6.12.112 HDMI codec source. A scan of all 1368
  OpenWrt 25.12.6 generic/bcm27xx patches found no source/header modifications
  or source overlays. External-module-only recovery of a pruned module.
  No autoload, boot changes, kernel replacement or hardware validation claim.
endef

define Build/Prepare
	$(HOST_PYTHON3) "$(AIRBRIDGE_MEDIA_SOURCE)/media/kernel/hdmi-codec/recover.py" recover \
		--cache "$(AIRBRIDGE_MEDIA_SOURCE)/media/kernel/hdmi-codec/cache" \
		--output "$(PKG_BUILD_DIR)/driver"
endef

define Build/Configure
endef

define Build/Compile
	$(HOST_PYTHON3) "$(AIRBRIDGE_MEDIA_SOURCE)/media/kernel/check_kernel.py" --kernel-tree "$(LINUX_DIR)"
	$(KERNEL_MAKE) KCONFIG_NOSILENTUPDATE=1 M="$(PKG_BUILD_DIR)/driver" modules
	$(HOST_PYTHON3) "$(AIRBRIDGE_MEDIA_SOURCE)/media/kernel/check_kernel.py" --kernel-tree "$(LINUX_DIR)"
endef

define KernelPackage/airbridge-hdmi-codec/install
	$(INSTALL_DIR) $(1)/usr/share/airbridge-media/kernel/hdmi-codec
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/driver/.source-provenance.json \
		$(1)/usr/share/airbridge-media/kernel/hdmi-codec/source-provenance.json
	$(INSTALL_DATA) "$(AIRBRIDGE_MEDIA_SOURCE)/media/kernel/hdmi-codec/COPYING" \
		$(1)/usr/share/airbridge-media/kernel/hdmi-codec/COPYING
endef

$(eval $(call KernelPackage,airbridge-hdmi-codec))
