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

## Private artifact locations on the prototype
- `/root/ma86xh-backup-20261106/`: original configs, old packages, traces and saved bandwidth configurations.
- `/root/airbridge-lcd-backup/`: original boot config and original display binary.

These may be absent on a replacement device and may contain credentials. Inspect targeted nonsecret files only; do not copy them into the public repository.

## What to report after troubleshooting
Record code commit, device/kernel/ABI, exact commands, checks that passed, what was visually confirmed, and remaining uncertainty. Distinguish service-startup enabled from a tested cold boot; distinguish host renderer tests from verified glass output. Update `STATUS.md` and the relevant runbook instead of relying on chat memory.
