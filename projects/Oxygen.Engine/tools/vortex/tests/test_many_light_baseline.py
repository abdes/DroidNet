"""Negative controls for baseline image admission and interval aggregation."""

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np


def load(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).resolve().parents[1] / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


runner = load("RunManyLightBaseline")
summary = load("SummarizeManyLightBaseline")


class BaselineAdmissionTest(unittest.TestCase):
    def image_pair(self, directory, changed=False, complete=True):
        paths = [Path(directory) / name for name in ("reference", "candidate")]
        for index, path in enumerate(paths):
            path.mkdir()
            image = np.array([[[1, 2, 3, 1], [0.25, 0.5, 0.75, 1]]], dtype="<f4")
            if changed and index == 1:
                image[0, 1, 0] = 0
            image.tofile(path / "frame.rgba32f")
            (path / "manifest.json").write_text(json.dumps({"complete": complete, "images": [{
                "file": "frame.rgba32f", "phase": 0, "view": 0, "width": 2, "height": 1, "pre_exposure": 1}]}))
        return paths

    def test_identical_images_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            result = runner.compare_images(*self.image_pair(directory))
            self.assertTrue(result["passed"])
            self.assertEqual(result["images"][0]["max_budget_fraction"], 0)

    def test_missing_contribution_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, "Image qualification failed"):
                runner.compare_images(*self.image_pair(directory, changed=True))

    def test_incomplete_capture_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, "incomplete"):
                runner.compare_images(*self.image_pair(directory, complete=False))

    def test_nested_and_overlapping_intervals_are_counted_once(self):
        self.assertEqual(summary.union_duration([(0, 4), (1, 2), (3, 5), (8, 10)]), 7)

    def test_nonfinite_timing_is_rejected(self):
        with self.assertRaises(ValueError):
            summary.statistics([1, float("nan")])


if __name__ == "__main__":
    unittest.main()
