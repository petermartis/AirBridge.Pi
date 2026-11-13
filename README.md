# AirBridge.Pi
A portable Wi-Fi appliance built around a **Raspberry Pi Compute Module 5 Lite**, **OpenWrt**, a **Mercusys MA86XH USB Wi-Fi 6E adapter**, and a **2.8-inch SPI status display**. The earlier ESP32 approach did not meet the required performance; its sources and hardware concepts are preserved under [`legacy/esp32/`](legacy/esp32/).

Repository: **https://github.com/petermartis/AirBridge.Pi**

## Working baseline
- CM5 Lite, 4 GB RAM, Terioto BentoIO CMX2 carrier, SD-card boot.
- OpenWrt **25.12.6**, kernel **6.12.112**, target `bcm27xx/bcm2712`.
- Custom, ABI-pinned MA86XH driver with a validated firmware TX-report routing repair.
- SSID `AirBridge`, Slovakia, 6 GHz channel 37, **80 MHz**, WPA3-SAE with required PMF.
- Full-screen LCD dashboard: CPU/RAM, SSID, management IP, Ethernet/AP status, bridge mode, client MAC addresses and signal.
- User-confirmed display settings: **1 MHz**, explicitly addressed scanlines, **90° rotation plus a 320×240 viewport**.
- Display and telemetry managed by OpenWrt `procd`, with startup enabled on the last verified deployment.

These are recorded observations, not a claim that a fresh OpenWrt installation has all this functionality. See [current status](docs/STATUS.md) for dates, limitations and unverified work.

## Important boundaries
The current network is an **Ethernet/Wi-Fi LAN bridge with DHCP-client management**, not yet an isolated travel-router WAN/LAN/NAT design. The LCD does not configure networking. Touch has a proposed pinout but is **not implemented, calibrated or verified**. NVMe migration was abandoned after initialization failures; keep the working SD card.

**Do not upgrade the kernel or wireless stack without rebuilding and validating the custom Wi-Fi driver.** Do not publish router configuration backups, passwords or private signing keys.

## Start here
1. Read [`AGENTS.md`](AGENTS.md) and [status](docs/STATUS.md) before modifying an existing device.
2. Use the [hardware/pinout guide](docs/HARDWARE.md), not the archived ESP32 schematics.
3. Follow [development and deployment](docs/DEVELOPMENT.md) for building/testing the active code.
4. Use the [troubleshooting runbook](docs/TROUBLESHOOTING.md) and [Wi-Fi driver guide](openwrt/drivers/ma86xh/README.md) for recovery.
5. The [documentation index](docs/README.md) explains the remaining entry points.

## Local development
Requirements for hardware-free checks: a C compiler, Make and Python 3. No Docker, router access or Python third-party packages are needed.

```sh
cd ~/AirBridge.Pi
make check
```

The renderer also runs on macOS without hardware access:

```sh
./openwrt/airbridge-lcd --test --rotation 90 --geometry 320x240 --render /tmp/airbridge-pi.ppm
```

Cross-compilation requires a compatible OpenWrt SDK; see the development guide. Never deploy the macOS/native-test executable to the router.

## Repository map
- `openwrt/`: active Linux SPI/GPIO renderer, telemetry collector, procd service, tests and cross-build helper.
- `openwrt/drivers/ma86xh/`: custom driver recipe, adaptation script, source snapshot, signed recovery package, public verification key and checksums.
- `docs/`: CM5 architecture, wiring, development, operational state and recovery.
- `tools/`: repository integrity/link/safety checks.
- `legacy/esp32/`: archived Arduino/ESP-IDF application and speculative dual-ESP32 CAD. Not the current platform.

## Branding and compatibility
The project and repository are named **AirBridge.Pi**. The deployed SSID `AirBridge`, service `airbridge-lcd`, module names and package identifiers remain stable compatibility interfaces; renaming them would require a separate router migration. The source dashboard title uses the new project name; an already-running router retains its installed binary until explicitly redeployed.

## Licensing
See [third-party notices](THIRD_PARTY_NOTICES.md). The retained driver source, firmware and signed package carry their own upstream licenses. No private signing key is included.
