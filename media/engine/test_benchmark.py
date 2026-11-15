#!/usr/bin/env python3
"""Offline tests for honest counter reporting and the validated plane mapping."""
import importlib.util
from pathlib import Path
import re
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("benchmark", HERE / "benchmark.py")
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)


def sample(t, drops, sync=0):
    return {"state": "playing", "time_pos": t, "width": 3840, "height": 2160,
            "hwdec": "drm", "dropped_frames": 0, "vo_dropped_frames": drops, "avsync": sync}


class BenchmarkTests(unittest.TestCase):
    def test_startup_drops_remain_visible(self):
        result = benchmark.summarize([sample(1, 2), sample(5, 2), sample(25, 2)])
        self.assertEqual(result["vo_dropped_frames_max"], 2)
        self.assertEqual(result["vo_dropped_frames_at_steady_start"], 2)
        self.assertEqual(result["vo_dropped_frames_steady_delta"], 0)

    def test_continuing_drops_are_counted(self):
        result = benchmark.summarize([sample(5, 39), sample(25, 163)])
        self.assertEqual(result["vo_dropped_frames_steady_delta"], 124)

    def test_unknown_sync_is_not_zero(self):
        self.assertIsNone(benchmark.summarize([sample(5, 2, None)])["max_steady_abs_avsync"])

    def test_counter_reset_is_flagged(self):
        result = benchmark.summarize([sample(5, 10), sample(8, 0)])
        self.assertTrue(result["counter_reset_detected"])
        self.assertIsNone(result["vo_dropped_frames_steady_delta"])

    def test_no_playback_is_not_success(self):
        self.assertIn("error", benchmark.summarize([]))

    def test_validated_plane_configuration(self):
        text = (HERE.parents[1] / "openwrt/packages/airbridge-media/files/mpv.conf").read_text()
        for setting in ["gpu-hwdec-interop=drmprime-overlay", "drm-draw-plane=overlay",
                        "drm-drmprime-video-plane=primary", "drm-format=xrgb8888"]:
            self.assertIn(setting, text)
        self.assertNotIn("drm-draw-surface-size=1920x1080", text)
        self.assertNotIn("framedrop=no", text)
        self.assertNotIn("loop-file=inf", text)

    def test_software_path_dark_scene_cleanup(self):
        package = HERE.parents[1] / "openwrt/packages/airbridge-media"
        lines = (package / "files/mpv.conf").read_text().splitlines()
        shader_path = "/etc/airbridge-media/dark-chroma.glsl"
        # profile=fast disables dithering, so the override must come after it.
        self.assertGreater(lines.index("dither=fruit"), lines.index("profile=fast"))
        self.assertIn("glsl-shaders=" + shader_path, lines)
        # Measured on the CM5: any scaler above bilinear drops frames at 4K.
        for line in lines:
            self.assertFalse(line.startswith(("scale=", "cscale=", "dscale=", "deband=")), line)
        shader = (package / "files/dark-chroma.glsl").read_text()
        for directive in ["//!HOOK CHROMA", "//!BIND HOOKED", "//!BIND LUMA"]:
            self.assertIn(directive, shader)
        # Any extra pass on 1080p sources dropped frames on the CM5: SD sources only.
        self.assertEqual(shader.count("//!HOOK"), 1)
        self.assertIn("//!WHEN HOOKED.w 401 <", shader)
        self.assertNotIn("//!HOOK LUMA", shader)
        self.assertNotIn("//!HOOK MAIN", shader)
        self.assertEqual(shader.count("{"), shader.count("}"))
        recipe = (package / "Makefile").read_text()
        self.assertIn("./files/dark-chroma.glsl $(1)" + shader_path, recipe)
        stage = (HERE / "stage.py").read_text()
        self.assertIn('("dark-chroma.glsl", "' + shader_path.lstrip("/") + '")', stage)
        release = re.search(r"(?m)^PKG_RELEASE:=(\d+)$", recipe)
        self.assertGreaterEqual(int(release[1]), 7)
        self.assertIn('"airbridge-media": "1.0.0-r' + release[1] + '"', (HERE / "package.py").read_text())


if __name__ == "__main__":
    unittest.main()
