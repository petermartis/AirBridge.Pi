from pathlib import Path
import os
import subprocess
import sys
import time

env = dict(os.environ)
env["PATH"] = (
    "/build/staging_dir/toolchain-aarch64_cortex-a76_gcc-14.3.0_musl/bin:"
    "/build/staging_dir/host/bin:" + env["PATH"]
)
package = sys.argv[1] if len(sys.argv) > 1 else "package/ma86xh-rtw89"
cmd = [
    "make", "-C", package, "TOPDIR=/build",
    "STAGING_DIR=/build/staging_dir/target-aarch64_cortex-a76_musl",
    "-j1", "compile", "V=s",
]
if "mac80211" in package:
    cmd += ["BUILD_VARIANT=regular", "ALL_VARIANTS=regular smallbuffers"]
log = Path("/build/" + ("mac80211-stage.log" if "mac80211" in package else "diag-build.log"))
last_progress = time.monotonic()
with log.open("w") as output:
    proc = subprocess.Popen(
        cmd, cwd="/build", env=env, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, bufsize=1,
    )
    for line in proc.stdout:
        output.write(line)
        output.flush()
        if (
            "error:" in line or line.startswith("ERROR:")
            or "warning:" in line or "ABTX trace" in line
            or line.lstrip().startswith(("CC ", "LD ", "MODPOST ", "BTF "))
            or time.monotonic() - last_progress > 25
        ):
            print(line.rstrip()[:700], flush=True)
            last_progress = time.monotonic()
    status = proc.wait()
print(f"Build exited {status}; full log: {log}", flush=True)
sys.exit(status)
