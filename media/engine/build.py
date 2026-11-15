#!/usr/bin/env python3
"""Cross-build into a private prefix; never installs into the host/router root.

Run in the isolated x86_64 Linux build container. Downloads are hashed in a lock
manifest; --fetch records first-use hashes, while later builds require that lock.
First-use hashes are integrity pins, not an independent upstream signature check.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import time
import urllib.request

HERE = Path(__file__).resolve().parent
SOURCES = json.loads((HERE / "sources.json").read_text())
WORK = Path(os.environ.get("MEDIA_WORK", "/work"))
PREFIX = WORK / "prefix"
BUILD = WORK / "build"
SRC = WORK / "src"
CACHE = WORK / "downloads"
LOCK = WORK / "sources.lock.json"
TC = WORK / "toolchain"
TRIPLE = "aarch64-openwrt-linux-musl"
CC = TC / "bin" / (TRIPLE + "-gcc")
JOBS = str(int(os.environ.get("JOBS", "4")))
ENV = dict(os.environ)
ENV.update({
    "PATH": str(TC / "bin") + ":" + ENV.get("PATH", ""),
    "CC": str(CC), "CXX": str(TC / "bin" / (TRIPLE + "-g++")),
    "AR": str(TC / "bin" / (TRIPLE + "-ar")),
    "RANLIB": str(TC / "bin" / (TRIPLE + "-ranlib")),
    "STRIP": str(TC / "bin" / (TRIPLE + "-strip")),
    "PKG_CONFIG_LIBDIR": str(PREFIX / "lib/pkgconfig") + ":" + str(PREFIX / "share/pkgconfig"),
    "PKG_CONFIG_PATH": "",
    "CFLAGS": "-O2 -pipe -fPIC -mcpu=cortex-a76 -I" + str(PREFIX / "include"),
    "CXXFLAGS": "-O2 -pipe -fPIC -mcpu=cortex-a76 -I" + str(PREFIX / "include"),
    "LDFLAGS": "-L" + str(PREFIX / "lib") + " -Wl,-rpath,/usr/lib/airbridge-media/lib",
    "STAGING_DIR": str(WORK),
})


def run(args, cwd=None, env=None):
    print("+", " ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=cwd, env=env or ENV, check=True)


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch():
    CACHE.mkdir(parents=True, exist_ok=True)
    prior = json.loads(LOCK.read_text()) if LOCK.exists() else {}

    def one(item):
        name, entry = item
        path = CACHE / (name + ".archive")
        if not path.exists():
            request = urllib.request.Request(entry["url"], headers={"User-Agent": "AirBridge.Pi-build/1"})
            with urllib.request.urlopen(request, timeout=90) as response, path.with_suffix(".tmp").open("wb") as out:
                shutil.copyfileobj(response, out)
            path.with_suffix(".tmp").rename(path)
        record = dict(entry, sha256=digest(path))
        if name in prior and record != prior[name]:
            raise RuntimeError("Source pin changed for " + name)
        print("source", name, record["sha256"], flush=True)
        return name, record

    records = dict(prior)
    failures = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        futures = {pool.submit(one, item): item[0] for item in SOURCES.items()}
        for future, name in futures.items():
            try:
                key, record = future.result()
                records[key] = record
            except Exception as exc:
                failures.append(name + ": " + str(exc))
    LOCK.write_text(json.dumps(records, indent=2) + "\n")
    if failures:
        raise RuntimeError("\n".join(failures))


def source(name):
    locked = json.loads(LOCK.read_text())
    archive = CACHE / (name + ".archive")
    if locked[name]["url"] != SOURCES[name]["url"] or digest(archive) != locked[name]["sha256"]:
        raise RuntimeError("Source integrity failure: " + name)
    dest = SRC / name
    if not dest.exists():
        dest.mkdir(parents=True)
        with tarfile.open(archive) as tar:
            tar.extractall(dest, filter="data")
    roots = list(dest.iterdir())
    return roots[0] if len(roots) == 1 and roots[0].is_dir() else dest


def cross_file():
    path = WORK / "cross.ini"
    path.write_text(f"""[binaries]
c = '{CC}'
cpp = '{ENV["CXX"]}'
ar = '{ENV["AR"]}'
strip = '{ENV["STRIP"]}'
pkg-config = '/usr/bin/pkg-config'
[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'cortex-a76'
endian = 'little'
[properties]
needs_exe_wrapper = true
[built-in options]
c_args = ['-O2', '-fPIC', '-mcpu=cortex-a76', '-I{PREFIX}/include']
cpp_args = ['-O2', '-fPIC', '-mcpu=cortex-a76', '-I{PREFIX}/include']
c_link_args = ['-L{PREFIX}/lib', '-Wl,-rpath,/usr/lib/airbridge-media/lib']
cpp_link_args = ['-L{PREFIX}/lib', '-Wl,-rpath,/usr/lib/airbridge-media/lib']
""")
    return path


def meson(name, options):
    src = source(name)
    build = BUILD / name
    if not (build / "build.ninja").exists():
        run(["meson", "setup", build, src, "--cross-file", cross_file(),
             "--prefix", PREFIX, "--libdir", "lib", "--buildtype", "release",
             "--wrap-mode", "nodownload", *options])
    run(["ninja", "-C", build, "-j", JOBS])
    run(["ninja", "-C", build, "install"])


def configure(name, options):
    src = source(name)
    if not (src / ".airbridge-configured").exists():
        run([src / "configure", "--prefix=" + str(PREFIX), "--host=" + TRIPLE,
             "--enable-shared", "--disable-static", *options], cwd=src)
        (src / ".airbridge-configured").touch()
    run(["make", "-j" + JOBS], cwd=src)
    run(["make", "install"], cwd=src)


def cmake(name, options):
    directory = BUILD / name
    run(["cmake", "-S", source(name), "-B", directory, "-G", "Ninja",
         "-DCMAKE_SYSTEM_NAME=Linux", "-DCMAKE_SYSTEM_PROCESSOR=aarch64",
         "-DCMAKE_C_COMPILER=" + str(CC), "-DCMAKE_CXX_COMPILER=" + ENV["CXX"],
         "-DCMAKE_INSTALL_PREFIX=" + str(PREFIX), "-DCMAKE_INSTALL_LIBDIR=lib",
         "-DCMAKE_PREFIX_PATH=" + str(PREFIX), "-DCMAKE_BUILD_TYPE=Release",
         "-DBUILD_TESTING=OFF", *options])
    run(["cmake", "--build", directory, "-j", JOBS])
    run(["cmake", "--install", directory])


def build(name):
    (WORK / "stamps").mkdir(exist_ok=True)
    stamp = WORK / "stamps" / name
    if stamp.exists():
        print("already built:", name, flush=True)
        return
    if name == "zlib":
        src = source(name)
        run([src / "configure", "--prefix=" + str(PREFIX)], cwd=src)
        run(["make", "-j" + JOBS], cwd=src)
        run(["make", "install"], cwd=src)
    elif name == "expat":
        configure(name, ["--without-docbook", "--without-examples", "--without-tests"])
    elif name == "libdrm":
        meson(name, ["-Dintel=disabled", "-Dradeon=disabled", "-Damdgpu=disabled",
                     "-Dnouveau=disabled", "-Dvmwgfx=disabled", "-Domap=disabled",
                     "-Dexynos=disabled", "-Dfreedreno=disabled", "-Dtegra=disabled",
                     "-Detnaviv=disabled", "-Dvc4=enabled", "-Dtests=false",
                     "-Dman-pages=disabled", "-Dvalgrind=disabled"])
    elif name == "libudev-zero":
        src = source(name)
        run(["make", "-j" + JOBS, "CC=" + str(CC), "AR=" + ENV["AR"], "RANLIB=" + ENV["RANLIB"]], cwd=src)
        run(["make", "PREFIX=" + str(PREFIX), "LIBDIR=" + str(PREFIX / "lib"), "install"], cwd=src)
    elif name == "alsa-lib":
        configure(name, ["--disable-python", "--disable-aload"])
    elif name == "freetype":
        configure(name, ["--with-harfbuzz=no", "--with-png=no", "--with-bzip2=no", "--with-brotli=no"])
    elif name == "fribidi":
        meson(name, ["-Ddocs=false", "-Dbin=false", "-Dtests=false"])
    elif name == "harfbuzz":
        meson(name, ["-Dglib=disabled", "-Dgobject=disabled", "-Dcairo=disabled",
                     "-Dicu=disabled", "-Dgraphite2=disabled", "-Dfreetype=enabled",
                     "-Dtests=disabled", "-Ddocs=disabled", "-Dutilities=disabled"])
    elif name == "libass":
        meson(name, ["-Dfontconfig=disabled", "-Dtest=false", "-Dprofile=false",
                     "-Drequire-system-font-provider=false"])
    elif name == "dav1d":
        meson(name, ["-Denable_tools=false", "-Denable_tests=false"])
    elif name == "libdisplay-info":
        meson(name, [])
    elif name == "mesa":
        meson(name, ["-Dgallium-drivers=v3d,vc4", "-Dvulkan-drivers=[]", "-Dplatforms=[]",
                     "-Dglx=disabled", "-Degl=enabled", "-Dgbm=enabled", "-Dgles1=disabled",
                     "-Dgles2=enabled", "-Dopengl=true", "-Dllvm=disabled", "-Dglvnd=disabled",
                     "-Dvideo-codecs=[]", "-Dzstd=disabled", "-Dvalgrind=disabled"])
    elif name == "vulkan-headers":
        src = source(name)
        for directory in ("vulkan", "vk_video"):
            shutil.copytree(src / "include" / directory, PREFIX / "include" / directory,
                            dirs_exist_ok=True)
    elif name == "libplacebo":
        meson(name, ["-Dvulkan=disabled", "-Dopengl=enabled", "-Dshaderc=disabled",
                     "-Dglslang=disabled", "-Dlcms=disabled", "-Ddemos=false", "-Dtests=false"])
    elif name == "ffmpeg":
        src = source(name)
        if not (src / "ffbuild/config.mak").exists():
            run([src / "configure", "--prefix=" + str(PREFIX),
                 "--enable-cross-compile", "--cross-prefix=" + str(TC / "bin" / (TRIPLE + "-")),
                 "--arch=aarch64", "--cpu=cortex-a76", "--target-os=linux", "--pkg-config=pkg-config",
                 "--enable-shared", "--disable-static", "--enable-gpl", "--disable-nonfree",
                 "--enable-pthreads", "--enable-neon", "--enable-libdrm", "--enable-libudev",
                 "--enable-v4l2-request", "--enable-sand", "--enable-libdav1d", "--enable-libass",
                 "--disable-network", "--disable-encoders", "--disable-muxers", "--disable-devices",
                 "--disable-doc", "--disable-debug", "--disable-ffplay",
                 "--extra-cflags=" + ENV["CFLAGS"], "--extra-ldflags=" + ENV["LDFLAGS"]], cwd=src)
        run(["make", "-j" + JOBS], cwd=src)
        run(["make", "install"], cwd=src)
    elif name == "mpv":
        meson(name, ["-Dgpl=true", "-Ddrm=enabled", "-Dgbm=enabled", "-Degl-drm=enabled",
                     "-Dgl=enabled", "-Dalsa=enabled", "-Dlibmpv=false",
                     "-Dlua=disabled", "-Djavascript=disabled", "-Dcplugins=disabled", "-Dx11=disabled",
                     "-Dwayland=disabled", "-Dpulse=disabled", "-Dpipewire=disabled",
                     "-Dbuild-date=false", "-Dmanpage-build=disabled", "-Dhtml-build=disabled"])
    elif name == "fonts":
        src = source(name)
        dest = PREFIX / "share/fonts/airbridge-media"
        dest.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src / "ttf/DejaVuSans.ttf", dest)
        shutil.copy2(src / "LICENSE", dest / "DejaVu-LICENSE")
    elif name == "json-c":
        cmake(name, ["-DBUILD_SHARED_LIBS=ON", "-DBUILD_STATIC_LIBS=OFF"])
    elif name in ("libubox", "ubus"):
        cmake(name, ["-DBUILD_LUA=OFF", "-DBUILD_EXAMPLES=OFF"])
        names = {"libubox": [("libubox.so", "20260213"), ("libblobmsg_json.so", "20260213")],
                 "ubus": [("libubus.so", "20251202")]}[name]
        for library, abi in names:
            path = PREFIX / "lib" / library
            run([WORK / "host/bin/patchelf", "--set-soname", library + "." + abi, path])
            # Dependencies of libubus/blobmsg must use the same actual runtime ABI.
            needed = subprocess.check_output(
                [str(WORK / "host/bin/patchelf"), "--print-needed", str(path)], text=True)
            if "libubox.so\n" in needed:
                run([WORK / "host/bin/patchelf", "--replace-needed", "libubox.so",
                     "libubox.so.20260213", path])
            link = path.with_name(library + "." + abi)
            if not link.exists():
                link.symlink_to(library)
    else:
        raise ValueError(name)
    stamp.write_text(str(time.time()) + "\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fetch", action="store_true")
    parser.add_argument("--only", choices=list(SOURCES))
    args = parser.parse_args()
    for path in (WORK, PREFIX, BUILD, SRC):
        path.mkdir(parents=True, exist_ok=True)
    if args.fetch:
        fetch()
        return
    if not CC.exists() or not LOCK.exists():
        raise SystemExit("Matching toolchain and reviewed source lock required; use --fetch first")
    for name in ([args.only] if args.only else SOURCES):
        build(name)
    print("Private ARM64 prefix:", PREFIX, flush=True)


if __name__ == "__main__":
    main()
