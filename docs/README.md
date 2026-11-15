# AirBridge.Pi documentation
- [Current status and next work](STATUS.md): what was actually tested, environment pins and unfinished features.
- [Architecture](ARCHITECTURE.md): LCD/telemetry, HDMI control and direct-to-media upload data flows, trust boundaries and separation from networking.
- [Hardware and wiring](HARDWARE.md): CM5/carrier, display, planned touch and electrical cautions.
- [Development/deployment](DEVELOPMENT.md): tests, cross-build, install, service management and rollback.
- [Troubleshooting/recovery](TROUBLESHOOTING.md): connectivity, LCD, Wi-Fi, playback/profile issues, upload/delete failures and SD/NVMe recovery.
- [Custom Wi-Fi driver](../openwrt/drivers/ma86xh/README.md): TX-report repair, package restoration, provenance and rebuild.
- [HDMI media add-on](MEDIA.md): LuCI controls, signed packages, 4K evidence, media storage and build/recovery.
- [Future-session instructions](../AGENTS.md): mandatory context and safety constraints for another developer/agent.
- [Third-party notices](../THIRD_PARTY_NOTICES.md): redistributed source/firmware licensing.

The material under [`legacy/esp32/`](../legacy/esp32/) is archived. Its ST7789/CST816D, USB-tethering and dual-ESP32 concepts are **not** documentation for the present CM5 prototype.
