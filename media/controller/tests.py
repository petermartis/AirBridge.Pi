#!/usr/bin/env python3
"""Hardware-free native core tests; Python is never shipped to OpenWrt."""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest

BINARY = str(Path(sys.argv.pop(1)).resolve())


class FakeIPC:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.bind(str(path))
        self.socket.listen(16)
        self.socket.settimeout(0.1)
        self.commands = []
        self.hardware = "drm"
        self.mode = "normal"
        self.idle = True
        self.loaded = ""
        self.pause = False
        self.delay_queries = 0
        self.queued_event = None
        self.send_loaded = True
        self.load_error = False
        self.properties = {
            "time-pos": 12.5, "duration": 120, "volume": 50, "mute": False,
            "track-list": [{"id": 1, "type": "audio", "lang": "eng", "selected": True}],
            "video-codec": "hevc", "width": 3840, "height": 2160,
            "decoder-frame-drop-count": 2, "avsync": 0.002,
        }
        self.done = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while not self.done.is_set():
            try:
                conn, _ = self.socket.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            with conn:
                conn.settimeout(2)
                self.serve(conn)

    def serve(self, conn):
        with conn.makefile("rb") as stream:
            while not self.done.is_set():
                try:
                    raw = stream.readline(8193)
                    if not raw:
                        return
                    request = json.loads(raw)
                    command = request["command"]
                    self.commands.append(command)
                    if self.queued_event:
                        conn.sendall(json.dumps(self.queued_event).encode() + b"\n")
                        self.queued_event = None
                    if self.mode == "silent":
                        time.sleep(0.8)
                        continue
                    if self.mode == "oversized":
                        conn.sendall(b"x" * 70000)
                        continue
                    if self.mode == "malformed":
                        conn.sendall(b'{"error":null,"request_id":' +
                                     str(request["request_id"]).encode() + b"}\n")
                        continue
                    data = None
                    error = "success"
                    if command[0] == "get_property":
                        name = command[1]
                        if name == "idle-active":
                            data = self.idle
                        elif name == "hwdec-current":
                            data = self.hardware if not self.delay_queries else "no"
                            self.delay_queries = max(0, self.delay_queries - 1)
                        elif name == "path":
                            data = self.loaded
                        elif name == "pause":
                            data = self.pause
                        elif name in self.properties:
                            data = self.properties[name]
                        else:
                            error = "property unavailable"
                    elif command[0] == "set_property":
                        if command[1] == "pause":
                            self.pause = command[2]
                        else:
                            self.properties[command[1]] = command[2]
                    elif command[0] == "loadfile":
                        self.loaded = command[1]
                        self.idle = False
                        self.pause = command[4]["pause"] == "yes"
                        if self.load_error:
                            self.idle = True
                            conn.sendall(b'{"event":"end-file","reason":"error","file_error":"unrecognized file format"}\n')
                        elif self.send_loaded:
                            conn.sendall(b'{"event":"file-loaded"}\n')
                    elif command[0] == "stop":
                        self.idle = True
                    payload = json.dumps({"request_id": request["request_id"],
                                          "error": error, "data": data}).encode() + b"\n"
                    conn.sendall(b'{"event":"idle"}\n' + payload[:7])
                    conn.sendall(payload[7:])
                except (OSError, ValueError, KeyError):
                    return

    def close(self):
        self.done.set()
        self.socket.close()
        self.thread.join(3)


class ControllerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="abmedia-", dir="/tmp")
        self.root = Path(self.tmp.name) / "media"
        self.root.mkdir()
        (self.root / "sub").mkdir()
        (self.root / "movie.mkv").write_bytes(b"fake media")
        (self.root / "sub" / "clip.mp4").write_bytes(b"fake media")
        (self.root / "playlist.m3u").write_text("/etc/passwd\n")
        (self.root / "link.mkv").symlink_to("/etc/passwd")
        (self.root / "escape").symlink_to("/etc", target_is_directory=True)
        os.mkfifo(self.root / "pipe.mkv")
        self.fake = FakeIPC(Path(self.tmp.name) / "mpv.sock")
        self.process = subprocess.Popen(
            [BINARY, str(self.root), str(Path(self.tmp.name) / "mpv.sock")],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True,
        )

    def tearDown(self):
        self.process.stdin.close()
        self.process.wait(timeout=10)
        errors = self.process.stderr.read()
        self.process.stdout.close()
        self.process.stderr.close()
        self.fake.close()
        self.tmp.cleanup()
        self.assertEqual(self.process.returncode, 0, errors)
        self.assertNotIn("runtime error:", errors)
        self.assertNotIn("AddressSanitizer", errors)

    def call(self, method, **args):
        self.process.stdin.write(json.dumps({"method": method, "args": args}) + "\n")
        self.process.stdin.flush()
        return json.loads(self.process.stdout.readline())

    def test_browse_and_repeat_root(self):
        for _ in range(3):
            result = self.call("browse", path="")
            self.assertTrue(result["ok"])
            self.assertEqual({e["path"] for e in result["entries"]}, {"sub", "movie.mkv"})
            self.assertFalse(result["more"])
        result = self.call("browse", path="sub")
        self.assertEqual(result["entries"], [{"name": "clip.mp4", "path": "sub/clip.mp4", "type": "file", "size_bytes": 10}])

    def test_pagination(self):
        first = self.call("browse", path="", offset=0, limit=1)
        second = self.call("browse", path="", offset=1, limit=1)
        self.assertTrue(first["more"])
        self.assertFalse(second["more"])
        self.assertNotEqual(first["entries"], second["entries"])
        for args in [{"limit": 201}, {"offset": -1}, {"offset": 0.1}, {"path": 1}]:
            self.assertFalse(self.call("browse", **args)["ok"])

    def test_path_security(self):
        for path in ["../movie.mkv", "/etc/passwd", "https://a/movie.mkv", "link.mkv",
                     "escape/passwd", "playlist.m3u", "pipe.mkv", "sub/../movie.mkv",
                     "sub//clip.mp4", "./movie.mkv", "movie.mkv/", "a\\b.mkv",
                     "movie.mkv\u0000evil", "bad\n.mkv"]:
            with self.subTest(path=path):
                self.assertFalse(self.call("play", path=path)["ok"])
        self.assertEqual(self.fake.commands, [])
        self.assertFalse(self.call("browse", path="escape")["ok"])

    def test_scan_bound(self):
        for index in range(4100):
            (self.root / f".hidden{index}").touch()
        result = self.call("browse", path="", limit=200)
        self.assertTrue(result["ok"])
        self.assertTrue(result["scan_limited"])
        self.assertTrue(result["more"])

    def test_output_byte_bound(self):
        relative = "/".join(['"' * 190] * 4)
        deep = self.root / relative
        deep.mkdir(parents=True)
        for index in range(200):
            (deep / ("x" * 99 + str(index) + ".mkv")).touch()
        first = self.call("browse", path=relative, limit=200)
        self.assertTrue(first["ok"])
        self.assertTrue(first["more"])
        self.assertLess(len(json.dumps(first).encode()), 210000)
        self.assertGreater(len(first["entries"]), 0)
        self.assertLess(len(first["entries"]), 200)
        second = self.call("browse", path=relative, offset=len(first["entries"]), limit=200)
        self.assertTrue(second["ok"])
        paths = [entry["path"] for entry in first["entries"] + second["entries"]]
        self.assertEqual(len(paths), len(set(paths)))

    def test_shell_metacharacters_are_data(self):
        name = "movie ' ; $(id).mkv"
        (self.root / name).write_bytes(b"fake media")
        self.assertTrue(self.call("play", path=name)["ok"])
        self.assertEqual(self.call("status")["title"], name)
        load = next(c for c in self.fake.commands if c[0] == "loadfile")
        self.assertNotIn(name, json.dumps(load))

    def test_required_hardware_and_telemetry(self):
        self.fake.delay_queries = 3
        self.assertTrue(self.call("play", path="movie.mkv")["ok"])
        load = next(c for c in self.fake.commands if c[0] == "loadfile")
        self.assertRegex(load[1], r"^/proc/[0-9]+/fd/[0-9]+$")
        self.assertEqual(load[2:4], ["replace", -1])
        self.assertEqual(load[4], {"demuxer": "lavf", "demuxer-lavf-format": "matroska",
                                  "access-references": "no", "pause": "yes"})
        status = self.call("status")
        self.assertEqual(status["state"], "playing")
        self.assertEqual(status["profile"], "quality4k")
        self.assertEqual(status["codec"], "hevc")
        self.assertEqual(status["width"], 3840)
        self.assertEqual(status["hwdec"], "drm")
        self.assertEqual(status["title"], "movie.mkv")
        self.assertFalse(status["verified_4k"])
        self.assertTrue(status["capabilities"]["mute"])
        self.assertFalse(status["capabilities"]["hardware_validation"])
        self.assertTrue(self.call("pause", paused=True)["ok"])
        self.assertEqual(self.call("status")["state"], "paused")
        self.assertTrue(self.call("stop")["ok"])
        self.assertEqual(self.call("status")["state"], "idle")

    def test_hardware_missing_fails_closed(self):
        self.fake.hardware = "no"
        result = self.call("play", path="movie.mkv")
        self.assertFalse(result["ok"])
        self.assertIn("requires an active hardware decoder", result["error"])
        self.assertEqual(self.fake.commands[-1], ["stop"])
        self.assertFalse(any(c == ["set_property", "pause", False] for c in self.fake.commands))
        self.assertIn("requires", self.call("status")["error"])

    def test_hardware_watchdog(self):
        self.assertTrue(self.call("play", path="movie.mkv")["ok"])
        self.fake.hardware = "no"
        result = self.call("_test_guard")
        self.assertEqual(result["state"], "idle")
        self.assertIn("required hardware", result["error"])
        self.assertIn(["stop"], self.fake.commands)

    def test_hardware_lost_stops(self):
        self.assertTrue(self.call("play", path="movie.mkv")["ok"])
        self.fake.hardware = "no"
        self.assertEqual(self.call("status")["state"], "idle")
        self.assertEqual(self.fake.commands[-1], ["stop"])

    def test_diagnostic_profile_and_controls(self):
        self.fake.hardware = "no"
        self.assertTrue(self.call("profile", name="quiet")["ok"])
        self.assertEqual(self.fake.properties["vd-lavc-threads"], 2)
        self.assertTrue(self.call("play", path="sub/clip.mp4")["ok"])
        for method, args in [
            ("seek", {"seconds": 2.5, "relative": False}),
            ("seek", {"seconds": -5, "relative": True}),
            ("volume", {"value": 42.5, "mute": True}),
            ("tracks", {"type": "audio", "id": 1}),
            ("tracks", {"type": "sub", "id": 0}),
        ]:
            self.assertTrue(self.call(method, **args)["ok"])
        self.assertEqual(self.fake.properties["volume"], 42.5)
        self.assertTrue(self.fake.properties["mute"])
        self.assertEqual(self.fake.properties["sid"], "no")

    def test_invalid_controls_no_ipc(self):
        for method, args in [
            ("volume", {"value": 101}), ("volume", {"value": True}),
            ("volume", {"value": 20, "mute": 1}), ("pause", {"paused": 1}),
            ("seek", {"seconds": -1, "relative": False}),
            ("seek", {"seconds": 1, "relative": 1}),
            ("tracks", {"type": "video", "id": 1}),
            ("tracks", {"type": "audio", "id": 1.5}),
            ("profile", {"name": "user-custom"}), ("command", {"command": ["quit"]}),
        ]:
            self.assertFalse(self.call(method, **args)["ok"])
        self.assertEqual(self.fake.commands, [])

    def test_actual_runtime_player_error(self):
        self.assertTrue(self.call("profile", name="balanced")["ok"])
        self.assertTrue(self.call("play", path="movie.mkv")["ok"])
        self.fake.idle = True
        self.fake.queued_event = {"event": "end-file", "reason": "error",
                                  "file_error": "decoder failed"}
        status = self.call("status")
        self.assertEqual(status["state"], "idle")
        self.assertIn("decoder failed", status["error"])

    def test_actual_load_error(self):
        self.fake.load_error = True
        result = self.call("play", path="movie.mkv")
        self.assertFalse(result["ok"])
        self.assertIn("unrecognized file format", result["error"])
        self.assertEqual(self.fake.commands[-1], ["stop"])

    def test_loaded_event_required(self):
        self.fake.send_loaded = False
        self.assertFalse(self.call("play", path="movie.mkv")["ok"])
        self.assertEqual(self.fake.commands[-1], ["stop"])
        self.assertFalse(self.call("pause", paused=False)["ok"])

    def test_queue(self):
        self.assertEqual(self.call("queue", action="list")["entries"], [])
        self.assertFalse(self.call("queue", action="append", path="link.mkv")["ok"])
        self.assertTrue(self.call("queue", action="append", path="movie.mkv")["ok"])
        self.assertEqual(self.call("queue", action="list")["entries"], ["movie.mkv"])
        self.assertTrue(self.call("queue", action="next")["ok"])
        self.assertEqual(self.call("queue", action="list")["entries"], [])
        self.assertFalse(self.call("queue", action="next")["ok"])
        for _ in range(128):
            self.assertTrue(self.call("queue", action="append", path="movie.mkv")["ok"])
        self.assertFalse(self.call("queue", action="append", path="movie.mkv")["ok"])
        self.assertEqual(self.call("queue", action="clear")["entries"], [])

    def test_unavailable_is_explicit(self):
        self.fake.close()
        result = self.call("status")
        self.assertTrue(result["ok"])
        self.assertFalse(result["available"])
        self.assertEqual(result["state"], "unavailable")
        self.assertIsNone(result["hwdec"])
        self.assertFalse(result["capabilities"]["play"])
        self.assertTrue(result["capabilities"]["browse"])

    def test_bounded_silent_ipc(self):
        self.fake.mode = "silent"
        start = time.monotonic()
        self.assertFalse(self.call("pause", paused=True)["ok"])
        self.assertLess(time.monotonic() - start, 2)

    def test_singleton_lock(self):
        lock = Path(self.tmp.name) / "singleton.lock"
        held = subprocess.Popen([BINARY, "--lock", str(lock)], stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertEqual(held.stdout.readline(), "locked\n")
        try:
            attempt = subprocess.run([BINARY, "--lock", str(lock)], input="", text=True,
                                     capture_output=True, timeout=2)
            self.assertEqual(attempt.returncode, 1)
            self.assertEqual(lock.stat().st_mode & 0o777, 0o600)
        finally:
            held.communicate("", timeout=2)
            self.assertEqual(held.returncode, 0)
        attempt = subprocess.run([BINARY, "--lock", str(lock)], input="", text=True,
                                 capture_output=True, timeout=2)
        self.assertEqual(attempt.returncode, 0, attempt.stderr)

    def test_hostile_lock_files(self):
        for file in ["link.mkv", "pipe.mkv"]:
            start = time.monotonic()
            attempt = subprocess.run([BINARY, "--lock", str(self.root / file)], input="",
                                     text=True, capture_output=True, timeout=2)
            self.assertEqual(attempt.returncode, 1)
            self.assertLess(time.monotonic() - start, 2)

    def test_oversized_and_null_error_ipc(self):
        for mode in ["oversized", "malformed"]:
            self.fake.mode = mode
            self.assertFalse(self.call("pause", paused=True)["ok"])


if __name__ == "__main__":
    unittest.main()
