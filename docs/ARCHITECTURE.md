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

## Native HDMI media subsystem
The media add-on is separate from the LCD and network collector. `/etc/init.d/airbridge-media` starts two procd instances: a fixed-argument player launcher and the native typed controller. The private mpv/FFmpeg/Mesa/ALSA runtime is under `/usr/lib/airbridge-media`; there is no desktop/compositor or new network listening service.

Control flow is authenticated browser → existing LuCI/uhttpd/rpcd → narrow `airbridge.media` methods → private mpv Unix socket. Browser inputs cannot specify arbitrary mpv commands, executable arguments, shell commands or URLs. The controller opens validated library-relative files through held directory descriptors and gives the player retained `/proc/<pid>/fd/<fd>` handles. `core.c` owns playback/policy; `ubus.c` is the native API adapter; `player.c` fixes launch options and environment.

The configured playback library is `/mnt/media`, currently dedicated SD p3. The optimized HEVC path uses the exact-release V4L2 decoder and DRM PRIME zero-copy scanout: decoded video on the VC4 primary plane and GUI on an overlay, preserving full 4K surfaces. H.264 needs software decoding on this CM5. Controller profile `quality4k` fails closed without active hardware decoding; `balanced` and `quiet` permit software fallback. Profiles and queues are controller-memory state, not persistent UCI settings; a controller restart restores `quality4k` and clears the queue.

### File-management data path and trust boundary
Uploads do not pass bulk data through ubus. The UI first requests an authenticated upload capability, then sends sequential raw chunks to the native CGI `/cgi-bin/airbridge-media-upload` under existing uhttpd. The CGI validates session permissions/ownership through private coordination calls before body reads, writes directly to the media filesystem and handles bulk synchronization outside the controller event loop. The controller tracks reservations, held descriptors, cancellation and atomic no-overwrite publication.

`files.c` implements storage/upload/delete state; `fs_mount.c` enforces the production Linux mount policy; `upload_protocol.c`, `upload_auth.c` and `upload_rpc.c` implement bounded capability/permission transport; `upload_cgi.c`, `upload_io.c` and `held_fd.c` handle the separate transfer worker. Test-only mount/RPC providers live under `media/controller/tests/` and must never enter target packages.

Writes require a dedicated actual ext4 filesystem-root mount on a nonvirtual block device distinct from root/boot/tmp, safe path traversal and unchanged mount identity. Partials are hidden on that filesystem, never staged in small-root `/tmp`; deletion is regular-file-only and refuses a held playback inode until Stop. There is no generic filesystem-write ACL. The private mpv socket, upload authorization and root diagnostic privileges remain sensitive; this is not a production codec-parser sandbox.

The [media runbook](MEDIA.md) records exact packages, startup ordering, storage geometry, security limits, deployment and measured evidence. The [controller contract](../media/controller/README.md) and [LuCI contract](../openwrt/packages/luci-app-airbridge-media/api-contract.json) specify the API. Software tests do not certify picture/audio, sustained performance or user permissions on another installation.

## Legacy boundary
`legacy/esp32/` contains the previous Arduino/ESP-IDF/LovyanGFX implementation, USB ECM/NCM patches and speculative ESP32-S3/C5 CAD. None of it is built by the primary Makefile or used by the CM5 service. Archiving preserves engineering history without claiming those designs are the present appliance.
