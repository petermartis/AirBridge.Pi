#!/usr/bin/env python3
"""Read-only identity guard for the retained, already prepared release kernel."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import sys

FINGERPRINTS = {
    ".config": "d1640f7e7e88fa1ff19741b7b367c52d13eba04a91db76b6255020a509e0298e",
    "include/generated/autoconf.h": "2e51cd255abc341e9109bfe7e58ad58a17136074a514321693661c12461decdb",
    "include/generated/utsrelease.h": "f85b4b4376daa273d74011f6b7258a632ae7c44a30756644a6b3233ee04f51af",
    "Module.symvers": "0b1bfb7f55cf3e9fe0dcbaf86c467f839f58ab88eb18a5dc17600ad31a3aa885",
    "include/media/media-request.h": "0bf1935b49cc178e36d37a667035ed6cd161e90d198ab0a4acc77ead24edd39d",
    "include/uapi/linux/videodev2.h": "801a5b0714a23abc4c39a5d00eb24415297c4e2cedd06c908f595f95acd2c0bb",
}


def check(tree):
    verified = {}
    for name, expected in FINGERPRINTS.items():
        fd = os.open(tree / name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        with os.fdopen(fd, "rb") as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_size > 8 * 1024 * 1024:
                raise ValueError(f"{name}: unsafe or oversized prepared-kernel file")
            digest = hashlib.sha256(stream.read(8 * 1024 * 1024 + 1)).hexdigest()
        if digest != expected:
            raise ValueError(f"{name}: prepared-kernel SHA-256 mismatch; do not rebuild/configure it")
        verified[name] = digest
    return verified


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel-tree", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        verified = check(args.kernel_tree)
    except (OSError, ValueError) as exc:
        print(f"HEVC prepared-kernel check refused: {exc}", file=sys.stderr)
        return 1
    print(json.dumps({"verified": verified, "hardware_validated": False}, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
