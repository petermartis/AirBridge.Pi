#!/usr/bin/env python3
"""Recover only the pinned OpenWrt HEVC C/header subtree; no shell or SDK writes.

Python is a host/build dependency only. `recover` and `verify` never use network.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import sys
import urllib.request

REVISION = "981c978eff636fc7d81f0ed17b00e2a6317c949a"
BASE_URL = (
    "https://raw.githubusercontent.com/openwrt/openwrt/"
    + REVISION + "/target/linux/bcm27xx/patches-6.12/"
)
KERNEL_VERSION = "6.12.112"
KERNEL_ABI = "294a8151df6d94c4f262106e0654471f"
PATCHES = (
    ("950-0834-media-platform-Add-Raspberry-Pi-HEVC-decoder-driver.patch",
     "ef5f170cdaad35035dcf1e0b56ada1560b9081b4ed49a18d6482ff3b6cc08593", 130378),
    ("950-0839-media-hevc_dec-Add-in-downstream-single-planar-SAND-.patch",
     "1834b9b3570ab677c61e45199934e85bf8be2e8d6a945212bacdeeff41174e60", 10189),
    ("950-0846-media-hevc_dec-Add-module-parameter-for-video_nr.patch",
     "b0e7a7e0714adbaebd68429a7562973cd341211ac19fd273e353417ca71e6e0f", 1486),
    ("950-0847-media-hevc_dec-Drop-the-new-image-formats-until-we-h.patch",
     "05d3861f7df2724bfb898dbbb63b596a2fe1f1af9154e0f33427eb014c02719c", 1666),
)
PREFIX = "drivers/media/platform/raspberrypi/hevc_dec/"
FILES = (
    "hevc_d.c", "hevc_d.h", "hevc_d_h265.c", "hevc_d_h265.h",
    "hevc_d_hw.c", "hevc_d_hw.h", "hevc_d_video.c", "hevc_d_video.h",
)
KBUILD = (
    "# SPDX-License-Identifier: GPL-2.0-only\n"
    "# External module only: never changes the existing kernel configuration.\n"
    "obj-m += rpi-hevc-dec.o\n"
    "rpi-hevc-dec-y := hevc_d.o hevc_d_video.o hevc_d_hw.o hevc_d_h265.o\n"
)
HUNK = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")


class RecoveryError(Exception):
    pass


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def read_patch(cache, name, digest, size):
    """Do not follow a cache file symlink or read unlimited/unexpected input."""
    try:
        fd = os.open(cache / name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        with os.fdopen(fd, "rb") as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_size != size:
                raise RecoveryError(f"{name}: not a regular file of pinned size {size}")
            data = stream.read(size + 1)
    except OSError as exc:
        raise RecoveryError(f"{name}: cache unavailable ({exc.strerror})") from exc
    if len(data) != size or sha256(data) != digest:
        raise RecoveryError(f"{name}: SHA-256 mismatch")
    return data


def fetch(cache):
    """Only this explicitly selected subcommand may access the public network."""
    cache.mkdir(parents=True, exist_ok=True)
    for name, digest, size in PATCHES:
        path = cache / name
        if path.exists() or path.is_symlink():
            read_patch(cache, name, digest, size)
            continue
        with urllib.request.urlopen(BASE_URL + name, timeout=30) as response:
            data = response.read(size + 1)
        if len(data) != size or sha256(data) != digest:
            raise RecoveryError(f"{name}: download does not match pinned source")
        # Never overwrite an existing cache file (including a raced symlink).
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o644)
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)


def relative_target(path):
    if path == "/dev/null":
        return None
    if not path.startswith(("a/", "b/")):
        raise RecoveryError("patch path lacks a/ or b/ prefix")
    path = path[2:]
    parts = path.split("/")
    if (not parts or path.startswith("/") or "\\" in path
            or any(p in ("", ".", "..") for p in parts)):
        raise RecoveryError("unsafe patch path")
    return path


def apply_patch(files, data):
    """Apply exact unified hunks in memory, no fuzz/offset/subprocess patch.

    Global files and the upstream Kconfig/Makefile are deliberately ignored.
    Only the eight named driver C/headers may enter the recovered source tree.
    """
    lines = data.decode("utf-8").splitlines(keepends=True)
    index = 0
    changed = 0
    while index < len(lines):
        if not lines[index].startswith("--- "):
            index += 1
            continue
        old_path = relative_target(lines[index][4:].strip())
        index += 1
        if index >= len(lines) or not lines[index].startswith("+++ "):
            raise RecoveryError("missing unified diff destination")
        new_path = relative_target(lines[index][4:].strip())
        index += 1
        target = new_path or old_path
        wanted = target is not None and target.startswith(PREFIX)
        if wanted:
            relative = target[len(PREFIX):]
            if relative in ("Kconfig", "Makefile"):
                wanted = False
            elif relative not in FILES:
                raise RecoveryError(f"unexpected decoder file {relative}")
        if not wanted:
            # Pinned upstream global diff sections contain no patch-header-like
            # code lines. No such content is ever written to the destination.
            while index < len(lines) and not lines[index].startswith("--- "):
                index += 1
            continue
        if new_path != PREFIX + relative or (old_path is not None and old_path != new_path):
            raise RecoveryError("decoder file deletion/rename is not permitted")
        if old_path is None:
            if relative in files:
                raise RecoveryError(f"duplicate new file {relative}")
            original = []
        else:
            if relative not in files:
                raise RecoveryError(f"missing original file {relative}")
            original = files[relative].splitlines(keepends=True)
        output = []
        cursor = 0
        hunks = 0
        while index < len(lines):
            match = HUNK.match(lines[index])
            if not match:
                break
            old_start, old_count, new_start, new_count = (
                int(match[1]), int(match[2] or 1), int(match[3]), int(match[4] or 1)
            )
            old_at = old_start - 1 if old_count else old_start
            new_at = new_start - 1 if new_count else new_start
            if old_at < cursor or old_at > len(original):
                raise RecoveryError(f"{relative}: overlapping/out-of-range hunk")
            output.extend(original[cursor:old_at])
            cursor = old_at
            if new_at != len(output):
                raise RecoveryError(f"{relative}: new hunk position mismatch")
            index += 1
            removed = added = 0
            while removed < old_count or added < new_count:
                if index >= len(lines):
                    raise RecoveryError(f"{relative}: truncated hunk")
                line = lines[index]
                index += 1
                if not line or line[0] not in " +-":
                    raise RecoveryError(f"{relative}: invalid hunk line")
                operation, content = line[0], line[1:]
                if operation in " -":
                    if cursor >= len(original) or original[cursor] != content:
                        raise RecoveryError(f"{relative}: context mismatch; refusing fuzz")
                    cursor += 1
                    removed += 1
                if operation in " +":
                    output.append(content)
                    added += 1
                if removed > old_count or added > new_count:
                    raise RecoveryError(f"{relative}: hunk count mismatch")
            hunks += 1
        if not hunks:
            raise RecoveryError(f"{relative}: no hunks")
        output.extend(original[cursor:])
        files[relative] = "".join(output)
        changed += 1
    if not changed:
        raise RecoveryError("patch contains no allowed decoder changes")


def recover(cache):
    files = {}
    for name, digest, size in PATCHES:
        apply_patch(files, read_patch(cache, name, digest, size))
    if set(files) != set(FILES):
        raise RecoveryError("incomplete recovered source closure")
    for name, text in files.items():
        if not text.endswith("\n") or "SPDX-License-Identifier: GPL-2.0" not in text[:512]:
            raise RecoveryError(f"{name}: source license/newline unexpectedly missing")
    files["Makefile"] = KBUILD
    return {name: text.encode("utf-8") for name, text in sorted(files.items())}


def metadata(files):
    return {
        "schema_version": 1,
        "openwrt_version": "25.12.6",
        "openwrt_revision": REVISION,
        "kernel_version": KERNEL_VERSION,
        "kernel_abi": KERNEL_ABI,
        "module": "rpi-hevc-dec",
        "license": "GPL-2.0-only",
        "patches": [
            {"name": name, "url": BASE_URL + name, "sha256": digest, "bytes": size}
            for name, digest, size in PATCHES
        ],
        "files": {name: {"sha256": sha256(data), "bytes": len(data)}
                  for name, data in sorted(files.items())},
        "kernel_configuration_modified": False,
        "hardware_validated": False,
    }


def write_output(output, files):
    """Never replace an existing directory/file or follow an output symlink."""
    if set(files) != set(FILES) | {"Makefile"}:
        raise RecoveryError("refusing unexpected output filenames")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.mkdir(mode=0o700, exist_ok=False)
    try:
        for name, data in files.items():
            with (output / name).open("xb") as stream:
                stream.write(data)
            (output / name).chmod(0o644)
        with (output / ".source-provenance.json").open("x", encoding="utf-8") as stream:
            json.dump(metadata(files), stream, indent=2, sort_keys=True)
            stream.write("\n")
        (output / ".source-provenance.json").chmod(0o644)
        output.chmod(0o755)
    except BaseException:
        shutil.rmtree(output)
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("fetch", "recover", "verify"))
    parser.add_argument("--cache", type=Path, default=Path(__file__).resolve().parent / "patches")
    parser.add_argument("--output", type=Path, help="new directory, required only for recover")
    args = parser.parse_args(argv)
    if (args.action == "recover") != (args.output is not None):
        parser.error("--output is required only with recover")
    try:
        if args.action == "fetch":
            fetch(args.cache)
        files = recover(args.cache)
        if args.action == "recover":
            write_output(args.output, files)
        print(json.dumps(metadata(files), sort_keys=True))
    except (OSError, RecoveryError, ValueError) as exc:
        print(f"HEVC recovery refused: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
