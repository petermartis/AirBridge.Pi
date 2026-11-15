# Exact-release HEVC decoder add-on
This is source recovery for the **existing** OpenWrt 25.12.6 / Linux 6.12.112 / bcm27xx-bcm2712 kernel, ABI `294a8151df6d94c4f262106e0654471f`. It is not a kernel upgrade or a generic/latest Raspberry Pi driver transplant.
## Offline source recovery
The four original upstream patches are tracked under `patches/`, preserving authors, commit headers and source license notices. Python 3 standard library is required only on the host/build system, never on the router.
Run from the repository root:
```sh
python3 media/kernel/recover.py verify
python3 media/kernel/recover.py recover --output media/kernel/build/hevc
```
The output directory must **not exist**, even if empty. Recovery writes only eight C/header files, a small external-module Makefile and `.source-provenance.json`. The original kernel Kconfig/Makefile/global diffs are deliberately skipped. It never executes `patch`, shell commands, Kbuild or a network operation. Cached patches must match both pinned byte lengths and SHA-256; symlinks, devices, missing/modified patches and nonmatching hunks fail closed. There is no fuzz or patch-offset adjustment.
For an isolated container containing this repository at `/work/repo`, with a separately copied **already prepared** exact kernel tree at `/work/kernel`:
```sh
python3 /work/repo/media/kernel/recover.py recover --output /work/hevc
python3 /work/repo/media/kernel/check_kernel.py --kernel-tree /work/kernel
```
`/work/hevc/Makefile` has `obj-m += rpi-hevc-dec.o` and composes hevc_d/video/hw/h265 objects. The lead owns compilation via the copied prepared tree with external `M=/work/hevc`; this tool does not build or configure any kernel. Do not run `olddefconfig`, `modules_prepare`, a kernel build, change CONFIG_VIDEO_RPI_HEVC_DEC, force vermagic, or overwrite the existing kernel.
To deliberately repopulate a missing cache, and **only then** permit downloads of the four pinned public URLs:
```sh
python3 media/kernel/recover.py fetch
```
Fetch never overwrites a mismatched/existing patch. `recover`/`verify` remain usable without any network access after the cache is present.
## Package
`openwrt/packages/kmod-airbridge-hevc/Makefile` defines package `kmod-airbridge-hevc` containing module `rpi-hevc-dec.ko`. Copy the recipe into a matching isolated SDK with `AIRBRIDGE_MEDIA_SOURCE` pointing at this repository root. It checks target, kernel version and ABI; uses existing prepared generated headers/Module.symvers and the standard KernelPackage exact kernel dependency; and makes only an external module. No kernel KCONFIG setting is added. Autoload is explicitly empty: installation does not authorize loading or device modifications.
The recipe also invokes the read-only check_kernel.py before and after external compilation: .config, generated autoconf/UTS, Module.symvers and two ABI-critical media headers must match fingerprints observed in the retained release SDK. This matters because its missing .vermagic file otherwise falls back to a fixed ABI string, which alone cannot detect drift. A mismatch is a blocker to investigate, not permission to regenerate configuration, replace the kernel or update these pins merely to silence a check.
Dependencies are the exact release `kmod-video-core`, `kmod-video-mem2mem`, `kmod-video-dma-contig` and their standard transitive dependencies. Do not replace them with modules from a newer release, install via force flags, or bypass signature checks.
## Provenance and feasibility
The precise release is OpenWrt revision `981c978eff636fc7d81f0ed17b00e2a6317c949a` (`r33311-981c978eff`). Its base linux-6.12.112.tar.xz SHA-256 is `164dc9d1f6c93c61a15e1f071c48379b467f2b17c469cce7223471968208ed03`. `provenance.json` records public source URLs/hashes, the complete four-patch decoder closure and deterministic recovered file hashes.
The retained prepared kernel has VIDEO_DEV/MEDIA_CONTROLLER/CMA/DMA_CMA/BCM2712_IOMMU/RASPBERRYPI_FIRMWARE built in, mem2mem and contiguous buffers modular, and already exports media_request_manual_complete plus the required V4L2/firmware APIs. Its SAND UAPI headers already contain NC12/NC30/Nc12/Nc30 formats. The disabled decoder's sources were pruned from the SDK, but the prerequisite core patches were not absent.
Release DT patch 950-0837 enables the new shared CM5 HEVC node with compatible `brcm,bcm2712-hevc-dec`, `raspberrypi,hevc-dec`. This is source evidence only: the deployed device's live DT has not been verified. Default decoder node preference from patch 950-0846 is video_nr=19, not a guarantee that `/dev/video19` exists or is free; discover via V4L2 metadata.
The closure is 950-0834 (new driver), 950-0839 (downstream single-planar SAND formats), 950-0846 (preferred video node), 950-0847 (old SAND formats advertised for userspace compatibility). Legacy rpivid is not substituted.
## Validation boundary
Run `python3 media/kernel/tests.py` for deterministic recovery, hashes/license preservation, offline operation, strict context/path checks, hostile cache/output denial and SDK-independent package-metadata/ABI guard tests. The metadata tests use only temporary stub makefiles, not the real SDK. Full GPL-2.0 text is retained at `LICENSES/GPL-2.0`; original source SPDX/author notices remain untouched.
Recovery and metadata tests cannot prove successful module compilation, ABI loading, decoder availability, DRM/ALSA access or physical HDMI playback. The lead must inspect modpost/imports, actual module vermagic/dependencies and exact kernel package dependency before any separately authorized device trial. Mandatory real 4K HDMI HEVC playback remains outstanding until actual hardware observation; no test here changes that acceptance criterion.
