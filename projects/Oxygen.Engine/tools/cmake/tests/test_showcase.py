# pyright: strict
"""Validate the showcase media pins and the staging of the installed showcase."""

import hashlib
import unittest
from typing import Any

from cmake_testing import ENGINE, CommandTestCase, load_module, mtimes, read_json

PROJECT = ENGINE / "Examples/Showcase"
PREPARE = ENGINE / "Examples/RenderScene/Tools/internal/prepare_showcase.py"


class ShowcaseTests(CommandTestCase):
    temp_prefix = "oxygen showcase "

    def test_pinned_source_asset_bytes(self) -> None:
        inventory = read_json(PROJECT / "Content/SourceMedia/showcase-assets.json")
        for asset in inventory["assets"]:
            if "path" not in asset:
                continue  # A download archive is read, never kept.
            with self.subTest(asset=asset["name"]):
                data = (PROJECT / asset["path"].removeprefix("/")).read_bytes()
                self.assertEqual(len(data), asset["bytes"])
                self.assertEqual(hashlib.sha256(data).hexdigest(), asset["sha256"])

    def test_staging_is_the_project_reduced_to_the_showcase_tag(self) -> None:
        showcase = load_module("showcase", PREPARE)
        inputs = showcase.prepare(PROJECT.resolve(), self.root, "showcase")
        project = self.root / "Showcase"
        tools = self.root / "RenderScene/Tools"
        catalog: list[dict[str, Any]] = read_json(PROJECT / "Examples.json")["examples"]
        tagged = sorted(e["name"] for e in catalog if "showcase" in e["tags"])
        staged: list[dict[str, Any]] = read_json(project / "Examples.json")["examples"]
        self.assertEqual(sorted(e["name"] for e in staged), tagged)
        self.assertEqual(
            sorted(p.name for p in (project / "Manifests").iterdir()),
            sorted(f"{name}.import-manifest.json" for name in tagged),
        )
        self.assertTrue((project / "Project.oxy").is_file())
        self.assertTrue((project / "Tools/fetch.ps1").is_file())
        self.assertFalse((project / "Tools/Generators").exists())
        for name in ("cook.ps1", "pak.ps1", "play.ps1", "internal/ExamplesTools.ps1"):
            self.assertTrue((tools / name).is_file(), name)
        self.assertFalse((tools / "internal/prepare_showcase.py").exists())
        # Every job's source and record ships, so each staged example cooks.
        for manifest in (project / "Manifests").iterdir():
            for job in read_json(manifest)["jobs"]:
                if "source" in job:
                    self.assertTrue((project / job["source"].removeprefix("/")).is_file())
                if "record" in job:
                    self.assertTrue((project / job["record"]).is_file())
        self.assertTrue(
            all(path.is_relative_to(PROJECT.resolve()) or path.is_relative_to(PREPARE.parents[1]) for path in inputs)
        )
        before = mtimes(path for path in self.root.rglob("*") if path.is_file())
        showcase.prepare(PROJECT.resolve(), self.root, "showcase")
        self.assertEqual(mtimes(before), before)

    def test_staging_removes_what_an_earlier_staging_left(self) -> None:
        showcase = load_module("showcase", PREPARE)
        project = self.root / "Showcase"
        stale = project / "scenes/old/import-manifest.json"
        stale.parent.mkdir(parents=True)
        stale.write_text("{}", encoding="utf-8")
        # An older lowercase folder for a staged one keeps its spelling on a
        # case-insensitive file system unless it is removed.
        lowercase = project / "manifests"
        lowercase.mkdir()
        (lowercase / "Old.import-manifest.json").write_text("{}", encoding="utf-8")
        old_tool = self.root / "RenderScene/Tools/import.ps1"
        old_tool.parent.mkdir(parents=True)
        old_tool.write_text("", encoding="utf-8")
        cooked = self.root / "RenderScene/.cooked/Content/container.index.bin"
        cooked.parent.mkdir(parents=True)
        cooked.write_bytes(b"")
        showcase.prepare(PROJECT.resolve(), self.root, "showcase")
        self.assertFalse((project / "scenes").exists())
        self.assertIn("Manifests", [entry.name for entry in project.iterdir()])
        self.assertNotIn("manifests", [entry.name for entry in project.iterdir()])
        self.assertFalse(old_tool.exists())
        self.assertTrue(cooked.is_file())

    def test_staging_refuses_an_unknown_tag(self) -> None:
        showcase = load_module("showcase", PREPARE)
        with self.assertRaisesRegex(ValueError, "No example has the tag"):
            showcase.prepare(PROJECT.resolve(), self.root, "no-such-tag")


if __name__ == "__main__":
    unittest.main()
