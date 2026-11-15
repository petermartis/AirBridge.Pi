# Troubleshooting and recovery
## First capture facts, not credentials
Confirm the current wiring/software branch and ask what changed. Record timestamps and exact errors. Commands below do not need wireless passwords:
```sh
uname -a
cat /etc/openwrt_release
cat /proc/cmdline
df -h / /boot
iw dev
ubus call network.interface.lan status
ubus call service list '{"name":"airbridge-lcd"}'
vcgencmd get_throttled
```
Do not dump full wireless configuration, hostapd files, private keys or raw config backups. Logs can contain addresses; redact deliberately before sharing.

## Router unreachable / SSH authentication
Last IPv4 was `192.168.1.185`, assigned by upstream DHCP. Check power, Ethernet link/daughterboard seating, and the main router's lease for MAC `98:fe:54:26:85:72`. A timeout is not a bad password. A password rejection is not a dead router. Reboots expire SSH multiplexed sessions; establish a fresh login privately.

Previously used multiplexing:
```sh
ssh -o StrictHostKeyChecking=yes -o ControlMaster=auto -o ControlPersist=30m \
  -o ControlPath=/tmp/airbridge-cm5-%C root@192.168.1.185
```
Never disable host-key checking just to suppress an unexpected identity change; verify why the key changed.

## LCD dark versus white versus scrambled
- **Completely dark:** inspect backlight, VCC/common ground and header orientation first. The direct LED supply is not controlled by the program. Power off before rewiring; measure VCC/LED against LCD ground with a meter if available. Do not guess a 5 V supply.
- **Backlit white:** power is present but initialization/data may be absent. Check CE0, MOSI, SCK, DC and reset, then whether SPI0 is enabled.
- **Interlaced/shifted/unreadable:** preserve the verified 1 MHz clock and per-row address windows. The former 8 MHz continuous-frame approach was not readable. Root cause was not isolated between timing and stream framing; do not claim one proven electrical cause.
- **Right-edge strip of old pixels:** ensure the service includes `--rotation 90 --geometry 320x240`. Inferring 240×320 from rotation left part of this module untouched.
- **Rotated/mirrored:** inspect current flags and a visible test pattern before changing wiring. Keep geometry explicit; a generic library rotation convention may not match this module.
- **All-zero ID:** not conclusive; it occurred with verified good output. The exact controller remains uncertain. A genuinely different nonzero ID is rejected by the program; investigate rather than forcing it.

## Display service and GPIO
```sh
ls -l /dev/spidev*
/usr/bin/airbridge-lcd --probe
ubus call service list '{"name":"airbridge-lcd"}'
cat /var/run/airbridge-lcd/status
logread -e airbridge-lcd
```
Stop the service before a foreground diagnostic; otherwise its GPIO ownership/process lock should reject a competing renderer. Do not hardcode gpiochip15: inspect `pinctrl-rp1` and `GPIO24/GPIO25` names. The other chips include Bluetooth lines at the same numeric offsets and must not be driven by accident.

GPIO-v1 metadata ioctls failed on the deployed OpenWrt kernel. Use v2 metadata/line requests. The `spidev10.0` node is not a replacement for the GPIO-header SPI0 panel.

If status is stale/missing, check the collector instance and run `/usr/libexec/airbridge-lcd-telemetry --once` to view nonsecret status. `iw`/`ubus`/`jsonfilter` are expected. The script discovers the AP, because `phyN-ap0` names can change.

Minimal OpenWrt did not have `nohup` or Python. Use procd for persistence. Do not install a Python runtime just to run this native renderer.

## Wi-Fi visible but client authentication fails
The historical MA86XH failure was missing genuine firmware TX completion reports, not an iPhone password problem. USB submission completed and frames were queued, but firmware reports were absent. The validated repair routes special reports to the WLAN CPU. Read the [driver runbook](../openwrt/drivers/ma86xh/README.md).

Check:
```sh
cat /sys/module/rtw89_core_git/parameters/abtx_repair_route
cat /sys/module/rtw89_core_git/parameters/abtx_trace
cat /etc/modules.d/60-ma86xh-rtw89
cat /etc/modules.d/61-ma86xh-rtw89-usb
iw dev
```
Expected `Y`, `N`, core module options first, USB module second. Preserve SK/6 GHz/channel 37/80 MHz/SAE/required PMF. Confirm actual client authorization and traffic, not merely beacon visibility. Do not spoof ACKs, weaken PMF or upgrade the kernel as a shortcut.

## Storage / boot / private backups
The SD install remains authoritative and small-root-space limited. Check `df` before package work. Save a private OpenWrt config archive before a risky change, with restrictive permissions, and keep it out of Git; it contains secrets. The committed driver APK is not a full router image or replacement for personal network configuration.

The NVMe attempt never cloned or formatted a disk and never changed EEPROM priority. SSD zero capacity/dead state, failed resets and a missing Mac disk do not conclusively isolate SSD versus connection/adapter failure. Do not format a zero-capacity or unreliable target. No further migration is authorized by this documentation.

Last boot order was `0xf2461` (SD before NVMe). CM5 EEPROM programming differs from ordinary Raspberry Pi boards; verify a supported CM5 procedure before changing it. Keep SD recovery intact. An SSD kernel/runtime failure is not guaranteed to fall back automatically.

## Media playback: collect state before changing policy
The UI is **Services → AirBridge.Pi Media**, `/cgi-bin/luci/admin/services/airbridge-media`. With an already authenticated device shell, capture targeted read-only facts:
```sh
ubus call airbridge.media status '{}'
ubus call airbridge.media storage '{}'
ubus call service list '{"name":"airbridge-media"}'
uci -q get airbridge-media.main.enabled
uci -q get airbridge-media.main.root
df -h / /boot /mnt/media
mount
apk info airbridge-media airbridge-media-engine luci-app-airbridge-media
ls -l /dev/dri /dev/snd
logread -e airbridge-media
```
Review/redact media titles and unrelated mount/log details before sharing. A successful ubus transport exit is not a successful operation: check response `ok` and `error`. `available:true` only means player IPC answered; it does not prove visible picture or audible sound.

### H.264 sample will not play
Default `quality4k` means **require hardware video decoding**, not “best quality for every file.” The CM5 has the validated HEVC hardware path, but this H.264 path is software-decoded. In the page select Profile **`balanced`**, wait for the control request to finish, then press Play again. `quiet` also permits software but limits threads to two. Balanced still tries supported hardware decoding; it does not disable HEVC acceleration.

`1080p-h264-fallback.mp4` previously played under Balanced with zero observed decoder/display drops. The user reported another failure on 2026-11-15; the strict profile is a likely cause, **not a newly verified diagnosis**. SSH authentication had expired, so current status/error could not be read and a successful retry has not been reported. If Balanced still fails, retain the exact UI/backend error and inspect codec/container, selected path, file completeness, service and mount rather than declaring it fixed.

Profile selection and the queue are not persisted: restarting the controller returns to `quality4k`. Audio-only files also cannot satisfy its hardware-video check. There is no automatic per-file profile switch. Do not silently weaken the strict profile or mislabel software decoding as hardware.

### Engine unavailable, no HDMI picture, or stutter
- **Unavailable:** check whether the add-on is enabled, both procd instances exist, the root mounted before controller startup, and private runtime/config dependencies are present. A newly installed package defaults disabled. Replacing/remounting the root requires a controlled service restart because descriptors are retained.
- **HEVC hardware rejected:** inspect exact-release module loading, V4L2 and DRM availability. `/dev/video19` was observed but numbering is not a universal contract. Do not force module ABI compatibility or install a newer kernel.
- **No picture/audio despite playing:** verify display input/cable/connector, another DRM owner, selected audio track, TV/player mute and ALSA HDMI mapping. Do not repeat old sysfs GPU-binding trials as routine recovery.
- **Stutter:** compare both `dropped_frames` (decoder) and `vo_dropped_frames` (presentation) across time. One or two startup display drops occurred in accepted runs; sustained increases are different. Preserve `drmprime-overlay`, video-primary/GUI-overlay mapping, full-size 4K and the 4K30 mode. The prior reversed plane assignment caused atomic commits to fail. Do not hide counters, downscale, overclock or change global governors to claim improvement.

See [the media runbook](MEDIA.md) for exact configuration, signed versions, module dependencies, benchmark methodology and rollback. Short HEVC clips do not establish 4K60, HDR, every codec or full-film thermal stability.

## Media upload/delete failures
- **Controls disabled:** both user write permission and backend storage readiness are required. Read-only sessions may browse/read storage but must not upload/delete. Engine availability and file-operation busy state are separate.
- **Storage unavailable/read-only:** inspect `storage.reason`, mount identity/options and free/usable space. Writes require the configured actual dedicated ext4 filesystem-root mount. Do not bypass the root/boot/tmpfs/submount guards or upload into an unmounted `/mnt/media` directory on root.
- **Insufficient space:** usable capacity excludes the 256 MiB reserve and outstanding logical reservations. There are two upload slots. Canceled in-flight workers may retain a slot until acknowledgment/lease expiry; inactivity expires at 900 seconds.
- **Duplicate:** publication never overwrites an existing filename. Refresh first, then choose a different local filename or explicitly delete the correct existing file after confirmation.
- **Cancel/disconnect/final error:** refresh the directory before retrying. Completed files remain, and a lost final acknowledgment or post-publication directory-sync error can leave a complete published file. Do not infer from a timeout that no file exists.
- **Delete says Stop first:** use Stop even if the clip already reached EOF; the controller still holds the inode. Directory/recursive delete, rename, folder creation and arbitrary path management are not implemented.
- **Unrecognized staging/quarantine:** preserve `.airbridge-upload` for investigation. Unknown entries and interrupted-delete quarantine can intentionally block writes. Never “fix” this by recursively deleting staging or user media.
- **Browser upload regression:** keep LuCI's Blob-returning content callback and query-free URL behavior (`cache:true`); native CGI replies need `Status: CODE REASON`, not bare status numbers. Backend R5/UI R4 contain these compatibility fixes. Never move bearer/session/capability values into a URL, browser DOM or logs.

No reboot, filesystem repair or general filesystem-write ACL is a routine response to an upload error. File/CGI transport details are in the [controller contract](../media/controller/README.md). Device write tests are opt-in and operate on generated fixtures only.

### SD capacity and recovery caveat
The root partition reserves 512 MiB but its filesystem remains about 128 MiB; media p3 supplies about 58.6 GiB. Root online growth hit the reserved-GDT issue. Do not rerun online resize, repair a mounted filesystem or use loop aliases to evade checks. The private initialized-SD backup predates p3: restoring its MBR blindly hides the current media partition. Preserve the current layout and separately review any offline repair/restoration.

## Private artifact locations on the prototype
- `/root/ma86xh-backup-20261106/`: original configs, old packages, traces and saved bandwidth configurations.
- `/root/airbridge-lcd-backup/`: original boot config and original display binary.

These may be absent on a replacement device and may contain credentials. Inspect targeted nonsecret files only; do not copy them into the public repository.

## What to report after troubleshooting
Record code commit, device/kernel/ABI, exact commands, checks that passed, what was visually confirmed, and remaining uncertainty. Distinguish service-startup enabled from a tested cold boot; distinguish host renderer tests from verified glass output. Update `STATUS.md` and the relevant runbook instead of relying on chat memory.
