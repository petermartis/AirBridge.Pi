#!/usr/bin/env python3
"""Offline tests for honest counter reporting and the validated plane mapping."""
import importlib.util
from pathlib import Path
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


if __name__ == "__main__":
    unittest.main()
