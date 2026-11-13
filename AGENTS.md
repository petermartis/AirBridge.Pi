# AirBridge.Pi — instructions for future development sessions
## Read first
Read `README.md`, `docs/STATUS.md`, `docs/HARDWARE.md`, `docs/DEVELOPMENT.md`, and `docs/TROUBLESHOOTING.md`. For Wi-Fi failures read `openwrt/drivers/ma86xh/README.md` before making changes. Treat `legacy/esp32/` as archived reference, never as the active CM5 wiring or application.

## Canonical workspace
- GitHub: `https://github.com/petermartis/AirBridge.Pi`; primary branch `main`.
- Current Mac checkout: `/Users/pmartis/AirBridge.Pi`, not Desktop.
- Active application: `openwrt/lcd.c`, `telemetry.sh`, `airbridge-lcd.init`.
- `make check` is the primary hardware-free validation command.
- No agent framework, Arduino toolchain, framebuffer or Python runtime is required on the router.

## Device access and safety
- Last management address: `192.168.1.185`, Ethernet MAC `98:fe:54:26:85:72`; DHCP can change the address.
- SSH as root using strict host-key verification. If no live control socket/key exists, let the user enter the password privately. Never put passwords into commands, source, docs or logs.
- Do not print `/etc/config/wireless`, `uci show wireless`, `ubus call network.wireless status`, private hostapd files or recovery archives: these may expose credentials. Read targeted nonsecret fields or `iw dev` instead.
- No kernel/OpenWrt/wireless upgrades, EEPROM changes, disk formatting or reboots without an explicit need and coordinated user approval.
- Preserve the SD card, management bridge, 80 MHz default and custom driver. Never assume NAT is active.
- Router reachability failed on 2026-11-13. Recheck connectivity; do not equate that with hardware failure or assume today's state from this file.

## Hardware invariants
- SPI0 display CE0/GPIO8; DC GPIO24; RESET GPIO25.
- Discover the RP1 GPIO chip via metadata, not a hardcoded `/dev/gpiochipN`; the old device used `gpiochip15`.
- GPIO **v2** line metadata/requests are required on this kernel. GPIO-v1 line-info calls failed.
- Preserve **1,000,000 Hz**, explicit per-row address windows, `--rotation 90 --geometry 320x240`. Faster streaming produced interlaced/shifted output; using only inferred 240×320 dimensions left an untouched strip.
- Readback ID `00 00 00 00` was observed even with a visibly working panel. Do not treat successful writes or that ID as proof of controller identity.
- Touch remains unimplemented; do not silently replace `spi0-1cs` or claim calibration. Proposed CE1/GPIO7 and IRQ/GPIO17 wiring is in `docs/HARDWARE.md`.

## Wi-Fi invariants
- OpenWrt 25.12.6; kernel 6.12.112; ABI `294a8151df6d94c4f262106e0654471f`; mac80211 backports 6.18.55-r2.
- Production package is signed **r6**, not one of the intermediate diagnostic builds.
- `/etc/modules.d/60-ma86xh-rtw89` loads `rtw89_core_git abtx_repair_route=1 abtx_trace=0` before the USB module in `61-ma86xh-rtw89-usb`.
- Repair must be `Y`, tracing `N`. Never fabricate ACK/TX completion or weaken SAE/PMF to hide a driver problem.
- Firmware/hash, repair register details, restoration and rebuild constraints are in the driver guide.

## Validation and change discipline
Run `make check`; cross-compile hardware changes against the matching SDK, and use AddressSanitizer/UBSan on the host build when available. Software render tests do not verify SPI waveforms or the glass: ask for visible confirmation/photo for timing/orientation changes.

Inspect Git state before editing; preserve unrelated work. Do not commit/push unless requested. Do not include secrets, SDKs, build containers, SD images or raw device backups. Keep operational identifiers stable unless explicitly migrating them. New code may improve the UI, but do not expand into router topology or touch activation without a separate reviewed scope.

## External local artifacts
`/Users/pmartis/.local/state/openwrt-builds/ma86xh-25.12.6/` contains the retained SDK, source and old diagnostic artifacts. Docker container `ma86xh-diag-20261106` previously held SDK `/build`, pinned driver tree `/driver`, and a **private signing key**. Do not inspect/copy/publish that key. Neither this directory nor the container is guaranteed to exist in a new environment.

Essential source/recipe/signed-r6/public-key recovery files are now tracked in this repository. SDK fingerprint and fresh/retained environment instructions are in the driver guide. Private router recovery files under `/root/ma86xh-backup-20261106/` and `/root/airbridge-lcd-backup/` stay on-device; never commit them.
