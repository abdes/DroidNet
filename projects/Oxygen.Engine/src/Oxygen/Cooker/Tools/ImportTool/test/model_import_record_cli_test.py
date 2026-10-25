"""Exercise model import records through the actual CLI and native publisher."""

import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import uuid


IMPORT_TOOL = Path(sys.argv.pop(1)).resolve()

POSITIONS = struct.pack("<9f", 0, 0, 0, 1, 0, 0, 0, 1, 0)


def write_model(directory: Path, name: str = "source.gltf") -> Path:
    """A one-triangle glTF whose positions live in a buffer file beside it."""
    directory.mkdir(parents=True, exist_ok=True)
    buffer = directory / "positions.bin"
    buffer.write_bytes(POSITIONS)
    source = directory / name
    source.write_text(
        json.dumps(
            {
                "asset": {"version": "2.0"},
                "buffers": [{"uri": buffer.name, "byteLength": len(POSITIONS)}],
                "bufferViews": [{"buffer": 0, "byteLength": len(POSITIONS)}],
                "accessors": [
                    {
                        "bufferView": 0,
                        "componentType": 5126,
                        "count": 3,
                        "type": "VEC3",
                        "min": [0, 0, 0],
                        "max": [1, 1, 0],
                    }
                ],
                "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
                "nodes": [{"name": "Root", "mesh": 0}],
                "scenes": [{"nodes": [0]}],
                "scene": 0,
            }
        ),
        encoding="utf-8",
    )
    return source


class ModelImportRecordCliTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="oxygen-record-cli-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def run_cli(self, arguments, *, succeeds):
        result = subprocess.run(
            [str(IMPORT_TOOL), "--no-tui", *map(str, arguments)],
            capture_output=True,
            text=True,
            timeout=60,
            check=False,
        )
        self.assertEqual(
            result.returncode == 0,
            succeeds,
            f"Unexpected exit {result.returncode}: {arguments}\n"
            f"{result.stdout}\n{result.stderr}",
        )
        return result

    def report_codes(self, report: Path) -> list[str]:
        document = json.loads(report.read_text(encoding="utf-8"))
        return [
            diagnostic["code"]
            for job in document["jobs"]
            for diagnostic in job["diagnostics"]
        ]

    def records(self, content: Path) -> list[Path]:
        return sorted((content / "imports").glob("*.import.json"))

    def test_recipe_replay_reclamation_identity_and_conflicts(self):
        root = self.root
        source_dir = root / ("source-" + "s" * 100) / ("t" * 100)
        source = write_model(source_dir)
        buffer = source_dir / "positions.bin"
        recipe = root / "recipe.json"
        recipe.write_text(
            json.dumps(
                {
                    "version": 1,
                    "jobs": [
                        {
                            "type": "gltf",
                            "source": source.relative_to(root).as_posix(),
                            "virtual_path": "/.cooked/source.gltf",
                            "mip_policy": "full",
                            "mip_filter": "kaiser",
                            "output_format": "bc7_srgb",
                            "data_format": "bc7",
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )
        content = root / ("deep-" + "a" * 100) / ("b" * 100) / "Content"
        identity = str(uuid.uuid7())
        record = content / "imports" / f"{identity}.import.json"

        apply_recipe = [
            "gltf",
            "--recipe",
            recipe,
            "--record",
            record,
            "--content-root",
            content,
        ]
        self.run_cli(apply_recipe, succeeds=True)
        first = json.loads(record.read_text(encoding="utf-8"))
        self.assertNotIn("\\\\?\\", record.read_text(encoding="utf-8"))
        self.assertEqual(first["source_identity"], identity)
        self.assertEqual(first["settings"]["mip_filter"], "kaiser")
        self.assertEqual(
            [node["anchor"] for node in first["published_nodes"]], ["Root"]
        )
        self.assertEqual(first["source_fingerprint"]["size"], source.stat().st_size)
        self.assertEqual(
            [file["path"] for file in first["source_fingerprint"]["files"]],
            ["positions.bin"],
        )
        generations = content / ".cooked" / "imports" / identity
        head = generations / "head.json"
        first_head = json.loads(head.read_text(encoding="utf-8"))

        self.run_cli(["gltf", "--record", record], succeeds=True)
        replayed = json.loads(record.read_text(encoding="utf-8"))
        replayed_head = json.loads(head.read_text(encoding="utf-8"))
        self.assertEqual(first["surface_provenance"], replayed["surface_provenance"])
        self.assertEqual(first["published_nodes"], replayed["published_nodes"])
        self.assertNotEqual(first_head["id"], replayed_head["id"])
        original_record = record.read_bytes()

        previous = generations / first_head["id"]
        selected = generations / replayed_head["id"]
        self.assertTrue(previous.is_dir())
        self.assertTrue(selected.is_dir())
        self.run_cli(["reclaim", "--record", record], succeeds=True)
        self.assertFalse(previous.exists())
        self.assertTrue(selected.is_dir())
        self.assertEqual(record.read_bytes(), original_record)
        self.assertEqual(buffer.read_bytes(), POSITIONS)
        self.run_cli(["reclaim", "--record", record], succeeds=True)
        self.assertTrue(selected.is_dir())
        self.assertEqual(record.read_bytes(), original_record)
        self.run_cli(
            ["reclaim", "--record", root / "missing.import.json"], succeeds=False
        )
        self.assertTrue(selected.is_dir())
        self.assertEqual(record.read_bytes(), original_record)

        buffer.write_bytes(POSITIONS[:-1])
        self.run_cli(["gltf", "--record", record], succeeds=False)
        self.assertEqual(record.read_bytes(), original_record)
        buffer.write_bytes(POSITIONS)

        conflicts = [
            ["gltf", "--record", record, "--normals", "generate"],
            ["gltf", "--record", record, "--content-root", content],
            [*apply_recipe, "--name", "override"],
            [*apply_recipe, "--content-hashing", "false"],
            ["fbx", *apply_recipe[1:]],
            ["gltf", source, *apply_recipe[1:]],
        ]
        for arguments in conflicts:
            with self.subTest(arguments=[str(a) for a in arguments]):
                self.run_cli(arguments, succeeds=False)
                self.assertEqual(record.read_bytes(), original_record)

        invalid_recipe = json.loads(recipe.read_text(encoding="utf-8"))
        invalid_recipe["thread_pool_size"] = 2
        recipe.write_text(json.dumps(invalid_recipe), encoding="utf-8")
        self.run_cli(apply_recipe, succeeds=False)
        self.assertEqual(record.read_bytes(), original_record)

    def test_lookup_keeps_identity_across_moves_and_copies(self):
        content = self.root / "Content"
        source = write_model(content / "Models")
        report = self.root / "report.json"

        def import_model(model: Path, virtual_path: str, *, succeeds=True):
            return self.run_cli(
                [
                    "gltf",
                    model,
                    "--content-root",
                    content,
                    "--virtual-path",
                    virtual_path,
                    "--report",
                    report,
                ],
                succeeds=succeeds,
            )

        import_model(source, "/.cooked/Models/source.gltf")
        (record,) = self.records(content)
        identity = json.loads(record.read_text(encoding="utf-8"))["source_identity"]
        self.assertEqual(record.name, f"{identity}.import.json")

        # Moved with its virtual path: the virtual path names the model.
        moved_dir = content / "Moved"
        moved_dir.mkdir()
        shutil.move(source, moved_dir / "source.gltf")
        shutil.move(content / "Models" / "positions.bin", moved_dir / "positions.bin")
        moved = moved_dir / "source.gltf"
        import_model(moved, "/.cooked/Models/source.gltf")
        self.assertIn("import.source_rebound", self.report_codes(report))
        self.assertEqual(self.records(content), [record])

        # Moved again, virtual path changed, identical bytes: found by proof.
        renamed_dir = content / "Renamed"
        shutil.move(moved_dir, renamed_dir)
        import_model(renamed_dir / "source.gltf", "/.cooked/Renamed/source.gltf")
        self.assertIn("import.source_relocated", self.report_codes(report))
        relocated = json.loads(record.read_text(encoding="utf-8"))
        self.assertEqual(relocated["source_identity"], identity)
        self.assertEqual(relocated["virtual_path"], "/.cooked/Renamed/source.gltf")

        # A copy under a new virtual path is a new model, with a hint.
        copy_dir = content / "Copy"
        shutil.copytree(renamed_dir, copy_dir)
        import_model(copy_dir / "source.gltf", "/.cooked/Copy/source.gltf")
        self.assertIn("import.source_copy", self.report_codes(report))
        self.assertEqual(len(self.records(content)), 2)

        # A recipe without --record finds, or makes, the record of its job.
        recipe_model = write_model(content / "Recipe")
        recipe = self.root / "recipe.json"
        recipe.write_text(
            json.dumps(
                {
                    "version": 1,
                    "jobs": [
                        {
                            "type": "gltf",
                            "source": recipe_model.as_posix(),
                            "virtual_path": "/.cooked/Recipe/source.gltf",
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )
        self.run_cli(
            ["gltf", "--recipe", recipe, "--content-root", content], succeeds=True
        )
        self.assertEqual(len(self.records(content)), 3)

        # A replay whose source is gone says so.
        shutil.rmtree(renamed_dir)
        result = self.run_cli(["gltf", "--record", record], succeeds=False)
        self.assertIn("import.source_missing", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
