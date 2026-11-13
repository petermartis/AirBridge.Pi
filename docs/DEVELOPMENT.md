# Development and deployment
## Native tests
From the repository root:
```sh
make check
```
This compiles with `-Wall -Wextra -Werror`, tests dashboard/test-pattern rendering, missing/oversized status input, rotations/viewports and CLI validation, checks shell syntax and repository links/assets. It does not access LCD hardware. On macOS the renderer supports PPM output only.

```sh
./openwrt/airbridge-lcd --test --rotation 90 --geometry 320x240 --render /tmp/lcd.ppm
```

## ARM64 cross-build
Use a Linux host/container and the **matching** OpenWrt SDK. On this Mac the retained Docker container is `ma86xh-diag-20261106`, SDK `/build`, toolchain `toolchain-aarch64_cortex-a76_gcc-14.3.0_musl`. See the driver guide for the external SDK archive fingerprint.

```sh
OPENWRT_SDK=/path/to/matching-sdk sh openwrt/build-lcd.sh
```
The output is `openwrt/build/airbridge-lcd`, distinct from the native test binary. The helper discovers exactly one matching ARM64 musl compiler and refuses ambiguous/missing toolchains.

For the retained Mac container, explicitly copy the new checkout rather than relying on the old Desktop path:
```sh
docker start ma86xh-diag-20261106
docker cp openwrt ma86xh-diag-20261106:/tmp/airbridge-pi-openwrt
docker exec ma86xh-diag-20261106 sh -c \
  'OPENWRT_SDK=/build sh /tmp/airbridge-pi-openwrt/build-lcd.sh'
docker cp ma86xh-diag-20261106:/tmp/airbridge-pi-openwrt/build/airbridge-lcd openwrt/build/
```
Create the local output directory first if it does not exist. Docker and the SDK are not required for ordinary native tests.

For Linux memory checks, build `openwrt/lcd.c` with `-fsanitize=address,undefined -g -O1` and run `openwrt/test_renderer.py` against that executable. Renderer tests do not verify electrical timing.

## First installation: separate from normal UI deployment
1. Verify the actual router model/kernel/ABI and confirm a private backup and management recovery path.
2. Refresh signed indexes with `apk update`; preview `apk add --simulate kmod-spi-dev`. Install only the compatible package, never a kernel upgrade or `--allow-untrusted`.
3. Back up `/boot/config.txt` privately, then add only `dtoverlay=spi0-1cs` under `[all]` if CE1/touch is not configured.
4. Coordinate a reboot. Expect `/dev/spidev0.0`; inspect its actual device-tree mapping. Do not use `/dev/spidev10.0`, which is a different controller.
5. Check the wired DC/reset lines with `airbridge-lcd --probe`, which reads GPIO metadata without requesting pins.

The current prototype already completed this procedure; do not append duplicate overlays or reboot just to deploy a new UI.

## Update the LCD application
No live changes are made by `make check` or Git push. Build an ARM64 binary first. Set a nonsecret target:
```sh
export ROUTER=root@192.168.1.185
ssh -o StrictHostKeyChecking=yes "$ROUTER" 'uname -m; ls -l /dev/spidev0.0'
scp -O -o StrictHostKeyChecking=yes openwrt/build/airbridge-lcd \
  openwrt/telemetry.sh openwrt/airbridge-lcd.init "$ROUTER":/tmp/
```
`-O` selects legacy SCP for OpenWrt installations without an SFTP server. Authenticate interactively or with a properly managed SSH key; never embed a password.

On the router, stop the existing renderer before replacement, keep a private versioned backup of the three installed files, and then install:
```sh
/etc/init.d/airbridge-lcd stop
cp /tmp/airbridge-lcd /usr/bin/airbridge-lcd
cp /tmp/telemetry.sh /usr/libexec/airbridge-lcd-telemetry
cp /tmp/airbridge-lcd.init /etc/init.d/airbridge-lcd
chmod 755 /usr/bin/airbridge-lcd /usr/libexec/airbridge-lcd-telemetry /etc/init.d/airbridge-lcd
```
First install `/usr/libexec` if missing. Do not copy over a running binary or erase the only known-good backup.

Run the pattern in the foreground, visually verify it, and stop with Ctrl-C:
```sh
/usr/bin/airbridge-lcd --test --rotation 90 --geometry 320x240 --speed 1000000
```
Then start and inspect:
```sh
/etc/init.d/airbridge-lcd start
ubus call service list '{"name":"airbridge-lcd"}'
cat /var/run/airbridge-lcd/status
```
Enable only after visible confirmation: `/etc/init.d/airbridge-lcd enable`. Verify `/etc/rc.d/S99airbridge-lcd`; this proves configuration, not an actual successful subsequent boot.

## Installed paths
- `/usr/bin/airbridge-lcd`: native ARM64 renderer.
- `/usr/libexec/airbridge-lcd-telemetry`: shell collector.
- `/etc/init.d/airbridge-lcd`: procd service, two instances.
- `/var/run/airbridge-lcd/status`: ephemeral, atomic, nonsecret telemetry.
- `/var/run/airbridge-lcd.lock`: renderer lock.
- `/root/airbridge-lcd-backup/`: original boot config and old binary on the prototype; private and external to Git.

Service names are intentionally stable despite new project branding. The service pins rotation, geometry and speed explicitly; do not rely on CLI defaults.

## Rollback
Stop/disable the service; restore known-good application/helper/init files from the private backup and start it again. For boot-configuration recovery, remove only the added overlay line, or restore an old file only if no later legitimate boot edits would be lost. Reboot only with user approval. Leave Ethernet, WPA3 settings, firmware and Wi-Fi modules alone.

## Legacy application
Use `legacy/esp32/` as its project directory if investigating old PlatformIO/CMake code. That application is not built or tested by the primary build and cannot run on OpenWrt. Historical CAD is a concept archive, not a validated CM5 board.
