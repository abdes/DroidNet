# pyright: strict
"""Validate showcase source inventory and the actual installed-input staging."""

import hashlib
import shutil
import unittest

from cmake_testing import ENGINE, CommandTestCase, load_module, mtimes, read_json

CONTENT = ENGINE / "Examples/Content"
SCENES = [
    "sdk-lantern",
    "sdk-furniture",
    "sdk-materials",
    "city-environment-validation",
    "emissive",
    "physics_domains",
    "multi-script",
    "point-shadow-validation",
    "spot-shadow-validation",
]


class ShowcaseTests(CommandTestCase):
    temp_prefix = "oxygen showcase "

    def test_pinned_source_asset_bytes(self) -> None:
        for asset in read_json(CONTENT / "showcase-assets.json")["assets"]:
            if asset["name"].endswith(".zip"):
                continue  # Download archives are not shipped; selected inputs are.
            with self.subTest(asset=asset["name"]):
                paths = list(CONTENT.rglob(asset["name"]))
                self.assertEqual(len(paths), 1)
                data = paths[0].read_bytes()
                self.assertEqual(len(data), asset["bytes"])
                self.assertEqual(hashlib.sha256(data).hexdigest(), asset["sha256"])

    def test_staging_resolves_schemas_and_excludes_benchmark_jobs(self) -> None:
        showcase = load_module("showcase", CONTENT / "internal/prepare_showcase.py")
        output = self.root / "Content"
        showcase.prepare(CONTENT.resolve(), output, SCENES)
        schemas = self.root / "schemas"
        schemas.mkdir()
        for schema in (
            *(ENGINE / "src/Oxygen/Cooker").rglob("*.schema.json"),
            CONTENT / "showcase-assets.schema.json",
            CONTENT / "import-sources.schema.json",
        ):
            shutil.copyfile(schema, schemas / schema.name)
        for path in output.rglob("*.json"):
            match read_json(path):
                case {"$schema": str(reference)} if not reference.startswith(("http:", "https:")):
                    self.assertTrue((path.parent / reference).is_file(), str(path))
                case _:
                    pass
        self.assertTrue((output / "import_models.ps1").is_file())
        self.assertTrue((output / "import_models.cmd").is_file())
        self.assertFalse((output / "internal").exists())
        manifests = list(output.glob("scenes/*/import-manifest.json"))
        self.assertEqual(len(manifests), len(SCENES))
        jobs = [job for manifest in manifests for job in read_json(manifest)["jobs"]]
        self.assertFalse(any("benchmark" in job.get("id", "") for job in jobs))
        before = mtimes(path for path in output.rglob("*") if path.is_file())
        showcase.prepare(CONTENT.resolve(), output, SCENES)
        self.assertEqual(mtimes(before), before)


if __name__ == "__main__":
    unittest.main()
