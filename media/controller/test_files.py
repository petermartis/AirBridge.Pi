#!/usr/bin/env python3
"""Generated-tempmedia tests only. No device, SDK, mount or existing-user-file writes."""
import fcntl
import hashlib
import json
import os
from pathlib import Path
import resource
import re
import signal
import subprocess
import sys
import tempfile
import unittest

FILES, CGI, RELEASE = [str(Path(p).resolve()) for p in sys.argv[1:4]]
del sys.argv[1:4]
SID = "a" * 32
OTHER = "a" * 31 + "1"
TOKEN = "b" * 64
RESERVE = 256 * 1024 * 1024
CHUNK = 1024 * 1024
STATUS_LINE = re.compile(rb"Status: ([1-5][0-9]{2}) ([!-~][ -~]*)")


def cgi_status(headers):
    line = headers.split(b"\r\n", 1)[0]
    # Pinned uhttpd requires a space after the three-digit code. A bare
    # `Status: 401` is silently ignored and becomes HTTP 200 on the wire.
    if not STATUS_LINE.fullmatch(line):
        raise ValueError("CGI Status must have three digits, a space and a nonempty reason phrase")
    return int(line.split(b": ", 1)[1].split()[0])


class StatusHeaderTests(unittest.TestCase):
    def test_valid_reason_phrases_and_numeric_extraction(self):
        for code, reason in ((200, "OK"), (401, "Unauthorized"),
                             (405, "Method Not Allowed"), (507, "Insufficient Storage")):
            self.assertEqual(cgi_status(f"Status: {code} {reason}\r\nContent-Type: application/json".encode()),
                             code)

    def test_bare_status_regression_is_rejected(self):
        for line in (b"Status: 401", b"Status: 401 ", b"Status: 401\tUnauthorized",
                     b"Status: 40 Unauthorized", b"Status: 401Unauthorized",
                     b"Status: 401 Unauthorized\nInjected"):
            with self.subTest(line=line):
                with self.assertRaises(ValueError):
                    cgi_status(line)


class FileTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="airbridge-file-tests-", dir="/tmp")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / "media"
        self.root.mkdir()
        (self.root / ".test-mounted").touch()
        (self.root / ".test-free").write_text(str(8 * 1024**3))
        (self.root / "sub").mkdir()
        self.start()
        self.addCleanup(self.close)

    def start(self, executable=FILES):
        self.process = subprocess.Popen(
            [executable, str(self.root), str(Path(self.tmp.name) / "absent-player.sock")],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    def close(self):
        if self.process.poll() is None:
            self.process.stdin.close()
            self.process.wait(timeout=10)
        errors = self.process.stderr.read()
        for stream in (self.process.stdout, self.process.stderr):
            stream.close()
        if not self.process.stdin.closed:
            self.process.stdin.close()
        self.assertEqual(self.process.returncode, 0, errors)
        self.assertNotIn("AddressSanitizer", errors)
        self.assertNotIn("runtime error:", errors)

    def call(self, method, **args):
        self.process.stdin.write(json.dumps({"method": method, "args": args}) + "\n")
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line:
            self.fail("native file harness exited unexpectedly")
        return json.loads(line)

    def begin(self, path="movie.mkv", size=5):
        result = self.call("upload_begin", path=path, size=size, ubus_rpc_session=SID)
        self.assertTrue(result["ok"], result["error"])
        self.assertEqual(len(result["token"]), 64)
        return result

    def private(self, method, upload, **args):
        return self.call(method, session=SID, token=upload["token"], **args)

    def chunk(self, upload, data, offset=0):
        claim = self.private("_upload_claim", upload, offset=offset, length=len(data))
        self.assertTrue(claim["ok"], claim["error"])
        partial = self.root / ".airbridge-upload" / (upload["token"] + ".part")
        fd = os.open(partial, os.O_WRONLY | os.O_NOFOLLOW)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            checked = self.private("_upload_check", upload, lease=claim["lease"])
            self.assertTrue(checked["ok"], checked["error"])
            self.assertEqual(os.pwrite(fd, data, offset), len(data))
            if offset + len(data) == upload["size"]:
                os.fsync(fd)
        finally:
            os.close(fd)
        return self.private("_upload_done", upload, lease=claim["lease"],
                            offset=offset, length=len(data))

    def test_storage_and_multi_gigabyte_declared_size(self):
        storage = self.call("storage")
        self.assertTrue(storage["mounted"])
        self.assertTrue(storage["writable"])
        self.assertEqual(storage["free_bytes"], 8 * 1024**3)
        self.assertEqual(storage["usable_bytes"], 8 * 1024**3 - RESERVE)
        upload = self.begin(size=5 * 1024**3)
        self.assertEqual(upload["size"], 5 * 1024**3)
        self.assertEqual(self.call("storage")["active_uploads"], 1)
        self.assertLess(self.call("storage")["usable_bytes"], storage["usable_bytes"])

    def test_authorization_is_real_validator_with_fake_transport(self):
        for session in ("b" * 32, "c" * 32, "bad", None):
            result = self.call("upload_begin", path="denied.mkv", size=1,
                               ubus_rpc_session=session)
            self.assertFalse(result["ok"])
        self.assertFalse((self.root / ".airbridge-upload").exists())
        self.assertTrue(self.call("storage")["writable"])

    def test_sizes_offsets_and_paths_rejected(self):
        for size in (0, -1, True, 1.5, 9007199254740992, "12"):
            self.assertFalse(self.call("upload_begin", path="x.mkv", size=size,
                                       ubus_rpc_session=SID)["ok"])
        for path in ("../x.mkv", "/tmp/x.mkv", ".hidden.mkv", "sub/.hidden/x.mkv",
                     "lost+found/x.mkv", "https://x/x.mkv", "x.m3u", "sub//x.mkv",
                     "x.mkv\0tail", "sub/../x.mkv"):
            self.assertFalse(self.call("upload_begin", path=path, size=1,
                                       ubus_rpc_session=SID)["ok"])
        self.assertFalse((self.root / ".airbridge-upload").exists())

    def test_mount_readonly_crossmount_and_root_replacement(self):
        for marker in (".test-unmounted", ".test-readonly", ".test-cross-mount"):
            path = self.root / marker
            path.touch()
            self.assertFalse(self.call("upload_begin", path="refused.mkv", size=1,
                                       ubus_rpc_session=SID)["ok"])
            path.unlink()
        old = self.root.with_name("old-media")
        self.root.rename(old)
        self.root.mkdir()
        (self.root / ".test-mounted").touch()
        self.assertFalse(self.call("storage")["writable"])
        self.assertFalse(self.call("upload_begin", path="wrong.mkv", size=1,
                                   ubus_rpc_session=SID)["ok"])
        self.assertFalse((self.root / "wrong.mkv").exists())

    def test_release_mount_policy_has_no_fixture_bypass(self):
        self.close()
        self.start(RELEASE)
        self.assertFalse(self.call("storage")["writable"])
        self.assertFalse(self.call("upload_begin", path="never.mkv", size=1,
                                   ubus_rpc_session=SID)["ok"])
        self.assertFalse((self.root / ".airbridge-upload").exists())

    def test_symlink_device_directory_and_infrastructure_denied(self):
        (self.root / "link.mkv").symlink_to("/etc/passwd")
        (self.root / "escape").symlink_to(self.root / "sub", target_is_directory=True)
        os.mkfifo(self.root / "fifo.mkv")
        (self.root / "dir.mkv").mkdir()
        for path in ("link.mkv", "fifo.mkv", "dir.mkv"):
            self.assertFalse(self.call("remove", path=path, ubus_rpc_session=SID)["ok"])
            self.assertTrue((self.root / path).exists())
        self.assertFalse(self.call("upload_begin", path="escape/new.mkv", size=1,
                                   ubus_rpc_session=SID)["ok"])

    def test_no_partial_browse_and_exact_completed_hash(self):
        data = bytes(range(256)) * (CHUNK // 256) + b"second chunk tail"
        upload = self.begin("sub/movie.mkv", len(data))
        first = self.chunk(upload, data[:CHUNK])
        self.assertTrue(first["ok"])
        self.assertFalse(first["complete"])
        self.assertFalse((self.root / "sub/movie.mkv").exists())
        self.assertEqual(self.call("browse", path="sub")["entries"], [])
        stage = self.root / ".airbridge-upload"
        self.assertEqual(stage.stat().st_mode & 0o777, 0o700)
        self.assertEqual((stage / (upload["token"] + ".part")).stat().st_mode & 0o777, 0o600)
        final = self.chunk(upload, data[CHUNK:], CHUNK)
        self.assertTrue(final["ok"], final["error"])
        self.assertTrue(final["complete"])
        actual = (self.root / "sub/movie.mkv").read_bytes()
        self.assertEqual(hashlib.sha256(actual).digest(), hashlib.sha256(data).digest())
        self.assertEqual(self.call("browse", path="sub")["entries"][0]["size_bytes"], len(data))
        self.assertEqual(sorted(p.name for p in stage.iterdir()), [".owner"])

    def test_publication_never_overwrites_existing_target(self):
        upload = self.begin(size=3)
        (self.root / "movie.mkv").write_bytes(b"existing generated content")
        result = self.chunk(upload, b"new")
        self.assertFalse(result["ok"])
        self.assertEqual((self.root / "movie.mkv").read_bytes(), b"existing generated content")
        self.assertEqual(self.call("storage")["active_uploads"], 0)

    def test_duplicate_and_concurrency_limits(self):
        a = self.begin("a.mkv")
        self.assertFalse(self.call("upload_begin", path="a.mkv", size=5,
                                   ubus_rpc_session=SID)["ok"])
        self.begin("b.mkv")
        self.assertFalse(self.call("upload_begin", path="c.mkv", size=5,
                                   ubus_rpc_session=SID)["ok"])
        self.assertTrue(self.call("upload_cancel", token=a["token"], ubus_rpc_session=SID)["ok"])
        self.begin("c.mkv")

    def test_owner_session_cancel_and_expiry(self):
        upload = self.begin()
        self.assertFalse(self.call("_upload_claim", session=OTHER, token=upload["token"],
                                   offset=0, length=5)["ok"])
        self.assertFalse(self.call("upload_cancel", token=upload["token"],
                                   ubus_rpc_session=OTHER)["ok"])
        first = self.call("upload_cancel", token=upload["token"], ubus_rpc_session=SID)
        second = self.call("upload_cancel", token=upload["token"], ubus_rpc_session=SID)
        self.assertTrue(first["canceled"])
        self.assertTrue(second["ok"])
        self.assertFalse(second["canceled"])
        upload = self.begin()
        self.assertEqual(self.call("_test_expire")["active_uploads"], 0)
        self.assertFalse(self.private("_upload_claim", upload, offset=0, length=5)["ok"])

    def test_http_injected_private_methods_never_return_fds(self):
        upload = self.begin()
        result = self.call("_upload_claim", session=SID, token=upload["token"],
                           offset=0, length=5, ubus_rpc_session=SID)
        self.assertFalse(result["ok"])
        self.assertNotIn("fd_path", result)

    def test_chunk_offsets_leases_and_lock(self):
        upload = self.begin(size=5)
        for offset, length in ((1, 1), (0, CHUNK + 1), (0, 6), (0, 0), (-1, 2)):
            self.assertFalse(self.private("_upload_claim", upload, offset=offset, length=length)["ok"])
        claim = self.private("_upload_claim", upload, offset=0, length=5)
        self.assertTrue(claim["ok"])
        self.assertFalse(self.private("_upload_claim", upload, offset=0, length=5)["ok"])
        partial = self.root / ".airbridge-upload" / (upload["token"] + ".part")
        fd = os.open(partial, os.O_WRONLY)
        fcntl.flock(fd, fcntl.LOCK_EX)
        os.write(fd, b"12345")
        self.assertFalse(self.private("_upload_done", upload, lease=claim["lease"],
                                      offset=0, length=5)["ok"])
        os.close(fd)
        result = self.private("_upload_done", upload, lease=claim["lease"], offset=0, length=5)
        self.assertTrue(result["ok"], result["error"])
        self.assertFalse(self.private("_upload_done", upload, lease=claim["lease"],
                                      offset=0, length=5)["ok"])

    def test_cancel_between_claim_and_open_retains_old_fd_and_inode(self):
        old = self.begin("old.mkv", 5)
        claim = self.private("_upload_claim", old, offset=0, length=5)
        fd = int(claim["fd_path"].rsplit("/", 1)[1])
        self.assertTrue(self.call("upload_cancel", token=old["token"], ubus_rpc_session=SID)["ok"])
        new = self.begin("new.mkv", 5)
        other = self.private("_upload_claim", new, offset=0, length=5)
        identity = self.call("_test_fd_identity", fd=fd)
        self.assertTrue(identity["ok"])
        self.assertEqual(identity["inode"], claim["inode"])
        self.assertEqual(identity["device"], claim["device"])
        self.assertEqual(identity["links"], 0)
        self.assertNotEqual(other["fd_path"], claim["fd_path"])
        self.assertFalse(self.private("_upload_check", old, lease=claim["lease"])["ok"])
        self.assertFalse(self.private("_upload_done", old, lease=claim["lease"],
                                      offset=0, length=5)["ok"])
        self.assertFalse((self.root / "old.mkv").exists())
        newpart = self.root / ".airbridge-upload" / (new["token"] + ".part")
        self.assertEqual(newpart.read_bytes(), b"")
        self.assertTrue(self.private("_upload_abort", new, lease=other["lease"])["ok"])

    def test_free_space_reserve_and_midupload_loss(self):
        (self.root / ".test-free").write_text(str(RESERVE + 4))
        self.assertFalse(self.call("upload_begin", path="too-big.mkv", size=5,
                                   ubus_rpc_session=SID)["ok"])
        upload = self.begin(size=4)
        (self.root / ".test-free").write_text(str(RESERVE + 3))
        self.assertFalse(self.private("_upload_claim", upload, offset=0, length=4)["ok"])
        self.assertEqual(self.call("storage")["active_uploads"], 0)
        self.assertFalse((self.root / "movie.mkv").exists())

    def test_restart_orphan_cleanup_and_foreign_staging_refusal(self):
        upload = self.begin()
        partial = self.root / ".airbridge-upload" / (upload["token"] + ".part")
        self.process.kill()
        self.process.wait()
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            stream.close()
        self.start()
        self.assertTrue(self.call("storage")["writable"])
        self.assertFalse(partial.exists())
        (self.root / ".airbridge-upload/keep.mkv").write_bytes(b"generated sentinel")
        self.close()
        self.start()
        self.assertFalse(self.call("storage")["writable"])
        self.assertEqual((self.root / ".airbridge-upload/keep.mkv").read_bytes(), b"generated sentinel")

    def test_remove_active_file_then_prune_queue(self):
        path = self.root / "generated.mkv"
        path.write_bytes(b"generated delete fixture")
        self.assertTrue(self.call("queue", action="append", path=path.name)["ok"])
        self.assertTrue(self.call("_test_hold", path=path.name)["ok"])
        result = self.call("remove", path=path.name, ubus_rpc_session=SID)
        self.assertFalse(result["ok"])
        self.assertIn("Stop first", result["error"])
        self.assertTrue(path.exists())
        self.call("_test_release")
        self.assertTrue(self.call("remove", path=path.name, ubus_rpc_session=SID)["ok"])
        self.assertFalse(path.exists())
        self.assertEqual(self.call("queue", action="list")["entries"], [])


class CGITests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="airbridge-cgi-tests-", dir="/tmp")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def request(self, data=b"raw chunk", flags=None, http=None, prefix=b"", limit=None):
        target = self.root / "fixture.part"
        target.write_bytes(prefix)
        target.chmod(0o600)
        body = self.root / "body.bin"
        body.write_bytes(data)
        datafd = os.open(target, os.O_RDWR)
        parentfd = os.open(self.root, os.O_RDONLY | os.O_DIRECTORY)
        trace_read, tracefd = os.pipe()
        env = {
            "REQUEST_METHOD": "POST", "CONTENT_TYPE": "application/octet-stream",
            "CONTENT_LENGTH": str(len(data)), "QUERY_STRING": "", "PATH_INFO": "",
            "HTTP_AUTHORIZATION": f"Bearer {SID}:{TOKEN}:{len(prefix)}",
            "AB_TEST_DATA_FD": str(datafd), "AB_TEST_PARENT_FD": str(parentfd),
            "AB_TEST_TRACE_FD": str(tracefd), "AB_TEST_TOTAL": str(len(prefix) + len(data)),
        }
        if flags:
            env.update(flags)
        if http:
            env.update(http)
        if not flags or "AB_TEST_TOTAL" not in flags:
            try:
                env["AB_TEST_TOTAL"] = str(len(prefix) + max(len(data), int(env["CONTENT_LENGTH"])))
            except ValueError:
                pass
        def restrict():
            if limit is not None:
                resource.setrlimit(resource.RLIMIT_FSIZE, (limit, limit))
        try:
            with body.open("rb") as stream:
                result = subprocess.run([CGI], stdin=stream, capture_output=True, env=env,
                                        pass_fds=(datafd, parentfd, tracefd),
                                        preexec_fn=restrict if limit is not None else None, timeout=10)
                consumed = os.lseek(stream.fileno(), 0, os.SEEK_CUR)
        finally:
            os.close(datafd)
            os.close(parentfd)
            os.close(tracefd)
        trace = bytearray()
        while True:
            part = os.read(trace_read, 4096)
            if not part:
                break
            trace.extend(part)
        os.close(trace_read)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        self.assertNotIn(b"AddressSanitizer", result.stderr)
        headers, payload = result.stdout.split(b"\r\n\r\n", 1)
        self.assertIn(b"Cache-Control: no-store", headers)
        self.assertIn(b"X-Content-Type-Options: nosniff", headers)
        self.assertNotIn(b"Access-Control-Allow", headers)
        self.assertNotIn(TOKEN.encode(), result.stdout)
        return cgi_status(headers), json.loads(payload), consumed, trace.decode(), target

    def test_native_raw_body_positive_exact_hash_and_lower_priority(self):
        data = bytes(range(256)) * 4096
        status, reply, consumed, trace, target = self.request(data)
        self.assertEqual(status, 200)
        self.assertTrue(reply["ok"])
        self.assertTrue(reply["complete"])
        self.assertEqual(reply["offset"], len(data))
        self.assertGreaterEqual(reply["worker_nice"], 10)
        self.assertEqual(consumed, len(data))
        self.assertEqual(hashlib.sha256(target.read_bytes()).digest(), hashlib.sha256(data).digest())
        self.assertEqual(trace.splitlines(), ["access", "_upload_claim", "_upload_check", "access", "_upload_done"])

    def test_denied_auth_and_nonboolean_access_read_no_body(self):
        for flag in ("AB_TEST_DENY_ACL", "AB_TEST_BAD_ACL_TYPE"):
            status, reply, consumed, trace, target = self.request(flags={flag: "1"})
            self.assertEqual(status, 403)
            self.assertFalse(reply["ok"])
            self.assertEqual(consumed, 0)
            self.assertEqual(trace.splitlines(), ["access"])
            self.assertEqual(target.read_bytes(), b"")

    def test_identity_cancel_and_bad_private_paths_read_no_body(self):
        for flag in ("AB_TEST_BAD_TOKEN", "AB_TEST_BAD_ID", "AB_TEST_BAD_PATH", "AB_TEST_CANCELED"):
            status, reply, consumed, trace, target = self.request(flags={flag: "1"})
            self.assertEqual(status, 409)
            self.assertFalse(reply["ok"])
            self.assertEqual(consumed, 0)
            self.assertNotIn("_upload_done", trace)
            self.assertEqual(target.read_bytes(), b"")

    def test_http_validation_before_body(self):
        cases = [
            ({"REQUEST_METHOD": "GET"}, 405),
            ({"CONTENT_TYPE": "application/json"}, 415),
            ({"CONTENT_LENGTH": "1048577"}, 413),
            ({"CONTENT_LENGTH": "0"}, 413),
            ({"CONTENT_LENGTH": "-1"}, 413),
            ({"QUERY_STRING": "offset=0"}, 400),
            ({"PATH_INFO": "/extra"}, 400),
            ({"HTTP_AUTHORIZATION": f"Bearer {SID}:{TOKEN}"}, 401),
        ]
        for headers, expected in cases:
            status, reply, consumed, trace, target = self.request(http=headers)
            self.assertEqual(status, expected)
            self.assertFalse(reply["ok"])
            self.assertEqual(consumed, 0)
            self.assertEqual(trace, "")
            self.assertEqual(target.read_bytes(), b"")

    def test_truncation_write_limit_and_permission_expiry_abort(self):
        status, reply, _, trace, _ = self.request(b"short", http={"CONTENT_LENGTH": "20"})
        self.assertEqual(status, 409)
        self.assertFalse(reply["ok"])
        self.assertIn("_upload_abort", trace)
        self.assertNotIn("_upload_done", trace)
        status, reply, _, trace, target = self.request(b"12345", limit=2)
        self.assertEqual(status, 409)
        self.assertFalse(reply["ok"])
        self.assertIn("_upload_abort", trace)
        self.assertLessEqual(target.stat().st_size, 2)
        status, reply, _, trace, _ = self.request(flags={"AB_TEST_EXPIRE_ACL": "1"})
        self.assertEqual(status, 409)
        self.assertFalse(reply["ok"])
        self.assertIn("_upload_abort", trace)
        self.assertNotIn("_upload_done", trace)

    def test_continuation_and_no_silent_duplicate_offset(self):
        status, reply, consumed, _, target = self.request(b"tail", prefix=b"head")
        self.assertEqual(status, 200)
        self.assertTrue(reply["complete"])
        self.assertEqual(reply["offset"], 8)
        self.assertEqual(consumed, 4)
        self.assertEqual(target.read_bytes(), b"headtail")
        status, reply, consumed, _, target = self.request(b"tail", prefix=b"head",
                                                        http={"HTTP_AUTHORIZATION": f"Bearer {SID}:{TOKEN}:0"})
        self.assertEqual(status, 409)
        self.assertFalse(reply["ok"])
        self.assertEqual(consumed, 0)
        self.assertEqual(target.read_bytes(), b"head")

    def test_nonfinal_chunk_keeps_sequential_progress(self):
        data = bytes(range(256)) * 256
        status, reply, consumed, trace, target = self.request(
            data, flags={"AB_TEST_TOTAL": str(len(data) * 2)})
        self.assertEqual(status, 200)
        self.assertTrue(reply["ok"])
        self.assertFalse(reply["complete"])
        self.assertEqual(reply["offset"], len(data))
        self.assertEqual(consumed, len(data))
        self.assertEqual(target.read_bytes(), data)
        self.assertIn("_upload_done", trace)


class PackageTests(unittest.TestCase):
    def test_r4_payload_metadata_and_no_release_test_sources(self):
        here = Path(__file__).resolve().parent
        recipe = here.parents[1] / "openwrt/packages/airbridge-media/Makefile"
        text = recipe.read_text()
        for source in ("files.c", "fs_mount.c", "upload_protocol.c", "upload_auth.c",
                       "upload_rpc.c", "upload_io.c", "upload_cgi.c", "held_fd.c"):
            self.assertIn("$(PKG_BUILD_DIR)/" + source, text)
        self.assertNotIn("tests/", text)
        self.assertNotIn("host.c", text)
        self.assertNotIn("TESTFS", text)
        self.assertIn("$(PKG_BUILD_DIR)/airbridge-media-upload $(1)/www/cgi-bin/", text)
        with tempfile.TemporaryDirectory(prefix="airbridge-package-metadata-") as temporary:
            root = Path(temporary)
            includes = root / "include"
            includes.mkdir()
            (root / "rules.mk").write_text("")
            (includes / "package.mk").write_text(
                "define BuildPackage\n$(eval $(Package/$(1)))\n"
                "$(info NAME=$(1))\n$(info RELEASE=$(PKG_RELEASE))\n"
                "$(info DEPENDS=$(DEPENDS))\nendef\nall: ;\n")
            result = subprocess.run(
                ["make", "--no-print-directory", "-f", str(recipe),
                 "TOPDIR=" + str(root), "INCLUDE_DIR=" + str(includes), "all"],
                text=True, capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("NAME=airbridge-media", result.stdout)
        release = re.search(r"(?m)^PKG_RELEASE:=(\d+)$", text)
        self.assertIsNotNone(release)
        self.assertGreaterEqual(int(release[1]), 4)
        self.assertIn("RELEASE=" + release[1], result.stdout)
        self.assertIn("+airbridge-media-engine", result.stdout)


if __name__ == "__main__":
    unittest.main()
