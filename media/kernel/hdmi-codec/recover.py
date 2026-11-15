#!/usr/bin/env python3
"""Host-only exact Linux/OpenWrt HDMI codec recovery. Offline except `fetch`."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import sys
import urllib.request

LINUX_REVISION = "7aba70ab2e8d8ab3cd45abcaa15e1119a00dc42a"
OPENWRT_REVISION = "981c978eff636fc7d81f0ed17b00e2a6317c949a"
SOURCE_URL = ("https://raw.githubusercontent.com/gregkh/linux/" + LINUX_REVISION
              + "/sound/soc/codecs/hdmi-codec.c")
SOURCE_SHA256 = "96180b76d3dd9316149db895d83f35c298c215361df7f710e5110b0215ad292f"
SOURCE_BYTES = 32393
KERNEL_ABI = "294a8151df6d94c4f262106e0654471f"
KBUILD = (
    "# SPDX-License-Identifier: GPL-2.0-only\n"
    "# Existing prepared kernel only; no KCONFIG change.\n"
    "obj-m += snd-soc-hdmi-codec.o\n"
    "snd-soc-hdmi-codec-y := hdmi-codec.o\n"
).encode()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def source(cache):
    fd = os.open(cache / "hdmi-codec.c", os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(fd, "rb") as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_size != SOURCE_BYTES:
            raise ValueError("HDMI source cache is not a regular file of pinned size")
        data = stream.read(SOURCE_BYTES + 1)
    if len(data) != SOURCE_BYTES or digest(data) != SOURCE_SHA256:
        raise ValueError("HDMI source SHA-256 mismatch")
    return data


def metadata():
    return {
        "schema_version": 1,
        "module": "snd-soc-hdmi-codec",
        "package": "kmod-airbridge-hdmi-codec",
        "kernel_version": "6.12.112",
        "kernel_abi": KERNEL_ABI,
        "kernel_dependency": "kernel (=6.12.112~" + KERNEL_ABI + "-r1)",
        "openwrt_revision": OPENWRT_REVISION,
        "linux_revision": LINUX_REVISION,
        "linux_tag": "v6.12.112",
        "linux_tag_object": "1092ff6640e96a20f741340bac5187c31070f091",
        "source_url": SOURCE_URL,
        "source_sha256": SOURCE_SHA256,
        "source_bytes": SOURCE_BYTES,
        "source_patch_closure": [],
        "source_overlays": [],
        "license": "GPL-2.0-only",
        "files": {
            "hdmi-codec.c": {"sha256": SOURCE_SHA256, "bytes": SOURCE_BYTES},
            "Makefile": {"sha256": digest(KBUILD), "bytes": len(KBUILD)},
        },
        "autoload": False,
        "kernel_configuration_modified": False,
        "hardware_validated": False,
    }


def fetch(cache):
    cache.mkdir(parents=True, exist_ok=True)
    path = cache / "hdmi-codec.c"
    if path.exists() or path.is_symlink():
        source(cache)
        return
    with urllib.request.urlopen(SOURCE_URL, timeout=30) as response:
        data = response.read(SOURCE_BYTES + 1)
    if len(data) != SOURCE_BYTES or digest(data) != SOURCE_SHA256:
        raise ValueError("HDMI download does not match immutable source pin")
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o644)
    with os.fdopen(fd, "wb") as stream:
        stream.write(data)


def recover(cache, output):
    data = source(cache)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.mkdir(mode=0o700, exist_ok=False)
    # No existing output, archive extraction, path interpolation or SDK writes.
    # An interrupted directory is left for operator review, never overwritten.
    for name, content in (("hdmi-codec.c", data), ("Makefile", KBUILD)):
        with (output / name).open("xb") as stream:
            stream.write(content)
        (output / name).chmod(0o644)
    with (output / ".source-provenance.json").open("x", encoding="utf-8") as stream:
        json.dump(metadata(), stream, indent=2, sort_keys=True)
        stream.write("\n")
    (output / ".source-provenance.json").chmod(0o644)
    output.chmod(0o755)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("fetch", "verify", "recover"))
    parser.add_argument("--cache", type=Path, default=Path(__file__).resolve().parent / "cache")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    if (args.action == "recover") != (args.output is not None):
        parser.error("--output is required only for recover and must not already exist")
    try:
        if args.action == "fetch":
            fetch(args.cache)
        source(args.cache)
        if args.action == "recover":
            recover(args.cache, args.output)
        print(json.dumps(metadata(), sort_keys=True))
    except (OSError, ValueError) as exc:
        print("HDMI codec recovery refused: " + str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
