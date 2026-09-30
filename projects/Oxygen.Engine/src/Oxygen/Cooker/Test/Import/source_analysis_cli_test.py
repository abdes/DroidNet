"""Source-analysis command integration and input-protection regressions."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


IMPORT_TOOL = Path(sys.argv.pop(1)).resolve()


class SourceAnalysisCliTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="oxygen-source-analysis-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def manifest(self, jobs):
        path = self.root / "manifest.json"
        path.write_text(json.dumps({"version": 1, "jobs": jobs}), encoding="utf-8")
        return path

    def analyze(self, manifest, report):
        return subprocess.run(
            [str(IMPORT_TOOL), "analyze-sources", "--manifest", str(manifest),
             "--report", str(report)],
            cwd=self.root, capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=20, check=False,
        )

    def test_mixed_batch_needs_no_cooked_destination(self):
        (self.root / "material.json").write_text('{"name":"Stone"}', encoding="utf-8")
        (self.root / "scene.json").write_text(
            '{"version":9,"name":"Empty","nodes":[]}', encoding="utf-8")
        manifest = self.manifest([
            {"type": "material-descriptor", "source": "material.json"},
            {"type": "scene-descriptor", "source": "scene.json"},
        ])
        report = self.root / "analysis.json"
        result = self.analyze(manifest, report)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        data = json.loads(report.read_text(encoding="utf-8"))
        self.assertTrue(data["complete"])
        self.assertEqual(len(data["jobs"]), 2)
        self.assertFalse((self.root / ".cooked").exists())

    def test_unsupported_job_cannot_overwrite_its_primary_source(self):
        source = self.root / "script.luau"
        source.write_text("return 42", encoding="utf-8")
        manifest = self.manifest([{"type": "script", "source": source.name}])
        report = self.root / "failure.json"
        result = self.analyze(manifest, report)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(report.exists(), result.stdout + result.stderr)
        self.assertFalse(json.loads(report.read_text(encoding="utf-8"))["complete"])
        result = self.analyze(manifest, source)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(source.read_text(encoding="utf-8"), "return 42")

    @unittest.skipUnless(os.name == "nt", "Windows case-insensitive path identity")
    def test_report_cannot_create_a_missing_source_through_case_alias(self):
        manifest = self.manifest([{"type": "texture", "source": "Missing.PNG"}])
        report = self.root / "missing.png"
        result = self.analyze(manifest, report)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(report.exists())

    def test_report_cannot_create_a_negatively_probed_cubemap_candidate(self):
        for suffix in ("posx", "negx", "posy", "negy", "posz", "negz"):
            (self.root / f"sky_{suffix}.png").write_bytes(b"not decoded")
        manifest = self.manifest([{"type": "texture", "source": "sky.png", "cubemap": True}])
        ordinary_report = self.root / "cube-analysis.json"
        result = self.analyze(manifest, ordinary_report)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = self.analyze(manifest, self.root / "sky_px.png")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / "sky_px.png").exists())


if __name__ == "__main__":
    unittest.main()
