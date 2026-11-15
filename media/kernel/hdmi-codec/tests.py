#!/usr/bin/env python3
"""Host-only offline HDMI source, closure and package-metadata tests."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from unittest import mock

HERE = Path(__file__).resolve().parent


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


recovery = load("airbridge_hdmi_recovery", HERE / "recover.py")
auditor = load("airbridge_hdmi_audit", HERE / "audit_closure.py")


class HDMITests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="hdmi-codec-test-")
        self.root = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)
        self.network = mock.patch("urllib.request.urlopen",
                                  side_effect=AssertionError("network forbidden in tests"))
        self.network.start()
        self.addCleanup(self.network.stop)

    def cache(self):
        return Path(shutil.copytree(HERE / "cache", self.root / "cache"))

    def test_golden_source_and_licensing(self):
        data = recovery.source(HERE / "cache")
        self.assertEqual(len(data), 32393)
        self.assertTrue(data.startswith(b"// SPDX-License-Identifier: GPL-2.0-only\n"))
        self.assertIn(b"Jyri Sarha", data)
        metadata = json.loads((HERE / "provenance.json").read_text())
        self.assertEqual(metadata["files"], recovery.metadata()["files"])
        self.assertEqual(metadata["linux_revision"], recovery.LINUX_REVISION)
        self.assertEqual(metadata["license_text_sha256"],
                         recovery.digest((HERE / "COPYING").read_bytes()))
        self.assertFalse(metadata["hardware_validated"])
        self.assertFalse(metadata["autoload"])

    def test_closure_is_exact_release_empty_and_complete(self):
        closure = json.loads((HERE / "patch-closure.json").read_text())
        provenance = json.loads((HERE / "provenance.json").read_text())
        self.assertEqual(provenance["patch_closure_evidence"], closure)
        self.assertEqual(sum(closure["scanned_patch_directories"].values()), 1368)
        self.assertEqual(closure["archive_sha256"], auditor.ARCHIVE_SHA256)
        for key in ("source_patch_closure", "source_overlays",
                    "header_patch_references", "header_overlays"):
            self.assertEqual(closure[key], [])

    def test_offline_verify_recover_and_cached_fetch(self):
        output = self.root / "driver"
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(recovery.main(["verify"]), 0)
            self.assertEqual(recovery.main(["recover", "--output", str(output)]), 0)
            self.assertEqual(recovery.main(["fetch"]), 0)
        self.assertEqual({p.name for p in output.iterdir()},
                         {"hdmi-codec.c", "Makefile", ".source-provenance.json"})
        self.assertEqual((output / "hdmi-codec.c").read_bytes(),
                         recovery.source(HERE / "cache"))
        self.assertEqual((output / "Makefile").read_bytes(), recovery.KBUILD)
        self.assertEqual(json.loads((output / ".source-provenance.json").read_text()),
                         recovery.metadata())
        self.assertEqual(output.stat().st_mode & 0o777, 0o755)
        self.assertTrue(all(p.stat().st_mode & 0o777 == 0o644 for p in output.iterdir()))

    def test_tamper_refused_without_overwrite(self):
        cache = self.cache()
        path = cache / "hdmi-codec.c"
        changed = bytearray(path.read_bytes())
        changed[-2] ^= 1
        path.write_bytes(changed)
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            recovery.source(cache)
        with self.assertRaises(ValueError):
            recovery.fetch(cache)
        self.assertEqual(path.read_bytes(), bytes(changed))

    def test_missing_wrong_size_symlink_and_fifo_refused(self):
        cache = self.cache()
        path = cache / "hdmi-codec.c"
        path.write_bytes(b"short")
        with self.assertRaises(ValueError):
            recovery.source(cache)
        path.unlink()
        with self.assertRaises(OSError):
            recovery.source(cache)
        path.symlink_to(HERE / "cache/hdmi-codec.c")
        with self.assertRaises(OSError):
            recovery.source(cache)
        path.unlink()
        os.mkfifo(path)
        with self.assertRaises(ValueError):
            recovery.source(cache)

    def test_existing_output_is_never_replaced(self):
        output = self.root / "existing"
        output.mkdir()
        sentinel = output / "keep"
        sentinel.write_text("user content")
        link = self.root / "link"
        link.symlink_to(output, target_is_directory=True)
        for path in (output, sentinel, link):
            with self.assertRaises(OSError):
                recovery.recover(HERE / "cache", path)
        self.assertEqual(sentinel.read_text(), "user content")
        self.assertTrue(link.is_symlink())

    def test_bad_cache_cli_creates_no_output(self):
        cache = self.cache()
        (cache / "hdmi-codec.c").write_bytes(b"bad")
        output = self.root / "refused"
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(recovery.main(["recover", "--cache", str(cache),
                                           "--output", str(output)]), 1)
        self.assertFalse(output.exists())

    def test_download_is_bounded_and_hash_verified(self):
        cache = self.root / "fetch"
        response = mock.MagicMock()
        response.__enter__.return_value.read.return_value = b"x" * recovery.SOURCE_BYTES
        with mock.patch("urllib.request.urlopen", return_value=response) as url:
            with self.assertRaises(ValueError):
                recovery.fetch(cache)
            url.assert_called_once_with(recovery.SOURCE_URL, timeout=30)
        response.__enter__.return_value.read.assert_called_once_with(recovery.SOURCE_BYTES + 1)
        self.assertFalse((cache / "hdmi-codec.c").exists())

    def test_deterministic_external_kbuild(self):
        a, b = self.root / "a", self.root / "b"
        recovery.recover(HERE / "cache", a)
        recovery.recover(HERE / "cache", b)
        self.assertEqual({p.name: p.read_bytes() for p in a.iterdir()},
                         {p.name: p.read_bytes() for p in b.iterdir()})
        self.assertIn(b"obj-m += snd-soc-hdmi-codec.o", recovery.KBUILD)
        self.assertNotIn(b"CONFIG_SND_SOC_HDMI_CODEC", recovery.KBUILD)

    def archive(self, members):
        buffer = io.BytesIO()
        with tarfile.open(fileobj=buffer, mode="w:gz") as archive:
            for name, data in members.items():
                info = tarfile.TarInfo("openwrt/" + name)
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))
        return buffer.getvalue()

    def test_auditor_refuses_wrong_archive(self):
        with self.assertRaisesRegex(ValueError, "source pin"):
            auditor.audit(b"not exact OpenWrt source")

    def test_auditor_scans_patch_and_overlay_content_without_extraction(self):
        data = self.archive({
            "target/linux/bcm27xx/patches-6.12/a.patch":
                b"+++ b/sound/soc/codecs/hdmi-codec.c\n",
            "target/linux/generic/hack-6.12/b.patch":
                b"+++ b/include/sound/hdmi-codec.h\n",
            "target/linux/bcm27xx/files/sound/soc/codecs/hdmi-codec.c": b"overlay",
            "target/linux/generic/files/include/sound/hdmi-codec.h": b"overlay",
            "unrelated.patch": b"ignored",
        })
        with mock.patch.object(auditor, "ARCHIVE_BYTES", len(data)):
            with mock.patch.object(auditor, "ARCHIVE_SHA256", recovery.digest(data)):
                result = auditor.audit(data)
                self.assertEqual(sum(result["scanned_patch_directories"].values()), 2)
                self.assertEqual(len(result["source_patch_closure"]), 1)
                self.assertEqual(len(result["header_patch_references"]), 1)
                self.assertEqual(len(result["source_overlays"]), 1)
                self.assertEqual(len(result["header_overlays"]), 1)
                path = self.root / "fixture.tar.gz"
                path.write_bytes(data)
                with contextlib.redirect_stdout(io.StringIO()):
                    with contextlib.redirect_stderr(io.StringIO()):
                        self.assertEqual(auditor.main(["--archive", str(path)]), 1)
        self.assertEqual(list(self.root.iterdir()), [self.root / "fixture.tar.gz"])

    def test_recipe_metadata_and_guards(self):
        includes = self.root / "include"
        includes.mkdir()
        (self.root / "rules.mk").write_text("")
        (includes / "kernel.mk").write_text(
            "BOARD ?= bcm27xx\nSUBTARGET ?= bcm2712\nLINUX_VERSION ?= 6.12.112\n"
            "LINUX_VERMAGIC ?= 294a8151df6d94c4f262106e0654471f\nLINUX_RELEASE ?= 1\n")
        (includes / "package.mk").write_text(
            "define KernelPackage\n$(eval $(KernelPackage/$(1)))\n"
            "$(info PACKAGE=kmod-$(1))\n$(info DEPENDS=$(DEPENDS))\n"
            "$(info AUTOLOAD=$(AUTOLOAD))\n$(info KCONFIG=$(KCONFIG))\nendef\nall: ;\n")
        recipe = HERE.parents[2] / "openwrt/packages/kmod-airbridge-hdmi-codec/Makefile"
        base = ["make", "--no-print-directory", "-f", str(recipe),
                "TOPDIR=" + str(self.root), "INCLUDE_DIR=" + str(includes),
                "BUILD_DIR=" + str(self.root / "build")]
        result = subprocess.run(base + ["all"], text=True, capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("PACKAGE=kmod-airbridge-hdmi-codec", result.stdout)
        self.assertIn("+kmod-sound-soc-core", result.stdout)
        self.assertIn("\nAUTOLOAD=\n", result.stdout)
        self.assertIn("\nKCONFIG=\n", result.stdout)
        for wrong in ("BOARD=other", "SUBTARGET=bcm2711", "LINUX_VERSION=6.12.113",
                      "LINUX_VERMAGIC=wrong", "LINUX_RELEASE=2"):
            result = subprocess.run(base + [wrong, "all"], text=True,
                                    capture_output=True, timeout=5)
            self.assertNotEqual(result.returncode, 0, wrong)
            self.assertIn("airbridge-hdmi-codec requires", result.stderr)


if __name__ == "__main__":
    unittest.main()
