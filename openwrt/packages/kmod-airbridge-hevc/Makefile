include $(TOPDIR)/rules.mk

PKG_NAME:=airbridge-hevc
PKG_VERSION:=1.0.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-only
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

# Copy this recipe into the matching SDK and explicitly provide the repo path.
AIRBRIDGE_MEDIA_SOURCE ?= $(CURDIR)/../../..
HOST_PYTHON3 ?= python3
AIRBRIDGE_HEVC_KERNEL:=6.12.112
AIRBRIDGE_HEVC_ABI:=294a8151df6d94c4f262106e0654471f

include $(INCLUDE_DIR)/kernel.mk
include $(INCLUDE_DIR)/package.mk

# Metadata enumeration uses placeholders; real builds must use this exact SDK.
# Never override LINUX_VERMAGIC or force module loading to bypass these checks.
ifneq ($(DUMP),1)
  ifneq ($(BOARD)/$(SUBTARGET),bcm27xx/bcm2712)
    $(error airbridge-hevc requires the bcm27xx/bcm2712 SDK)
  endif
  ifneq ($(LINUX_VERSION),$(AIRBRIDGE_HEVC_KERNEL))
    $(error airbridge-hevc requires kernel $(AIRBRIDGE_HEVC_KERNEL))
  endif
  ifneq ($(LINUX_VERMAGIC),$(AIRBRIDGE_HEVC_ABI))
    $(error airbridge-hevc requires unchanged ABI $(AIRBRIDGE_HEVC_ABI))
  endif
  ifneq ($(LINUX_RELEASE),1)
    $(error airbridge-hevc requires kernel release 1)
  endif
endif

define KernelPackage/airbridge-hevc
  SUBMENU:=Video Support
  TITLE:=AirBridge.Pi exact-release Raspberry Pi HEVC decoder
  DEPENDS:=@TARGET_bcm27xx_bcm2712 +kmod-video-core +kmod-video-mem2mem +kmod-video-dma-contig
  FILES:=$(PKG_BUILD_DIR)/driver/rpi-hevc-dec.ko
  # No KCONFIG change: external obj-m build against the existing prepared tree.
  # KernelPackage adds its normal exact kernel version/ABI dependency.
  # No /etc/modules.d entry; loading requires a separate approved device trial.
  AUTOLOAD:=
endef

define KernelPackage/airbridge-hevc/description
  Exact OpenWrt 25.12.6 downstream rpi-hevc-dec sources, recovered from
  four hash-pinned patches at revision 981c978eff. External-module-only build
  for Linux 6.12.112, ABI 294a8151df6d94c4f262106e0654471f. Does not rebuild,
  configure or replace the kernel. Not a claim of validated 4K playback.
endef

define Build/Prepare
	$(HOST_PYTHON3) "$(AIRBRIDGE_MEDIA_SOURCE)/media/kernel/recover.py" recover \
		--cache "$(AIRBRIDGE_MEDIA_SOURCE)/media/kernel/patches" \
		--output "$(PKG_BUILD_DIR)/driver"
endef

define Build/Configure
endef

define Build/Compile
	$(HOST_PYTHON3) "$(AIRBRIDGE_MEDIA_SOURCE)/media/kernel/check_kernel.py" \
		--kernel-tree "$(LINUX_DIR)"
	$(KERNEL_MAKE) KCONFIG_NOSILENTUPDATE=1 M="$(PKG_BUILD_DIR)/driver" modules
	$(HOST_PYTHON3) "$(AIRBRIDGE_MEDIA_SOURCE)/media/kernel/check_kernel.py" \
		--kernel-tree "$(LINUX_DIR)"
endef

define KernelPackage/airbridge-hevc/install
	$(INSTALL_DIR) $(1)/usr/share/airbridge-media/kernel
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/driver/.source-provenance.json \
		$(1)/usr/share/airbridge-media/kernel/source-provenance.json
	$(INSTALL_DATA) "$(AIRBRIDGE_MEDIA_SOURCE)/media/kernel/LICENSES/GPL-2.0" \
		$(1)/usr/share/airbridge-media/kernel/COPYING
endef

$(eval $(call KernelPackage,airbridge-hevc))
