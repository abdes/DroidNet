"""Validate showcase source inventory and the actual installed-input staging."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

from test_build_contract import ENGINE

CONTENT = ENGINE / "Examples/Content"


class ShowcaseTests(unittest.TestCase):
    def test_pinned_source_asset_bytes(self):
        inventory = json.loads((CONTENT / "showcase-assets.json").read_text())
        for asset in inventory["assets"]:
            if asset["name"].endswith(".zip"):
                continue  # Download archives are not shipped; selected inputs are.
            with self.subTest(asset=asset["name"]):
                paths = list(CONTENT.rglob(asset["name"]))
                self.assertEqual(len(paths), 1)
                data = paths[0].read_bytes()
                self.assertEqual(len(data), asset["bytes"])
                self.assertEqual(hashlib.sha256(data).hexdigest(), asset["sha256"])

    def test_staging_resolves_schemas_and_excludes_benchmark_jobs(self):
        spec = importlib.util.spec_from_file_location("showcase", CONTENT / "prepare_showcase.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        scenes = ["sdk-lantern", "sdk-furniture", "sdk-materials", "city-environment-validation",
                  "emissive", "physics_domains", "multi-script", "point-shadow-validation",
                  "spot-shadow-validation"]
        with tempfile.TemporaryDirectory(prefix="oxygen showcase ") as temporary:
            root = Path(temporary)
            output = root / "Content"
            module.prepare(CONTENT.resolve(), output, scenes)
            schema_sources = list((ENGINE / "src/Oxygen/Cooker").rglob("*.schema.json"))
            schema_sources.append(CONTENT / "showcase-assets.schema.json")
            (root / "schemas").mkdir()
            for schema in schema_sources:
                (root / "schemas" / schema.name).write_bytes(schema.read_bytes())
            for path in output.rglob("*.json"):
                data = json.loads(path.read_text())
                if "$schema" in data:
                    self.assertTrue((path.parent / data["$schema"]).is_file(), str(path))
            manifests = list(output.glob("scenes/*/import-manifest.json"))
            self.assertEqual(len(manifests), len(scenes))
            self.assertFalse(any("benchmark" in job.get("id", "")
                                 for p in manifests for job in json.loads(p.read_text())["jobs"]))
            before = {p: p.stat().st_mtime_ns for p in output.rglob("*") if p.is_file()}
            module.prepare(CONTENT.resolve(), output, scenes)
            self.assertEqual(before, {p: p.stat().st_mtime_ns for p in before})


if __name__ == "__main__":
    unittest.main()
