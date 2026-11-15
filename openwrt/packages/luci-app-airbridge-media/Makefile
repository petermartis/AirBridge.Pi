include $(TOPDIR)/rules.mk

LUCI_TITLE:=AirBridge.Pi native HDMI media controls
LUCI_DEPENDS:=+luci-base +airbridge-media
LUCI_PKGARCH:=all
PKG_VERSION:=1.0.0
PKG_RELEASE:=4

PKG_LICENSE:=MIT

include $(TOPDIR)/feeds/luci/luci.mk

# Call BuildPackage through luci.mk; no engine, SDK or device changes here.
