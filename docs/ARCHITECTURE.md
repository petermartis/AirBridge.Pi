# Active architecture
## Hardware and network
The CM5 runs OpenWrt from SD. The CMX2 exposes Ethernet through its daughterboard, GPIO/SPI and USB. The MA86XH is the active 6 GHz AP adapter; the SPI panel is a local status surface.

The last deployment bridges Ethernet and AP traffic on `br-lan`; upstream DHCP supplies the management IPv4 address. OpenWrt/hostapd owns network configuration. The application must not assume NAT, separate client DHCP, USB tethering or internet reachability merely because Ethernet carrier is up.

## Application data flow
1. `openwrt/telemetry.sh --loop` discovers the AP using `iw dev`, obtains AP/channel/station information, queries only LAN status through `ubus`/`jsonfilter`, and checks Ethernet carrier and AP bridge membership.
2. It writes tab-separated status atomically to `/var/run/airbridge-lcd/status`, using restrictive permissions. No wireless key is queried or persisted.
3. `openwrt/lcd.c` reads that file, obtains CPU and memory data from procfs and renders a small RGB565 buffer.
4. A Linux spidev writer sends initialization commands and explicitly addressed pixel rows to CE0; GPIO character-device v2 controls DC/reset.
5. `openwrt/airbridge-lcd.init` registers separate collector and display processes with procd, with bounded respawn behavior.

`ETH UP` means carrier only. `AP UP` means an AP interface was found, not successful client authentication or internet access. `BRIDGED` comes from AP bridge membership, not an assumed firewall policy. Client count comes from station enumeration; visible entries require signal data and are capped at six. IP addresses for clients are not fabricated because leases belong to upstream DHCP.

## Status file contract
Each line is a key, tab, value. Keys: `SSID`, `IP`, `ETH`, `AP`, `MODE`, `RADIO`, `COUNT`, repeated `CLIENT`. Unknown keys are ignored. A missing or older-than-15-seconds file yields unavailable/stale indications; collection interval is about three seconds. Rendering updates roughly every two seconds plus transfer time.

The password field deliberately says `HIDDEN`. Do not add credential collection to logs or to this status file.

## Renderer and device interface
The renderer has an internal bitmap font, bounded text/rectangles, RGB565 encoding and PPM output for hardware-free tests. Rotation and viewport are independent CLI options because the visually verified hardware combination does not match a naive dimension swap.

GPIO discovery searches chip labels and line names: `pinctrl-rp1`, `GPIO24`, `GPIO25`. It refuses ambiguity and obtains exclusive line ownership. A process lock prevents competing renderer instances. Kernel SPI arbitration handles the bus; future touch should use a distinct chip-select, never reuse CE0.

There is no Linux framebuffer dependency, desktop compositor, LVGL, Python runtime on-device or touch event loop in the current implementation.

## Custom wireless driver
The kernel package is separate from the dashboard. `adapt-driver.py` adjusts upstream callbacks to the installed backports and adds gated diagnostic metadata plus a guarded report-route fix. Repair changes only the report-route mask, not authentication policy or fabricated completion status. See the [driver guide](../openwrt/drivers/ma86xh/README.md).

## Legacy boundary
`legacy/esp32/` contains the previous Arduino/ESP-IDF/LovyanGFX implementation, USB ECM/NCM patches and speculative ESP32-S3/C5 CAD. None of it is built by the primary Makefile or used by the CM5 service. Archiving preserves engineering history without claiming those designs are the present appliance.
