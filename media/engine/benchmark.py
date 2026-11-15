#!/usr/bin/env python3
"""Measure the installed typed media API; never disables frame dropping.

Uses an already-authenticated SSH session/key, not passwords. Plays a selected
library-relative sample with audio muted, records truthful counters, then stops
and restores the previous volume/mute. Output is private and never overwritten.
"""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import time


def summarize(samples):
    playing = [s for s in samples if s.get("state") == "playing"
               and isinstance(s.get("time_pos"), (int, float))]
    steady = [s for s in playing if s["time_pos"] >= 5]
    result = {"samples": len(samples), "playing_samples": len(playing)}
    if not steady:
        result["error"] = "No measured steady-state window"
        return result
    first, last = steady[0], steady[-1]
    sync = [abs(s["avsync"]) for s in steady if isinstance(s.get("avsync"), (int, float))]
    result.update({
        "steady_start_seconds": first["time_pos"],
        "steady_end_seconds": last["time_pos"],
        "hwdec_values": sorted({str(s.get("hwdec")) for s in playing}),
        "resolutions": sorted({f'{s.get("width")}x{s.get("height")}' for s in playing}),
        "max_steady_abs_avsync": max(sync) if sync else None,
    })
    for key in ("dropped_frames", "vo_dropped_frames"):
        values = [s[key] for s in steady if isinstance(s.get(key), (int, float))]
        if values:
            result[key + "_at_steady_start"] = values[0]
            result[key + "_steady_delta"] = values[-1] - values[0] if values[-1] >= values[0] else None
            if any(b < a for a, b in zip(values, values[1:])):
                result["counter_reset_detected"] = True
                result[key + "_steady_delta"] = None
            result[key + "_max"] = max(s[key] for s in playing
                                      if isinstance(s.get(key), (int, float)))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="root@192.168.1.185")
    parser.add_argument("--sample", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seconds", type=int, default=26)
    args = parser.parse_args()
    if args.host.startswith("-") or any(c.isspace() for c in args.host):
        parser.error("Invalid SSH host")
    if args.seconds < 8 or args.seconds > 600:
        parser.error("Seconds must be 8..600")
    if args.output.exists():
        parser.error("Refusing to overwrite a benchmark record")
    ssh = ["ssh", "-T", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=yes",
           "-o", "ControlPath=/tmp/airbridge-cm5-%C", args.host]

    def call(method, params=None):
        command = "ubus call airbridge.media " + method
        if params is not None:
            command += " " + shlex.quote(json.dumps(params, separators=(",", ":")))
        reply = json.loads(subprocess.check_output(ssh + [command], stderr=subprocess.PIPE, timeout=15))
        if not reply.get("ok"):
            raise RuntimeError(reply.get("error") or "Media API failure")
        return reply

    deadline = time.monotonic() + 10
    while True:
        try:
            before = call("status")
            if before.get("available"):
                break
        except (subprocess.CalledProcessError, json.JSONDecodeError):
            pass
        if time.monotonic() >= deadline:
            raise SystemExit("Media service did not become available")
        time.sleep(0.25)
    if before.get("state") not in ("idle", "unavailable"):
        raise SystemExit("Stop existing playback before benchmarking")
    samples = []
    try:
        call("volume", {"value": before.get("volume", 20), "mute": True})
        call("play", {"path": args.sample})
        start = time.monotonic()
        for i in range(args.seconds):
            time.sleep(max(0, start + i - time.monotonic()))
            status = call("status")
            fields = ("state", "time_pos", "width", "height", "hwdec",
                      "dropped_frames", "vo_dropped_frames", "avsync", "profile")
            sample = {key: status.get(key) for key in fields}
            sample["wall_seconds"] = round(time.monotonic() - start, 3)
            samples.append(sample)
            if status.get("state") != "playing":
                break
    finally:
        call("stop")
        call("volume", {"value": before.get("volume", 20), "mute": bool(before.get("mute"))})
    result = {"sample": args.sample, "samples": samples, "summary": summarize(samples),
              "frame_drop_detection_disabled": False}
    args.output.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    with args.output.open("x") as stream:
        args.output.chmod(0o600)
        json.dump(result, stream, indent=2)
        stream.write("\n")
    print(json.dumps(result["summary"], indent=2))


if __name__ == "__main__":
    main()
