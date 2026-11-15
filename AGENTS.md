# AirBridge.Pi — instructions for future development sessions
## Read first
Read `README.md`, `docs/STATUS.md`, `docs/HARDWARE.md`, `docs/DEVELOPMENT.md`, and `docs/TROUBLESHOOTING.md`. For Wi-Fi failures read `openwrt/drivers/ma86xh/README.md` before making changes. Treat `legacy/esp32/` as archived reference, never as the active CM5 wiring or application.

## Canonical workspace
- GitHub: `https://github.com/petermartis/AirBridge.Pi`; primary branch `main`.
- Current Mac checkout: `/Users/pmartis/AirBridge.Pi`, not Desktop.
- Active application: `openwrt/lcd.c`, `telemetry.sh`, `airbridge-lcd.init`.
- `make check` is the primary hardware-free validation command.
- No agent framework, Arduino toolchain, framebuffer or Python runtime is required on the router.
- Media work is now implemented: read `docs/MEDIA.md`, `media/controller/README.md` and `media/kernel/README.md`. Run `make check-media` in addition to baseline checks. Native tests need host json-c/pkg-config, Python 3 and Node.

## Device access and safety
- Last management address: `192.168.1.185`, Ethernet MAC `98:fe:54:26:85:72`; DHCP can change the address.
- SSH as root using strict host-key verification. If no live control socket/key exists, let the user enter the password privately. Never put passwords into commands, source, docs or logs.
- Do not print `/etc/config/wireless`, `uci show wireless`, `ubus call network.wireless status`, private hostapd files or recovery archives: these may expose credentials. Read targeted nonsecret fields or `iw dev` instead.
- No kernel/OpenWrt/wireless upgrades, EEPROM changes, disk formatting or reboots without an explicit need and coordinated user approval.
- Preserve the SD card, management bridge, 80 MHz default and custom driver. Never assume NAT is active.
- Router access was restored during the 2026-11-13/14 media work. Recheck connectivity and current state rather than assuming this snapshot is current.

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

## Media invariants and storage
- Existing kernel was not upgraded. `rpi-hevc-dec.ko` and `snd-soc-hdmi-codec.ko` are exact-release external add-ons; use recovered source/fingerprint guards, never latest unrelated drivers or forced vermagic.
- Approved boot overlay is `vc4-kms-v3d-pi5,cma-512`, alongside the original SPI overlay. Explicit activation loads HDMI codec before stock VC4 and starts the media service idle. Do not silently enable autoload on another device before validation.
- Use the validated R3 zero-copy mapping: `gpu-hwdec-interop=drmprime-overlay`, `drm-draw-plane=overlay`, `drm-drmprime-video-plane=primary`, `drm-format=xrgb8888`, with Mesa VC4/V3D and the built-in fast profile. The original default plane ordering failed atomic commits because VC4 overlays cannot use zpos 0; swapping plane roles fixed it. Do not revert to that failed default or reduce video resolution to hide drops.
- Software-decoded video uses `dither=fruit` and the fixed `dark-chroma.glsl` shader after `profile=fast` (package r7, 2026-11-15). The shader must stay limited to SD sources: any extra pass on 1080p dropped frames. Keep bilinear scaling: spline36 and mitchell dropped frames at 4K. Never expose shader selection through the API.
- Controller r10 (installed 2026-11-15) picks the display mode per file and verifies the really open mode through `display-width`/`display-height`/`display-fps` (the `drm-mode` option alone is not proof: a still-open output keeps its old mode): `quality4k` sets `3840x2160@30`; Balanced/Quiet use 1080p at 24/25/30 Hz matched to the file's frame rate and reopen the output via the DRM-unused `wid` option. Keep the software path at 1080p ≤30 Hz: on this TV/cable 4K GPU-drawn output flashed and 1080p50 distorted picture and audio (cause unresolved: cable, TV port and the 297 MHz rate were ruled out on 2026-11-15, 1080p120 is clean, and the TV's SCDC error counters proved unreliable; the fault follows a moving 3840×2160 picture, including hardware HEVC under `quality4k`). Never hold the display closed with `vid=no` (video-only files fail), and never unpause before the file is loaded and the output configured.
- LuCI: Services → AirBridge.Pi Media; `airbridge.media` is a narrow authenticated API, not a raw mpv command proxy. Socket stays private. Both processes still use diagnostic root privileges; dedicated-UID hardening remains unfinished.
- File management is shipped as backend R5/UI R4. Public storage/remove/upload_begin/upload_cancel methods are narrow media-only operations; binary POSTs use `/cgi-bin/airbridge-media-upload`, existing-session permission checks and session-bound opaque capabilities. Never grant generic `file.write`/exec access or put credentials in URLs/logs. Bulk I/O/fsync must stay outside the playback event loop.
- Preserve upload containment: only a dedicated mounted ext4 media filesystem (not root/boot/tmpfs), no symlinks/cross-mount paths, private staging, 256 MiB reserve, no-overwrite publication, Stop before deleting held playback files. Retain canceled claimed FDs until worker acknowledgement/lease expiry to prevent FD-number reuse races.
- Installed LuCI JSON-stringifies a direct Blob and appends cache-busting query parameters by default: keep the raw-body callback and `cache:true` URL-preservation option. CGI responses remain `Cache-Control:no-store`; uhttpd requires `Status: CODE REASON`, not a bare numeric status.
- SD layout now has unchanged boot start/ID, a 512 MiB p2 containing a 128 MiB ext4 root, and p3 (~58.6 GiB mounted) at `/mnt/media` by UUID. Root has only ~22 MiB free. Do not assume 512 MiB partition capacity means the root filesystem grew to that size.
- Root growth hit the known OpenWrt reserved-GDT bug. Do not repeat online resize, repair mounted filesystems, bypass detection through loop aliases, or format root. Only the explicitly approved new p3 was formatted.
- HEVC Main/Main10 4K30 decode/direct scanout was measured: both added zero decoder/display drops in the ~20-second steady windows; 1–2 startup display drops remain. User confirmed the clean Main10 motion is smooth without flashes. See `media/validation/cm5-20261114-optimization.json`. This is not a feature-film/4K60/HDR or every-codec/profile guarantee.
- Default `quality4k` intentionally rejects software decoding. Use `balanced` for H.264/VP9/AV1 or audio-only files; it still allows supported hardware decoding. Profile and queue are in-memory and reset on controller restart. The 2026-11-15 `1080p-h264-fallback.mp4` failure is open: Balanced was recommended based on a previous successful test, but live diagnosis/retry remains unconfirmed after SSH authentication expired. Do not describe the latest report as fixed.

## External local artifacts
`/Users/pmartis/.local/state/openwrt-builds/ma86xh-25.12.6/` contains the retained SDK, source and old diagnostic artifacts. Docker container `ma86xh-diag-20261106` previously held SDK `/build`, pinned driver tree `/driver`, and a **private signing key**. Do not inspect/copy/publish that key. Neither this directory nor the container is guaranteed to exist in a new environment.

Essential source/recipe/signed-r6/public-key recovery files are now tracked in this repository. SDK fingerprint and fresh/retained environment instructions are in the driver guide. Private router recovery files under `/root/ma86xh-backup-20261106/` and `/root/airbridge-lcd-backup/` stay on-device; never commit them.

Media build workspace/container: `/Users/pmartis/.local/state/airbridge-media-build/`, `airbridge-media-build-20261113`. Signed APKs/public key/manifests are in its `artifacts/`; **private key is in `signing/private.pem` and must never be read or published**. A byte-verified initialized-SD backup is under `/Users/pmartis/.local/state/airbridge-media-private-backups/20261113-221647/`, outside Git, mode-restricted. It predates p3: do not blindly restore its partition table over the new media layout/data.
