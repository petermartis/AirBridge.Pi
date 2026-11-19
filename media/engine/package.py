#!/usr/bin/env python3
"""Sign prepared APKs using a file-managed key; never reads or prints the key."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

WORK = Path(os.environ.get("MEDIA_WORK", "/work"))
APK = WORK / "host/bin/apk"
KEY = WORK / "signing/private.pem"
OUT = WORK / "artifacts"
KERNEL = "kernel=6.12.112~294a8151df6d94c4f262106e0654471f-r1"
PACKAGES = [
    ("airbridge-media-engine", "engine-root", "GPL-2.0-or-later",
     "libc libgcc libstdcpp6 kmod-drm-vc4 kmod-drm-v3d kmod-airbridge-hevc kmod-airbridge-hdmi-codec"),
    ("airbridge-media", "controller-root", "MIT",
     "airbridge-media-engine=1.0.0-r1 libubus libubox libblobmsg-json libjson-c"),
    ("luci-app-airbridge-media", "luci-root", "MIT", "luci-base airbridge-media>=1.0.0-r4"),
    ("kmod-airbridge-hevc", "hevc-root", "GPL-2.0-only",
     KERNEL + " kmod-video-core kmod-video-mem2mem kmod-video-dma-contig"),
    ("kmod-airbridge-hdmi-codec", "hdmi-root", "GPL-2.0-only", KERNEL + " kmod-sound-soc-core"),
]


def main():
    if not KEY.is_file() or KEY.stat().st_mode & 0o077:
        raise SystemExit("A privately managed signing key with mode 0600 is required")
    if OUT.exists():
        OUT.rename(WORK / ("artifacts.previous-" + str(time.time_ns())))
    OUT.mkdir()
    public = OUT / "airbridge-media-public.pem"
    subprocess.run(["openssl", "pkey", "-in", str(KEY), "-pubout", "-out", str(public)], check=True)
    verify = WORK / "verify-keys"
    verify.mkdir(exist_ok=True)
    shutil.copy2(public, verify / public.name)
    hashes = {}
    for name, dirname, license_id, depends in PACKAGES:
        payload = WORK / dirname
        if not payload.is_dir():
            raise SystemExit("Missing prepared payload " + dirname)
        version = {"airbridge-media": "1.0.0-r11", "luci-app-airbridge-media": "1.0.0-r4"}.get(name, "1.0.0-r1")
        output = OUT / (name + "-" + version + ".apk")
        args = [str(APK), "mkpkg", "--files", str(payload), "--output", str(output),
                "--sign-key", str(KEY)]
        for info in [
            "name:" + name, "version:" + version, "arch:aarch64_cortex-a76",
            "description:AirBridge.Pi native local HDMI media add-on",
            "license:" + license_id, "url:https://github.com/petermartis/AirBridge.Pi",
            "depends:" + depends]:
            args += ["--info", info]
        subprocess.run(args, check=True)
        subprocess.run([str(APK), "--keys-dir", str(verify), "verify", str(output)], check=True)
        hashes[output.name] = hashlib.sha256(output.read_bytes()).hexdigest()
    hashes[public.name] = hashlib.sha256(public.read_bytes()).hexdigest()
    (OUT / "SHA256SUMS").write_text(
        "".join(f"{digest}  {name}\n" for name, digest in hashes.items()))
    (OUT / "artifact-manifest.json").write_text(json.dumps({
        "packages": hashes, "kernel_dependency": KERNEL,
        "hardware_validation": "See device validation report; build signatures do not prove HDMI performance"
    }, indent=2) + "\n")
    print("Signed artifacts:", OUT)


if __name__ == "__main__":
    main()
