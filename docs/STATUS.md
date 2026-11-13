# Operational state and handoff
## Evidence dates
- 2026-11-06: custom MA86XH r6 package and startup ordering validated; SD-to-NVMe migration stopped before copying or formatting.
- 2026-11-07: after a reboot, Wi-Fi repair returned as `Y`, tracing `N`, AP channel 37 at 80 MHz. User confirmed readable test pattern, correct rotation and full-screen coverage after timing/viewport fixes. Dashboard/collector ran under procd; startup symlinks were present.
- 2026-11-09: touch wiring was discussed; no touch software or hardware test was completed.
- 2026-11-13: repository renamed/moved and maintenance context consolidated. SSH to the last address timed out. No router modifications, reboot or new on-device verification belong to this repository-maintenance step.

Repository-transition validation: native renderer/CLI tests, Linux ASan/UBSan tests, ARM64 cross-build using the documented helper, Python/shell syntax, documentation links, recovery checksums, APK signature against the isolated public key, firmware identity and adaptation of a fresh source extraction passed. No clean-room full driver rebuild or archived firmware/CAD rebuild was performed.

## Last known platform
- CM5 Lite, 4 GB RAM; Terioto BentoIO CMX2; official Raspberry Pi 27 W USB-C supply.
- OpenWrt 25.12.6 / Linux 6.12.112 / ARM64 `aarch64_cortex-a76` / bcm27xx bcm2712.
- Kernel ABI `294a8151df6d94c4f262106e0654471f`; mac80211 backports 6.18.55-r2.
- SD `/dev/mmcblk0`, approximately 64 GB, remains the boot medium. Boot FAT partition approximately 64 MiB; ext4 root partition approximately 104 MiB, filesystem about 98 MiB. Root was not expanded to the entire SD.
- Original root `PARTUUID=fac83c25-02`; do not hardcode this in a replacement image without checking.
- Management `192.168.1.185` via DHCP on `br-lan`; upstream gateway observed at `192.168.1.1`. Ethernet MAC `98:fe:54:26:85:72`.
- Ethernet and Wi-Fi AP bridged on the LAN. This is not an isolated WAN/LAN travel-router configuration.

## Wi-Fi baseline
MA86XH: initial USB installer identity `0bda:1a2b`; `usb-modeswitch` exposes `2c4e:0127` / RTL8832CU. Custom module family `rtw89_*_git`; r6 package, guarded report-route repair enabled and trace disabled.

SSID `AirBridge`; regulatory country `SK`; 6 GHz channel 37; 80 MHz center 6145 MHz; observed TX power 23 dBm. Interface most recently `phy1-ap0`; earlier boots used different phy numbers, so discover rather than hardcode.

WPA3-SAE/CCMP, required PMF (`ieee80211w=2`), H2E (`sae_pwe=1`); hostapd antenna-pattern options `he_6ghz_rx_ant_pat=0`, `he_6ghz_tx_ant_pat=0`. A 160 MHz trial reached roughly 800 Mbps in the user's environment, but **80 MHz is the requested default**. This is an observation, not a performance guarantee.

## LCD baseline
2.8-inch board marked `TFT 240xRGBx320 V1.1`, SPI display with separate touch pins and SD slot. ILI9341-compatible initialization produced visually verified output; the exact controller was **not** positively identified by readback.

Service command:
`/usr/bin/airbridge-lcd --device /dev/spidev0.0 --rotation 90 --geometry 320x240 --speed 1000000`

Boot overlay `dtoverlay=spi0-1cs`; kernel package `kmod-spi-dev`. Initial 8 MHz/frame-stream writes were unreadable. Per-row address windows plus 1 MHz were readable. After rotation, an inferred 240×320 viewport left a right-hand strip; explicit 320×240 filled the glass. Preserve this empirically verified combination.

The source title has since been renamed to AirBridge.Pi. The physical router's older installed binary is not automatically changed by a Git commit/push.

## NVMe attempt: abandoned, not migrated
SSSTC `CL1-3D128-Q11 NVMe SSSTC 128GB`, firmware `22301116`, PCI `0001:01:00.0`: dead controller/zero capacity; reset rejected; driver rebind failed with `Device not ready; aborting reset, CSTS=0x1`. After reseating/cold power cycle it was not successfully validated. A Mac test did not expose a 128 GB disk, but adapter/connection failure was not ruled out.

No partitions were formatted/copied and no boot-order change was applied. User chose to remain on SD. Last observed EEPROM order `0xf2461` prefers SD before NVMe. Do not retry erase, migration, APST changes or EEPROM updates without a new explicit task.

## Next work, not completed
1. Re-establish router access and take a private backup before new device work.
2. Confirm touch wiring, enable CE1 safely alongside the existing display, identify/test the touch controller, then calibrate coordinate rotation. Likely XPT2046/ADS7846-compatible; not yet confirmed.
3. Decide desired touch interactions before adding configuration actions.
4. Design isolated WAN/LAN routing only as a separate project step; preserve management access.
5. Test a future cold boot of the final dashboard service when the user can tolerate an interruption. Startup is enabled, but no post-enablement reboot was performed.
