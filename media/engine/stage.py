#!/usr/bin/env python3
"""Create reviewed package payloads in MEDIA_WORK; no device or host installs."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

WORK = Path(os.environ.get("MEDIA_WORK", "/work"))
PREFIX = WORK / "prefix"
REPO = Path(os.environ.get("AIRBRIDGE_REPO", "/opt/airbridge-repo"))
STRIP = WORK / "toolchain/bin/aarch64-openwrt-linux-musl-strip"
READELF = WORK / "toolchain/bin/aarch64-openwrt-linux-musl-readelf"


def fresh(name):
    dest = WORK / name
    if dest.exists():
        dest.rename(WORK / (name + ".previous-" + str(time.time_ns())))
    dest.mkdir()
    return dest


def copy(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if source.is_symlink():
        target = os.readlink(source)
        if os.path.isabs(target):
            target = os.path.relpath(source.resolve(), source.parent)
        destination.symlink_to(target)
    else:
        shutil.copy2(source, destination)
        if destination.read_bytes()[:4] == b"\x7fELF":
            subprocess.run([str(STRIP), "--strip-unneeded", str(destination)], check=True)


def capture(binary, *args):
    result = subprocess.run(
        ["qemu-aarch64", "-L", str(WORK / "toolchain"), "-E",
         f"LD_LIBRARY_PATH={PREFIX}/lib:{WORK}/toolchain/lib",
         str(PREFIX / "bin" / binary), *args],
        text=True, capture_output=True, check=True, timeout=60)
    return result.stdout + result.stderr


def main():
    engine = fresh("engine-root")
    private = engine / "usr/lib/airbridge-media"
    for name in ("mpv", "ffmpeg", "ffprobe"):
        copy(PREFIX / "bin" / name, private / "bin" / name)
    # Include runtime plugins as well as NEEDED libraries (Mesa and ALSA dlopen).
    for path in (PREFIX / "lib").rglob("*"):
        if path.is_file() and ".so" in path.name:
            copy(path, private / "lib" / path.relative_to(PREFIX / "lib"))
    shutil.copytree(PREFIX / "share/alsa", engine / "usr/share/airbridge-media/alsa")
    shutil.copytree(PREFIX / "share/fonts/airbridge-media", engine / "usr/share/airbridge-media/fonts")
    licenses = engine / "usr/share/airbridge-media/licenses"
    for source in (WORK / "src").iterdir():
        dirs = list(source.iterdir())
        top = dirs[0] if len(dirs) == 1 and dirs[0].is_dir() else source
        for path in top.iterdir():
            if path.is_file() and path.name.upper().startswith(("COPYING", "LICENSE", "LICENCE", "NOTICE")):
                target = licenses / source.name / path.name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
    decoders = capture("ffmpeg", "-hide_banner", "-decoders")
    decoder_names = {line.split()[1] for line in decoders.splitlines()
                     if len(line.split()) >= 2 and line.strip().startswith(("V", "A", "S"))}
    required = ["h264", "hevc", "mpeg1video", "mpeg2video", "mpeg4", "vp8", "vp9",
                "libdav1d", "vc1", "aac", "mp3float", "ac3", "eac3", "dca", "flac",
                "alac", "opus", "vorbis", "pcm_s16le", "ass", "subrip",
                "dvdsub", "pgssub"]
    missing = sorted(set(required) - decoder_names)
    if missing:
        raise SystemExit("Required decoder coverage missing: " + ",".join(missing))
    manifest = {
        "architecture": "aarch64-openwrt-linux-musl",
        "required_decoders_present": required,
        "decoder_listing": decoders,
        "player_version": capture("mpv", "--version"),
        "video_outputs": capture("mpv", "--vo=help"),
        "hardware_decoder_backends": capture("mpv", "--hwdec=help"),
        "hardware_validated": False,
        "hdmi_4k_validated": False,
        "sources": json.loads((WORK / "sources.lock.json").read_text()),
    }
    out = engine / "usr/share/airbridge-media/build-manifest.json"
    out.write_text(json.dumps(manifest, indent=2) + "\n")
    (WORK / "build-manifest.json").write_text(out.read_text())
    controller = fresh("controller-root")
    copy(WORK / "airbridge-media-controller", controller / "usr/sbin/airbridge-media-controller")
    copy(WORK / "airbridge-media-player", controller / "usr/libexec/airbridge-media-player")
    copy(WORK / "airbridge-media-upload", controller / "www/cgi-bin/airbridge-media-upload")
    files = REPO / "openwrt/packages/airbridge-media/files"
    for name, dest in [
        ("airbridge-media.init", "etc/init.d/airbridge-media"),
        ("airbridge-media.config", "etc/config/airbridge-media"),
        ("mpv.conf", "etc/airbridge-media/mpv.conf"),
        ("dark-chroma.glsl", "etc/airbridge-media/dark-chroma.glsl")]:
        target = controller / dest
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(files / name, target)
        target.chmod(0o755 if name.endswith(".init") else 0o600)
    luci = fresh("luci-root")
    luci_source = REPO / "openwrt/packages/luci-app-airbridge-media"
    shutil.copytree(luci_source / "htdocs", luci / "www")
    shutil.copytree(luci_source / "root", luci, dirs_exist_ok=True)
    hevc = fresh("hevc-root")
    copy(WORK / "hevc/rpi-hevc-dec.ko", hevc / "lib/modules/6.12.112/rpi-hevc-dec.ko")
    for root in (engine, controller, luci, hevc):
        entries = {}
        size = 0
        for path in sorted(root.rglob("*")):
            if path.is_file() and not path.is_symlink():
                data = path.read_bytes()
                size += len(data)
                entries[str(path.relative_to(root))] = hashlib.sha256(data).hexdigest()
        (WORK / (root.name + ".files.json")).write_text(json.dumps(entries, indent=2) + "\n")
        print(root.name, "runtime bytes", size, flush=True)


if __name__ == "__main__":
    main()
