# AirBridge.Pi — CM5 / OpenWrt LCD dashboard
This is the active Linux port of the status screen archived in `legacy/esp32/src/display.cpp`. It uses an ILI9341-compatible initialization sequence on a 320×240 SPI panel; exact controller readback remains inconclusive. SPI0 CE0, GPIO24 for DC and GPIO25 for RESET are the working assignments. Touch is not enabled. See the [maintenance docs](../docs/README.md) and [current status](../docs/STATUS.md).
`--rotation 0|90|180|270` selects clockwise rotation relative to the original test image. The default is 90 degrees for the user's current mounting. Without an explicit viewport, quarter turns select 240×320. `--geometry 320x240|240x320` overrides this independently. Physical testing on this particular module confirmed that rotation 90 with an explicit 320×240 viewport fills the screen correctly; the deployed service pins both values rather than assuming the dimensions from rotation.

## Hardware
Use the carrier's standard Raspberry Pi GPIO header: SCK GPIO11/pin23, MOSI GPIO10/pin19, MISO GPIO9/pin21, CS GPIO8/pin24, DC GPIO24/pin18, RESET GPIO25/pin22, ground pin6. Confirm your carrier pinout. Power/backlight connections depend on the module; never apply 5 V to GPIO. Disconnect power before wiring.

The program discovers the RP1 GPIO chip using its label and GPIO24/GPIO25 line names. It refuses to use ambiguous chips or GPIO lines already owned by another consumer. It uses GPIO character-device v2 and conservative 1 MHz SPI, without direct register access or Python dependencies. Each scanline gets its own address window to prevent stream-alignment errors from propagating across the frame. `--speed Hz` overrides the clock for diagnostics.

## Build and test
Run `make -C openwrt` and `make -C openwrt test`. On macOS the binary supports software rendering only. Cross-compile with the matching OpenWrt SDK compiler via `CC`; use the SDK's `STAGING_DIR` environment variable. No kernel rebuild is required.

`airbridge-lcd --status fixture.txt --render preview.ppm` renders without hardware. `--test --render test.ppm` generates the test pattern. On Linux, `--probe` reports GPIO chip/line metadata without requesting lines. `--test` displays color bars until interrupted. The diagnostic attempts LCD ID register 0xD3; a blank/all-ones read is inconclusive, not evidence of a working LCD.

## Deployment
Install only the kernel-ABI-matching `kmod-spi-dev`; do not upgrade the kernel or the custom wireless modules. Back up `/boot/config.txt`, then add `dtoverlay=spi0-1cs` under `[all]`. Reboot only when the router can be interrupted. Expect `/dev/spidev0.0`; confirm actual enumeration before proceeding.

Install `airbridge-lcd` at `/usr/bin/airbridge-lcd`, `telemetry.sh` at `/usr/libexec/airbridge-lcd-telemetry` and `airbridge-lcd.init` at `/etc/init.d/airbridge-lcd`, with executable permissions. Run the test pattern first and visually verify orientation, colors and text. Stop it before starting the service.

Start with `/etc/init.d/airbridge-lcd start`. Enable startup only after physical verification with `/etc/init.d/airbridge-lcd enable`. The procd service starts an atomic status collector and the renderer; it does not configure networking.

Telemetry reads AP SSID, LAN IPv4, Ethernet carrier, actual bridge membership, radio channel/width, Wi-Fi stations and signal. CPU and available-memory percentages come from procfs. The password stays hidden and wireless keys are never queried. This is not a port of ESP32 NAT, USB tethering, web configuration or touch. Client IP addresses are not fabricated; this version displays MAC and signal because the upstream DHCP server owns leases.

## Rollback
Stop and disable `/etc/init.d/airbridge-lcd`. Remove only the added SPI overlay line, or restore the saved original boot configuration if no other edits have happened since the backup. Reboot at a convenient time. Leave existing Ethernet/Wi-Fi configuration and custom driver packages intact.
