include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=ma86xh-rtw89
PKG_VERSION:=20261105
PKG_RELEASE:=6
PKG_LICENSE:=GPL-2.0-only BSD-3-Clause LicenseRef-Realtek-Firmware
PKG_BUILD_DEPENDS:=mac80211

include $(INCLUDE_DIR)/package.mk

MODPARAMS.rtw89_core_git:=abtx_repair_route=1 abtx_trace=0

define KernelPackage/ma86xh-rtw89
  SUBMENU:=Wireless Drivers
  TITLE:=Mercusys MA86XH RTL8832CU USB driver with guarded report routing repair
  DEPENDS:=+kmod-mac80211 +kmod-usb-core +rtl8852ce-firmware
  AUTOLOAD:=$(call AutoLoad,60,rtw89_core_git)
  FILES:= \
	$(PKG_BUILD_DIR)/rtw89_core_git.ko \
	$(PKG_BUILD_DIR)/rtw89_8852c_git.ko \
	$(PKG_BUILD_DIR)/rtw89_usb_git.ko \
	$(PKG_BUILD_DIR)/rtw89_8852cu_git.ko
endef

define KernelPackage/ma86xh-rtw89/install
	$(INSTALL_DIR) $(1)/etc/modules.d
	printf '%s\n' 'rtw89_8852cu_git' > $(1)/etc/modules.d/61-ma86xh-rtw89-usb
	$(INSTALL_DIR) $(1)/lib/firmware/rtw89
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/firmware/rtw8852c_fw-2.bin $(1)/lib/firmware/rtw89/
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/firmware/LICENCE.rtlwifi_firmware.txt $(1)/lib/firmware/rtw89/
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) /driver/. $(PKG_BUILD_DIR)/
	$(CP) $(CURDIR)/Kbuild $(PKG_BUILD_DIR)/Kbuild
	python3 $(CURDIR)/adapt-driver.py $(PKG_BUILD_DIR)
endef

NOSTDINC_FLAGS := \
	$(KERNEL_NOSTDINC_FLAGS) \
	-I$(STAGING_DIR)/usr/include/mac80211-backport \
	-I$(STAGING_DIR)/usr/include/mac80211-backport/uapi \
	-I$(STAGING_DIR)/usr/include/mac80211 \
	-I$(STAGING_DIR)/usr/include/mac80211/uapi \
	-include backport/backport.h

define Build/Compile
	+$(KERNEL_MAKE) M="$(PKG_BUILD_DIR)" \
		NOSTDINC_FLAGS="$(NOSTDINC_FLAGS)" modules
endef

$(eval $(call KernelPackage,ma86xh-rtw89))
