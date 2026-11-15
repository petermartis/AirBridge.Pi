#!/usr/bin/env python3
"""Opt-in device test. Uses generated files, temporary scoped RPC sessions,
and an SSH-encrypted localhost HTTP forward. Never prints session/capability
tokens, reads browser cookies, or uses a password argument.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import shlex
import socket
import subprocess
import time
import urllib.error
import urllib.request


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="root@192.168.1.185")
    parser.add_argument("--allow-write-tests", action="store_true", required=True)
    parser.add_argument("--megabytes", type=int, default=8)
    parser.add_argument("--play-sample", help="Existing library-relative sample for concurrent playback")
    parser.add_argument("--rate-mib", type=float, default=0, help="Optional client MiB/s cap for overlap measurement")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.host.startswith("-") or any(c.isspace() for c in args.host):
        parser.error("Invalid SSH host")
    if not 1 <= args.megabytes <= 1024 or args.output.exists():
        parser.error("Use 1..1024 MiB and a new output path")
    if not 0 <= args.rate_mib <= 1024:
        parser.error("Invalid transfer rate")
    ssh = ["ssh", "-T", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=yes",
           "-o", "ControlPath=/tmp/airbridge-cm5-%C"]
    env_names = []
    created_files = {}
    pending = []
    results = []
    playback = []
    playback_before = None
    opener = urllib.request.build_opener(NoRedirect())

    def local_rpc(method, params):
        # Sensitive dynamic JSON goes through encrypted stdin, not an SSH argv.
        output = subprocess.check_output(
            ssh + [args.host, 'ubus call session ' + method + ' "$(cat)"'],
            input=json.dumps(params).encode(), stderr=subprocess.PIPE, timeout=15)
        return json.loads(output) if output.strip() else {}

    def session(label, methods):
        value = local_rpc("create", {"timeout": 300})["ubus_rpc_session"]
        name = "AIRBRIDGE_FILE_TEST_" + label.upper()
        os.environ[name] = value
        env_names.append(name)
        local_rpc("grant", {"ubus_rpc_session": os.environ[name], "scope": "ubus",
                           "objects": [["airbridge.media", method] for method in methods]})
        return name

    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    rule = f"127.0.0.1:{port}:127.0.0.1:80"
    forward = subprocess.Popen(
        ssh + ["-N", "-o", "ExitOnForwardFailure=yes", "-L", rule, args.host],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    base = f"http://127.0.0.1:{port}"

    def http(path, data, headers=None):
        req = urllib.request.Request(base + path, data=data, method="POST",
                                     headers=headers or {"Content-Type": "application/json"})
        try:
            with opener.open(req, timeout=45) as response:
                return response.status, json.load(response)
        except urllib.error.HTTPError as error:
            try:
                payload = json.load(error)
            except (ValueError, UnicodeError):
                payload = {"ok": False, "error": "HTTP request rejected"}
            return error.code, payload

    def rpc(env, method, params=None, expect_ok=True):
        code, wire = http("/ubus", json.dumps({
            "jsonrpc": "2.0", "id": 1, "method": "call",
            "params": [os.environ[env], "airbridge.media", method, params or {}]
        }).encode())
        result = wire.get("result", [])
        good = code == 200 and len(result) > 1 and result[0] == 0
        reply = result[1] if good else {"ok": False, "error": "RPC permission/transport denied"}
        if expect_ok and not reply.get("ok"):
            raise RuntimeError("Media operation failed: " + method)
        return reply

    def begin(env, path, size):
        reply = rpc(env, "upload_begin", {"path": path, "size": size})
        key = "AIRBRIDGE_FILE_UPLOAD_" + str(len(pending))
        os.environ[key] = reply["token"]
        env_names.append(key)
        pending.append((env, key))
        assert reply["offset"] == 0 and reply["size"] == size
        return key, reply

    def chunk(env, token_env, offset, data):
        return http("/cgi-bin/airbridge-media-upload", data, {
            "Content-Type": "application/octet-stream",
            "Authorization": f"Bearer {os.environ[env]}:{os.environ[token_env]}:{offset}"
        })

    def cancel(env, token_env):
        return rpc(env, "upload_cancel", {"token": os.environ[token_env]})

    def file_hash(path):
        command = "sha256sum " + shlex.quote("/mnt/media/" + path)
        return subprocess.check_output(
            ssh + [args.host, command], stderr=subprocess.PIPE, timeout=30).decode().split()[0]

    def native_media(method, params=None):
        return json.loads(subprocess.check_output(
            ssh + [args.host, 'ubus call airbridge.media ' + method + ' "$(cat)"'],
            input=json.dumps(params or {}).encode(), stderr=subprocess.PIPE, timeout=15))

    try:
        for _ in range(100):
            if forward.poll() is not None:
                raise RuntimeError("SSH HTTP tunnel did not start")
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                    break
            except OSError:
                time.sleep(0.1)
        else:
            raise RuntimeError("SSH HTTP tunnel unavailable")
        read = session("read", ["status", "browse", "storage"])
        write = session("write", ["status", "browse", "storage", "remove",
                                   "upload_begin", "upload_cancel"])
        other = session("other", ["status", "storage", "upload_begin", "upload_cancel"])
        storage = rpc(write, "storage")
        assert storage["writable"] and storage["active_uploads"] == 0
        prefix = "Test/file-management-" + secrets.token_hex(6)
        # Permission failures must happen without filesystem side effects.
        code, _ = http("/cgi-bin/airbridge-media-upload", b"x",
                       {"Content-Type": "application/octet-stream"})
        assert code in (401, 403)
        assert not rpc(read, "upload_begin", {"path": prefix + "-denied.mkv", "size": 1},
                       expect_ok=False)["ok"]
        assert not rpc(read, "remove", {"path": prefix + ".mkv"}, expect_ok=False)["ok"]
        assert not rpc(write, "_upload_claim", {}, expect_ok=False)["ok"]
        results.append("unauthenticated/readonly/private-RPC requests denied")
        for path in ("../escape.mkv", "/etc/escape.mkv", ".airbridge-uploads/escape.mkv"):
            assert not rpc(write, "upload_begin", {"path": path, "size": 1},
                           expect_ok=False)["ok"]
        results.append("upload traversal/absolute/internal paths denied")
        large, _ = begin(write, prefix + "-large-boundary.mkv", 4 * 1024**3 + 123)
        assert cancel(write, large)["canceled"]
        assert cancel(write, large)["canceled"] is False
        results.append(">4GiB size accepted and canceled without a whole-file transfer")
        canceled, _ = begin(write, prefix + "-cancel.mkv", 2 * 1024**2)
        code, _ = chunk(other, canceled, 0, b"wrong owner")
        assert code in (400, 401, 403, 404, 409)
        code, _ = chunk(read, canceled, 0, b"readonly")
        assert code in (401, 403)
        code, reply = chunk(write, canceled, 0, b"a" * 65536)
        assert code == 200 and reply["offset"] == 65536 and not reply["complete"]
        cancel(write, canceled)
        assert not any(entry["path"] == prefix + "-cancel.mkv"
                       for entry in rpc(write, "browse", {"path": "Test", "limit": 200})["entries"])
        results.append("session ownership, partial visibility and cancellation checked")
        size = args.megabytes * 1024**2 + 12345
        path = prefix + ".mkv"
        block = bytes(range(256)) * 4096
        expected_digest = hashlib.sha256()
        for position in range(0, size, len(block)):
            expected_digest.update(block[:min(len(block), size - position)])
        token, reply = begin(write, path, size)
        # Begin confirmed the unique destination did not exist. Cleanup may
        # remove it only if its full bytes match this generated fixture.
        created_files[path] = expected_digest.hexdigest()
        if args.play_sample:
            before = native_media("status")
            if before.get("state") != "idle":
                raise RuntimeError("Concurrent test requires an idle player")
            playback_before = before
            assert native_media("profile", {"name": "quality4k"})["ok"]
            assert native_media("volume", {"value": before.get("volume", 20), "mute": True})["ok"]
            assert native_media("play", {"path": args.play_sample})["ok"]
        offset = 0
        digest = hashlib.sha256()
        start = time.monotonic()
        next_status = start
        while offset < size:
            data = block[:min(len(block), size - offset)]
            code, reply = chunk(write, token, offset, data)
            assert code == 200 and reply.get("ok"), "Chunk request failed"
            digest.update(data)
            offset += len(data)
            assert reply["offset"] == offset and reply["size"] == size
            assert reply["complete"] == (offset == size)
            now = time.monotonic()
            if args.play_sample and now >= next_status:
                status = rpc(write, "status")
                keys = ("state", "time_pos", "width", "height", "hwdec",
                        "dropped_frames", "vo_dropped_frames", "avsync")
                playback.append({key: status.get(key) for key in keys})
                next_status = now + 1
            if args.rate_mib:
                time.sleep(max(0, start + offset / (args.rate_mib * 1024**2) - time.monotonic()))
        wire_elapsed = time.monotonic() - start
        expected = digest.hexdigest()
        assert expected == expected_digest.hexdigest()
        created_files[path] = expected
        assert file_hash(path) == expected
        duplicate = rpc(write, "upload_begin", {"path": path, "size": 1}, expect_ok=False)
        assert not duplicate["ok"] and file_hash(path) == expected
        entries = rpc(write, "browse", {"path": "Test", "limit": 200})["entries"]
        entry = next(item for item in entries if item["path"] == path)
        assert entry["size_bytes"] == size
        elapsed = time.monotonic() - start
        results.append("streamed file published once; exact size/SHA256 verified; duplicate refused")
        rpc(write, "remove", {"path": path})
        del created_files[path]
        assert not any(item["path"] == path
                       for item in rpc(write, "browse", {"path": "Test", "limit": 200})["entries"])
        results.append("generated uploaded file deleted through scoped API")
        report = {"checks": results, "uploaded_bytes": size, "upload_seconds": wire_elapsed,
                  "upload_and_verification_seconds": elapsed,
                  "sha256_verified": True, "full_4GiB_transfer_tested": False}
        if args.play_sample:
            report["concurrent_playback"] = {"sample": args.play_sample, "samples": playback}
        args.output.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        with args.output.open("x") as stream:
            args.output.chmod(0o600)
            json.dump(report, stream, indent=2)
            stream.write("\n")
        print(json.dumps(report, indent=2))
    finally:
        if playback_before:
            try:
                native_media("stop")
                native_media("volume", {"value": playback_before.get("volume", 20),
                                       "mute": bool(playback_before.get("mute"))})
                native_media("profile", {"name": playback_before.get("profile", "quality4k")})
            except Exception:
                pass
        for env, token in pending:
            try:
                cancel(env, token)
            except Exception:
                pass
        for path, expected in created_files.items():
            try:
                if file_hash(path) == expected:
                    rpc(write, "remove", {"path": path})
            except Exception:
                pass
        for name in list(env_names):
            if name.startswith("AIRBRIDGE_FILE_TEST_"):
                try:
                    local_rpc("destroy", {"ubus_rpc_session": os.environ[name]})
                except Exception:
                    pass
            os.environ.pop(name, None)
        try:
            subprocess.run(ssh + ["-O", "cancel", "-L", rule, args.host],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
        except subprocess.TimeoutExpired:
            pass
        forward.terminate()
        try:
            forward.wait(timeout=5)
        except subprocess.TimeoutExpired:
            forward.kill()


if __name__ == "__main__":
    main()
