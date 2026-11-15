# Third-party notices
## Driver source and binary
`openwrt/drivers/ma86xh/vendor/rtw89-8019af4b9ed57a9042e139432b88687770f155b5.tar.gz` is the retained source snapshot from [morrownr/rtw89](https://github.com/morrownr/rtw89), pinned to the commit in its filename. It contains its original SPDX notices and Realtek firmware license. The package recipe declares `GPL-2.0-only BSD-3-Clause LicenseRef-Realtek-Firmware`; individual files retain their upstream terms.

The committed adaptation script and Kbuild describe the modifications used for the signed r6 package. The source snapshot includes `firmware/LICENCE.rtlwifi_firmware.txt` and `firmware/rtw8852c_fw-2.bin`. The signed prebuilt APK also installs that firmware/license. No proprietary firmware is relicensed as project code.

The original source archive is not rewritten merely to update AirBridge.Pi branding; its integrity and license notices must remain intact. Package/module names and signatures are compatibility/provenance artifacts.

## Archived application and designs
`legacy/esp32/` preserves the prior project and its references to ESP-IDF, Arduino, TinyUSB and LovyanGFX. Those dependencies are not vendored by this transition and have their own licenses when downloaded. Historical README license descriptions are retained as historical material, not a new license grant for third-party dependencies.

## Native media runtime and kernel add-ons
The optional media runtime builds mpv, Raspberry Pi FFmpeg, Mesa, libdrm, ALSA, subtitle/font libraries and other dependencies enumerated in `media/engine/sources.json` and pinned by archive hash in `sources.lock.json`. These are separate upstream works; the AirBridge.Pi name does not relicense them. Exact configured features, dependencies and source revisions affect redistribution obligations; retain corresponding sources/build instructions and applicable upstream notices when distributing packages.

`media/engine/stage.py` collects top-level COPYING/LICENSE/LICENCE/NOTICE files from extracted upstream sources into `/usr/share/airbridge-media/licenses/` in the runtime payload. Automatic top-level collection is not a substitute for reviewing all component, embedded-data, font and transitive-dependency license requirements before distribution. The build/artifact manifests and package recipe preserve provenance, not a legal compliance certification.

The exact-release HEVC driver recovery patches and recovered HDMI-codec source retain their upstream notices. License material is included under `media/kernel/LICENSES/` and `media/kernel/hdmi-codec/COPYING`; the corresponding recovery guides and provenance records explain source identity and patch closure. Never strip upstream copyright/SPDX headers from recovered code.

## Keys and private information
Only **public verification keys** for driver/media packages are tracked. They grant no signing authority. Private signing keys, router configurations, passwords and full storage images are excluded.
