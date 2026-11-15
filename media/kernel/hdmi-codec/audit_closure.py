#!/usr/bin/env python3
"""Read-only offline audit of the exact OpenWrt source archive. Never extracts."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile

REVISION = "981c978eff636fc7d81f0ed17b00e2a6317c949a"
ARCHIVE_SHA256 = "b17aacd2ee31faf64c1d8d99bdb194ae1a4ff68e008ca4f6a504a9985b134174"
ARCHIVE_BYTES = 11813390


def audit(data):
    if len(data) != ARCHIVE_BYTES or hashlib.sha256(data).hexdigest() != ARCHIVE_SHA256:
        raise ValueError("archive does not match exact OpenWrt source pin")
    result = {
        "openwrt_revision": REVISION,
        "archive_url": "https://codeload.github.com/openwrt/openwrt/tar.gz/" + REVISION,
        "archive_sha256": ARCHIVE_SHA256,
        "scanned_patch_directories": {},
        "source_patch_closure": [],
        "source_overlays": [],
        "header_patch_references": [],
        "header_overlays": [],
    }
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
        for member in archive:
            if not member.isfile() or "/" not in member.name:
                continue
            name = member.name.split("/", 1)[1]
            if not name.startswith(("target/linux/generic/", "target/linux/bcm27xx/")):
                continue
            if name.endswith("/sound/soc/codecs/hdmi-codec.c"):
                result["source_overlays"].append(name)
            if name.endswith("/include/sound/hdmi-codec.h"):
                result["header_overlays"].append(name)
            if not name.endswith(".patch"):
                continue
            directory = name.rsplit("/", 1)[0]
            counts = result["scanned_patch_directories"]
            counts[directory] = counts.get(directory, 0) + 1
            patch = archive.extractfile(member).read()
            if b"sound/soc/codecs/hdmi-codec.c" in patch:
                result["source_patch_closure"].append(name)
            if b"include/sound/hdmi-codec.h" in patch:
                result["header_patch_references"].append(name)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        with args.archive.open("rb") as stream:
            result = audit(stream.read(ARCHIVE_BYTES + 1))
        print(json.dumps(result, sort_keys=True))
        if any(result[key] for key in ("source_patch_closure", "source_overlays",
                                       "header_patch_references", "header_overlays")):
            raise ValueError("nonempty HDMI closure: baseline-only recovery is not valid")
    except (OSError, ValueError, tarfile.TarError) as exc:
        print("HDMI closure audit refused: " + str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
