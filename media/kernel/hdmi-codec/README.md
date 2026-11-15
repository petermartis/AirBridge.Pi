# Exact-release HDMI ASoC codec recovery
Optional recovery of the pruned **snd-soc-hdmi-codec.ko** for the existing bcm27xx/bcm2712 OpenWrt 25.12.6 / Linux 6.12.112 kernel, ABI `294a8151df6d94c4f262106e0654471f`. This is not HDA HDMI audio, not a newer driver transplant, and not a kernel/configuration change.
## Quick offline recovery
From the repository root, with Python 3 standard library on the build host:
```sh
python3 media/kernel/hdmi-codec/recover.py verify
python3 media/kernel/hdmi-codec/recover.py recover --output media/kernel/hdmi-codec/build/driver
```
Output must be a **new directory**. It contains only `hdmi-codec.c`, an external Kbuild Makefile and source provenance JSON. No shell, patch program, network or kernel build is invoked by recover/verify. The cached regular source file is bounded to 32,393 bytes and SHA-256 `96180b76d3dd9316149db895d83f35c298c215361df7f710e5110b0215ad292f`; symlinks, devices, oversized/modified sources and existing output are refused.
The lead may copy this prepared directory into the isolated build container and compile through the separately copied matching prepared kernel, external `M=` only. Kbuild produces `snd-soc-hdmi-codec.ko` from `hdmi-codec.o`. Do not configure/rebuild/replace the existing kernel or force module/kernel dependencies. Python is not required on the router.
`recover.py fetch` is the sole opt-in network operation, downloading the immutable source only when cache is missing; it never replaces a bad cache. Normal recovery works completely offline.
## Exact source and closure
Linux stable tag v6.12.112 (tag object `1092ff6640e96a20f741340bac5187c31070f091`) resolves to commit `7aba70ab2e8d8ab3cd45abcaa15e1119a00dc42a`. The cached source is from that immutable commit, with its original GPL SPDX, copyright and author preserved. Full GPL text is in `COPYING`.
The exact OpenWrt release commit is `981c978eff636fc7d81f0ed17b00e2a6317c949a`; the complete source archive SHA-256 is `b17aacd2ee31faf64c1d8d99bdb194ae1a4ff68e008ca4f6a504a9985b134174`.
An audit of **all 1,368** applicable patches (884 bcm27xx, 277 generic backport, 56 generic hack, 151 generic pending) found **zero changes to hdmi-codec.c or include/sound/hdmi-codec.h**, and no file overlays for either. Thus the exact downstream source closure is empty: the Linux 6.12.112 file is unchanged, not merely an assumed generic compatible replacement. `patch-closure.json` records the audit; `provenance.json` records source pins, Kbuild hashes and evidence.
The audit is independently repeatable offline if the exact pinned OpenWrt archive is cached locally:
```sh
python3 media/kernel/hdmi-codec/audit_closure.py --archive /path/to/pinned-openwrt-source.tar.gz
```
The auditor verifies the whole archive size/hash, reads relevant patch bodies in memory, extracts no files and refuses a nonempty HDMI closure. The 11 MB archive need not be tracked or present for ordinary recovery.
## Package and prerequisites
`openwrt/packages/kmod-airbridge-hdmi-codec/Makefile` defines `kmod-airbridge-hdmi-codec`, depending on the exact-release `kmod-sound-soc-core` (ALSA sound-core/PCM transitive). Target/kernel/ABI/release guards and the shared read-only prepared-kernel fingerprint checker run before/after external compilation. No KCONFIG entry is added; autoload is explicitly empty. Module loading and any bind/reprobe remain separate approved device operations owned by the lead.
Read-only inspection of the copied matching kernel confirmed CONFIG_SND_SOC_HDMI_CODEC=m, CONFIG_SND_SOC=m, CONFIG_SND_PCM=m, CONFIG_SND_PCM_ELD=y, CONFIG_SND_PCM_IEC958=y, CONFIG_SND_JACK=y. Module.symvers already contains snd_jack_report from snd, channel-map/ELD/IEC958 helpers from snd-pcm, and component registration from snd-soc-core. No prerequisite kernel feature needs changing.
The copied kernel's include/sound/hdmi-codec.h also matched the immutable stable commit byte-for-byte, SHA-256 `5a74465ad44b60e87aeee7f83fe39cd75065a6173a0f889ae7435b447aa21591`.
## Validation boundary
`python3 media/kernel/hdmi-codec/tests.py` tests offline recovery, source tampering/hostile cache denial, nonreplacement of existing output, provenance, licensing, closure-auditor logic and package metadata/ABI guards. Tests do not compile/load the module or prove that HDMI codec absence is the only GPU deferred-probe cause. Connector/sound registration and real 4K HDMI HEVC playback require actual device evidence; no hardware validation is claimed here.
