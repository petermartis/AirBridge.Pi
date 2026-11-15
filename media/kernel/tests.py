#!/usr/bin/env python3
"""Host-only checks; no network, SDK writes, kernel builds or device access."""
import contextlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

import recover
import check_kernel

HERE = Path(__file__).resolve().parent
CACHE = HERE / "patches"
RECIPE = HERE.parents[1] / "openwrt/packages/kmod-airbridge-hevc/Makefile"


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="hevc-test-")
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def copy_cache(self):
        return Path(shutil.copytree(CACHE, self.root / "patches"))

    def test_deterministic_source_and_provenance(self):
        files = recover.recover(CACHE)
        self.assertEqual(files, recover.recover(CACHE))
        self.assertEqual(set(files), set(recover.FILES) | {"Makefile"})
        golden = json.loads((HERE / "provenance.json").read_text())
        actual = recover.metadata(files)
        self.assertEqual(golden["files"], actual["files"])
        self.assertEqual(golden["kernel_abi"], actual["kernel_abi"])
        self.assertEqual(golden["openwrt_revision"], actual["openwrt_revision"])
        for expected, actual_patch in zip(golden["patches"], actual["patches"]):
            for key in ("name", "url", "sha256", "bytes"):
                self.assertEqual(expected[key], actual_patch[key])
        self.assertFalse(golden["hardware_validated"])
        self.assertFalse(golden["kernel_configuration_modified"])
        self.assertFalse(golden["autoload"])

    def test_four_patch_behavior_and_source_license(self):
        files = recover.recover(CACHE)
        for name in recover.FILES:
            self.assertIn(b"SPDX-License-Identifier: GPL-2.0", files[name][:512])
        self.assertIn(b"static int video_nr = 19;", files["hevc_d.c"])
        self.assertIn(b"//V4L2_PIX_FMT_NV12MT_COL128,", files["hevc_d_video.c"])
        self.assertIn(b"V4L2_PIX_FMT_NV12_COL128", files["hevc_d_h265.c"])
        self.assertIn(b"obj-m += rpi-hevc-dec.o", files["Makefile"])
        self.assertNotIn(b"CONFIG_VIDEO_RPI_HEVC_DEC", files["Makefile"])
        self.assertNotIn("Kconfig", files)
        self.assertNotIn("MAINTAINERS", files)
        license_data = (HERE / "LICENSES/GPL-2.0").read_bytes()
        self.assertEqual(recover.sha256(license_data),
                         "f6b78c087c3ebdf0f3c13415070dd480a3f35d8fc76f3d02180a407c1c812f79")

    def test_recovery_cli_is_offline_and_writes_only_allowed_files(self):
        output = self.root / "recovered"
        with mock.patch("urllib.request.urlopen", side_effect=AssertionError("network forbidden")):
            with mock.patch("socket.create_connection", side_effect=AssertionError("network forbidden")):
                with contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(recover.main(["verify", "--cache", str(CACHE)]), 0)
                    self.assertEqual(recover.main(["recover", "--cache", str(CACHE),
                                                   "--output", str(output)]), 0)
        self.assertEqual({p.name for p in output.iterdir()},
                         set(recover.FILES) | {"Makefile", ".source-provenance.json"})
        metadata = json.loads((output / ".source-provenance.json").read_text())
        self.assertEqual(metadata["files"], recover.metadata(recover.recover(CACHE))["files"])
        self.assertEqual(output.stat().st_mode & 0o777, 0o755)
        for path in output.iterdir():
            self.assertEqual(path.stat().st_mode & 0o777, 0o644)

    def test_complete_fetch_cache_does_not_use_network(self):
        with mock.patch("urllib.request.urlopen", side_effect=AssertionError("network forbidden")):
            recover.fetch(CACHE)

    def test_each_patch_is_hash_checked(self):
        cache = self.copy_cache()
        for name, _, _ in recover.PATCHES:
            path = cache / name
            original = path.read_bytes()
            bad = bytearray(original)
            bad[-8] ^= 1
            path.write_bytes(bad)
            with self.subTest(name=name):
                with self.assertRaisesRegex(recover.RecoveryError, "SHA-256 mismatch"):
                    recover.recover(cache)
            path.write_bytes(original)

    def test_truncated_oversized_and_missing_cache_refused(self):
        cache = self.copy_cache()
        name, _, _ = recover.PATCHES[0]
        path = cache / name
        original = path.read_bytes()
        for altered in (original[:-1], original + b"x"):
            path.write_bytes(altered)
            with self.assertRaises(recover.RecoveryError):
                recover.recover(cache)
        path.unlink()
        with self.assertRaises(recover.RecoveryError):
            recover.recover(cache)
        output = self.root / "must-not-appear"
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(recover.main(["recover", "--cache", str(cache),
                                           "--output", str(output)]), 1)
        self.assertFalse(output.exists())

    def test_symlink_and_fifo_cache_refused(self):
        cache = self.copy_cache()
        name, _, _ = recover.PATCHES[0]
        path = cache / name
        path.unlink()
        path.symlink_to(CACHE / name)
        with self.assertRaises(recover.RecoveryError):
            recover.recover(cache)
        path.unlink()
        os.mkfifo(path)
        with self.assertRaises(recover.RecoveryError):
            recover.recover(cache)

    def test_fetch_refuses_to_overwrite_bad_cache(self):
        cache = self.copy_cache()
        name, _, _ = recover.PATCHES[0]
        (cache / name).write_bytes(b"not upstream")
        with mock.patch("urllib.request.urlopen", side_effect=AssertionError("network forbidden")):
            with self.assertRaises(recover.RecoveryError):
                recover.fetch(cache)
        self.assertEqual((cache / name).read_bytes(), b"not upstream")

    def test_existing_output_and_symlink_are_never_replaced(self):
        files = recover.recover(CACHE)
        existing = self.root / "existing"
        existing.mkdir()
        sentinel = existing / "keep"
        sentinel.write_text("user data")
        link = self.root / "link"
        link.symlink_to(existing, target_is_directory=True)
        for output in (existing, sentinel, link):
            with self.subTest(output=output):
                with self.assertRaises(OSError):
                    recover.write_output(output, files)
        self.assertEqual(sentinel.read_text(), "user data")
        self.assertTrue(link.is_symlink())

    def test_output_whitelist(self):
        files = recover.recover(CACHE)
        files["../escape"] = b"forbidden"
        output = self.root / "refused"
        with self.assertRaises(recover.RecoveryError):
            recover.write_output(output, files)
        self.assertFalse(output.exists())
        self.assertFalse((self.root / "escape").exists())

    def test_patch_path_restrictions(self):
        for path in ("a/../escape", "/etc/passwd", "a//etc", "b/a/./x",
                     "b/a\\x", "a/"):
            with self.subTest(path=path):
                with self.assertRaises(recover.RecoveryError):
                    recover.relative_target(path)
        bad = ("--- /dev/null\n+++ b/" + recover.PREFIX +
               "unrequested.c\n@@ -0,0 +1 @@\n+unrequested\n").encode()
        with self.assertRaises(recover.RecoveryError):
            recover.apply_patch({}, bad)

    def test_no_fuzz_or_offset_patch_application(self):
        files = {}
        first, second = recover.PATCHES[:2]
        recover.apply_patch(files, recover.read_patch(CACHE, *first))
        old = files["hevc_d_h265.c"]
        changed = old.replace("\tde->cmd_len = 0;", "\tde->cmd_len = 1;", 1)
        self.assertNotEqual(changed, old)
        files["hevc_d_h265.c"] = changed
        with self.assertRaisesRegex(recover.RecoveryError, "context mismatch"):
            recover.apply_patch(files, recover.read_patch(CACHE, *second))


class PreparedKernelTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="hevc-kernel-identity-test-")
        self.root = Path(self.tmp.name)
        self.expected = {}
        for name in check_kernel.FINGERPRINTS:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            data = (name + "\n").encode()
            path.write_bytes(data)
            self.expected[name] = recover.sha256(data)

    def tearDown(self):
        self.tmp.cleanup()

    def test_real_pins_match_documented_evidence(self):
        metadata = json.loads((HERE / "provenance.json").read_text())
        self.assertEqual(metadata["existing_kernel_prerequisites"]["prepared_file_sha256"],
                         check_kernel.FINGERPRINTS)

    def test_check_is_read_only(self):
        before = {name: (self.root / name).read_bytes() for name in self.expected}
        with mock.patch.object(check_kernel, "FINGERPRINTS", self.expected):
            self.assertEqual(check_kernel.check(self.root), self.expected)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(check_kernel.main(["--kernel-tree", str(self.root)]), 0)
        self.assertEqual(before, {name: (self.root / name).read_bytes()
                                  for name in self.expected})

    def test_changed_prepared_configuration_refused(self):
        (self.root / ".config").write_text("different configuration\n")
        with mock.patch.object(check_kernel, "FINGERPRINTS", self.expected):
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                check_kernel.check(self.root)
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(check_kernel.main(["--kernel-tree", str(self.root)]), 1)

    def test_missing_symlink_and_nonregular_identity_files_refused(self):
        path = self.root / ".config"
        path.unlink()
        with mock.patch.object(check_kernel, "FINGERPRINTS", self.expected):
            with self.assertRaises(OSError):
                check_kernel.check(self.root)
            path.symlink_to(self.root / "Module.symvers")
            with self.assertRaises(OSError):
                check_kernel.check(self.root)
            path.unlink()
            os.mkfifo(path)
            with self.assertRaises(ValueError):
                check_kernel.check(self.root)


class PackageMetadataTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="hevc-make-test-")
        self.root = Path(self.tmp.name)
        includes = self.root / "include"
        includes.mkdir()
        (self.root / "rules.mk").write_text("")
        (includes / "kernel.mk").write_text(
            "BOARD ?= bcm27xx\nSUBTARGET ?= bcm2712\nLINUX_VERSION ?= 6.12.112\n"
            "LINUX_VERMAGIC ?= 294a8151df6d94c4f262106e0654471f\nLINUX_RELEASE ?= 1\n")
        (includes / "package.mk").write_text(
            "define KernelPackage\n"
            "$(eval $(KernelPackage/$(1)))\n"
            "$(info PACKAGE=kmod-$(1))\n"
            "$(info DEPENDS=$(DEPENDS))\n"
            "$(info AUTOLOAD=$(AUTOLOAD))\n"
            "$(info KCONFIG=$(KCONFIG))\n"
            "$(info FILES=$(FILES))\n"
            "endef\n"
            ".PHONY: all\nall: ;\n")

    def tearDown(self):
        self.tmp.cleanup()

    def make(self, *args):
        return subprocess.run(
            ["make", "--no-print-directory", "-f", str(RECIPE),
             "TOPDIR=" + str(self.root), "INCLUDE_DIR=" + str(self.root / "include"),
             "BUILD_DIR=" + str(self.root / "build"), *args, "all"],
            text=True, capture_output=True, timeout=10)

    def test_metadata_exact_target_and_no_autoload(self):
        result = self.make()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("PACKAGE=kmod-airbridge-hevc", result.stdout)
        self.assertIn("+kmod-video-mem2mem +kmod-video-dma-contig", result.stdout)
        self.assertIn("\nAUTOLOAD=\n", result.stdout)
        self.assertIn("\nKCONFIG=\n", result.stdout)
        self.assertIn("/driver/rpi-hevc-dec.ko", result.stdout)
        source = RECIPE.read_text()
        self.assertIn("$(KERNEL_MAKE) KCONFIG_NOSILENTUPDATE=1", source)
        self.assertNotIn("modules_prepare", source)
        self.assertNotIn("olddefconfig", source)

    def test_other_kernel_abi_or_target_refused(self):
        for value in ("BOARD=ath79", "SUBTARGET=bcm2711", "LINUX_VERSION=6.12.113",
                      "LINUX_VERMAGIC=wrong", "LINUX_RELEASE=2"):
            with self.subTest(value=value):
                result = self.make(value)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn("airbridge-hevc requires", result.stderr)

    def test_dump_mode_accepts_metadata_placeholders(self):
        result = self.make("DUMP=1", "LINUX_VERSION=<LINUX_VERSION>",
                           "LINUX_VERMAGIC=<LINUX_VERMAGIC>", "BOARD=<BOARD>")
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
