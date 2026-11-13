# MA86XH custom driver: preservation and recovery
## Exact compatibility baseline
- Device: Mercusys MA86XH / RTL8832CU, operational USB ID `2c4e:0127`.
- Installer-mode USB ID `0bda:1a2b`; official `usb-modeswitch` was required.
- OpenWrt 25.12.6, bcm27xx/bcm2712, `aarch64_cortex-a76`, kernel 6.12.112.
- Kernel ABI **`294a8151df6d94c4f262106e0654471f`**.
- Wireless backports **6.18.55-r2**.
- Upstream [morrownr/rtw89](https://github.com/morrownr/rtw89), commit **`8019af4b9ed57a9042e139432b88687770f155b5`**.
- Production artifact: **`kmod-ma86xh-rtw89-6.12.112.20261105-r6.apk`**.
- Modules: `rtw89_core_git`, `rtw89_8852c_git`, `rtw89_usb_git`, `rtw89_8852cu_git`.
- Firmware: `rtw89/rtw8852c_fw-2.bin`, version **0.27.129.4**; SHA256 **`916e87d1470bce5400712f53d419ae68a0f44d858670c57e118d4f0b1d89c5b4`**.

This is not a generic driver for any OpenWrt release. Kernel/backports changes require a rebuild and validation. Do not force package installation around ABI checks.

## Why this patch exists
AP advertising worked but iPhone authentication stalled. Diagnostics observed management TX submission, successful USB transfer and report queueing, but no corresponding genuine firmware completion.

`R_AX_PTCLRPT_FULL_HDL` at **0xC660** read **0x04**: special-report route bits 5:4 were zero despite the intended `FWD_TO_WLCPU=1`. A guarded late direct masked write, outside firmware IO packs, changed the register to **0x14**, preserving all other fields. Firmware reports and client authorization/traffic then worked.

The repair is restricted to RTL8852C USB with TX reports enabled and an explicit module option. It does not fabricate ACKs, bypass SAE or weaken PMF. Its cause should not be generalized to unrelated chips.

## Production startup ordering
`/etc/modules.d/60-ma86xh-rtw89`:
```text
rtw89_core_git abtx_repair_route=1 abtx_trace=0
```
`/etc/modules.d/61-ma86xh-rtw89-usb`:
```text
rtw89_8852cu_git
```
Core must load first with its options. Simply relying on dependency loading or differently sorted AUTOLOAD names can load the core before repair options apply.

Read-only checks:
```sh
cat /sys/module/rtw89_core_git/parameters/abtx_repair_route
cat /sys/module/rtw89_core_git/parameters/abtx_trace
sha256sum /lib/firmware/rtw89/rtw8852c_fw-2.bin
iw dev
```
Expected `Y`, `N`, the firmware hash above and the 80 MHz AP. Startup flags survived the recorded 2026-11-07 reboot; client traffic still needs actual validation after a new rebuild.

## Preserved assets
- `Makefile`, `Kbuild`, `adapt-driver.py`: exact package/build/adaptation recipe from the validated build.
- `run-build.py`: helper for the **retained staged SDK** at `/build`, logs bounded progress to stdout and complete output to `/build/diag-build.log`.
- `enable-build-signing.py`: targeted APK signing hook for this package, using the private key already managed outside Git. Does not contain a private key.
- `vendor/rtw89-*.tar.gz`: flat-root pinned upstream source snapshot, including firmware and its license. This is the retained archive, not necessarily byte-identical to a newly generated GitHub download.
- `prebuilt/`: signed r6 APK and public verification key only.
- `SHA256SUMS`: fingerprints checked by `make check`.

The binary package signature was verified against **only this public key** using apk-tools 3.0.5 during the 2026-11-13 repository transition. The source archive and adaptation script were preserved so the corresponding source is not dependent on a vanished container. See [third-party notices](../../../THIRD_PARTY_NOTICES.md).

The source firmware hash and fresh adaptation anchors were also validated in a temporary extraction. The archive has four relative symlinks for upstream hostapd sample aliases; they were audited and excluded from that source-patching smoke test. Keep extraction confined to a new directory and inspect links instead of blindly following them.

## Restore the existing signed package
First check current kernel, ABI, installed dependencies and private backups. The recipe depends on `kmod-mac80211`, `kmod-usb-core` and `rtl8852ce-firmware`; the custom package additionally owns its matching firmware/license.

From the repo root, copy the APK and public key to `/tmp` on the router using authenticated SSH/SCP. On-device, verify SHA256 against the committed manifest before trusting/copying the key. Install the public key into `/etc/apk/keys/` only after verifying its provenance. Use a temporary key directory if independently checking just this signature.

```sh
mkdir -p /tmp/ma86xh-verify-keys
cp /tmp/ma86xh-diagnostic-public.pem /tmp/ma86xh-verify-keys/
apk --keys-dir /tmp/ma86xh-verify-keys verify /tmp/kmod-ma86xh-rtw89-6.12.112.20261105-r6.apk
```
The command form was tested with the retained SDK's apk-tools. Confirm `apk --help` if another installed apk version differs.

After backing up the current package/startup files and coordinating any Wi-Fi interruption:
```sh
cp /tmp/ma86xh-diagnostic-public.pem /etc/apk/keys/
apk add --simulate /tmp/kmod-ma86xh-rtw89-6.12.112.20261105-r6.apk
```
Inspect the preview; proceed with the corresponding `apk add` only if it does not upgrade kernel/backports or replace unrelated working packages. If r6 is already installed, stop and investigate rather than assuming a no-op reinstall repairs active modules. Reloading modules interrupts the AP; preserve wired management and coordinate with the user. Never use `--allow-untrusted`.

Verify the two startup files, repair/tracing flags, firmware, country/security/channel/width, client authorization and actual traffic. The committed package is not a complete network configuration backup.

## Retained SDK environment
The original SDK archive is deliberately **not committed**:
- Mac archive: `/Users/pmartis/.local/state/openwrt-builds/ma86xh-25.12.6/sdk.tar.zst`
- SHA256: **`9edc70b7bd3714ec31512af91c0152be40f029e7ea3ef626eef26d42433bd1ce`**
- Release download directory: `https://downloads.openwrt.org/releases/25.12.6/targets/bcm27xx/bcm2712/`
- Retained Docker container: `ma86xh-diag-20261106`; Linux x86_64 SDK host tools, ARM64 musl target compiler.
- SDK `/build`; pristine source `/driver`; package `/build/package/ma86xh-rtw89`.
- Compiler `/build/staging_dir/toolchain-aarch64_cortex-a76_gcc-14.3.0_musl/bin/aarch64-openwrt-linux-musl-gcc`.
- Staged backport headers `/build/staging_dir/target-aarch64_cortex-a76_musl/usr/include/mac80211-backport/`.
- Private key existed at `/build/ma86xh-signing/private.pem`. **Do not read, export, log or commit it.** If unavailable, generate/manage a new key privately and deliberately enroll its public key on the target.

SDK archive fingerprint, compiler identity and release/ABI matter more than a mutable container name. A fresh SDK download must be verified against upstream release checksums; do not assume a similarly named archive is identical.

## Rebuild in the retained staged SDK
Use a copy/snapshot of the working SDK for experiments. Never patch the only trusted package or `/driver` tree in place.

1. Check `SHA256SUMS`. Confirm `/driver` corresponds to the pinned, **unadapted** source. `Build/Prepare` applies the adaptation; applying it twice must fail its anchor checks.
2. Place the recipe's `Makefile`, `Kbuild` and `adapt-driver.py` in `/build/package/ma86xh-rtw89/`.
3. Confirm matching kernel build state and staged **6.18.55-r2** mac80211 headers. Existing staged headers were checked; a clean SDK does not automatically supply the same compiled dependency state.
4. Ensure this package is selected as a module and official dependencies are staged. Keep kernel/backports pins unchanged.
5. If producing a signed package, use the managed signing setup; `enable-build-signing.py` idempotently adjusts only this package's APK build rule. Run it in the intended `/build` environment, never as a general system updater.
6. Run `run-build.py` in the Linux SDK environment (copy it to `/tmp` first if needed):
```sh
python3 /tmp/run-build.py package/ma86xh-rtw89
```
This uses `make -C package/ma86xh-rtw89 TOPDIR=/build STAGING_DIR=/build/staging_dir/target-aarch64_cortex-a76_musl -j1 compile V=s` and the pinned toolchain PATH. It assumes prepared SDK configuration; it is not a fresh-machine bootstrap command.
7. Inspect the build log and package contents, signature, dependencies, firmware, autoload files and kernel `vermagic`. Bump the recipe release for changed code; do not overwrite the preserved r6 artifact.
8. Install only after the preview and recovery checks above. Validate real firmware reports/authentication, then keep diagnostics off.

## Fresh-machine reconstruction
Obtain the exact target SDK and verify it. Extract the committed flat-root source snapshot into a **new** `/driver` equivalent (inspect member paths first), bring over the recipe, and prepare the matching mac80211/kernel dependency staging. Inspect/choose the SDK's supported host environment rather than executing its x86_64 host tools on an incompatible machine.

The adaptation intentionally compares APIs against backports 6.18.55 rather than raw Linux 6.12 version checks in `mac80211.c`, `phy.c`, `phy.h` and `wow.c`. With another backports version, revisit the APIs and patch anchors instead of just changing version numbers.

A clean SDK/driver rebuild has **not** been reproduced from scratch during this documentation task. The retained staged environment and signed package are the proven recovery path; do not claim the helper alone bootstraps missing feeds, host dependencies, kernel objects, keys or firmware.

## Bounded diagnostics
`abtx_trace` gates metadata-only, rate-limited logging; production default is off. Trace stages include `SUBMIT`, `USB_DONE`, `RPT_QUEUE`, `FW_REPORT`, `FW_MATCH`, `TX_STATUS`, `C2H_META` and route repair. Counters under `abtx_counts` are meaningful when tracing is enabled, not proof of client traffic while disabled.

If enabling trace for a specific reproduction, save the prior value, limit capture duration, inspect only metadata and restore `N`. The recorded console printk setting was `7 4 1 7`; do not leave noisy debug settings behind. Use reload/reboot only when necessary and coordinated; never fabricate completion to make authentication appear successful.
