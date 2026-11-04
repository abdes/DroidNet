"""Exercise model import records through the actual CLI and native publisher."""

import json
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import uuid
from pathlib import Path

IMPORT_TOOL = Path(sys.argv.pop(1)).resolve()
MANIFEST_VERSION: int = json.loads(
    (
        Path(__file__).resolve().parents[3]
        / "Import"
        / "Schemas"
        / "oxygen.import-manifest.schema.json"
    ).read_text(encoding="utf-8")
)["properties"]["schema_version"]["const"]

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
        # A record lives beside its model: at `<model identity>.import.json`.
        return sorted(content.rglob("*.import.json"))

    def test_recipe_replay_reclamation_identity_and_conflicts(self):
        root = self.root
        source_dir = root / ("source-" + "s" * 100) / ("t" * 100)
        source = write_model(source_dir)
        buffer = source_dir / "positions.bin"
        recipe = root / "recipe.json"
        recipe.write_text(
            json.dumps(
                {
                    "schema_version": MANIFEST_VERSION,
                    "jobs": [
                        {
                            "type": "gltf",
                            "source": "/Content/" + source.relative_to(root).as_posix(),
                            "virtual_path": "/Content/source.gltf",
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
        # The record lives at its model's identity, in the model's mount.
        record = root / "source.gltf.import.json"
        # The models lie beside the content root, in the one source mount.
        mount = ["--mount", f"Content={root}"]
        # Generations go to a folder the tool owns, never one the content
        # root derives.
        output = root / "generations"
        published = [*mount, "--output", output]

        apply_recipe = [
            "gltf",
            "--recipe",
            recipe,
            "--record",
            record,
            "--root",
            content,
            *published,
        ]
        self.run_cli([*apply_recipe[:-2]], succeeds=False)
        self.assertFalse(record.exists())
        self.run_cli(apply_recipe, succeeds=True)
        first = json.loads(record.read_text(encoding="utf-8"))
        self.assertNotIn("\\\\?\\", record.read_text(encoding="utf-8"))
        identity = first["source_identity"]
        self.assertEqual(str(uuid.UUID(identity).version), "7")
        self.assertEqual(first["settings"]["mip_filter"], "kaiser")
        self.assertEqual(
            [node["anchor"] for node in first["published_nodes"]], ["Root"]
        )
        self.assertEqual(first["source_fingerprint"]["size"], source.stat().st_size)
        self.assertEqual(
            [file["path"] for file in first["source_fingerprint"]["files"]],
            ["positions.bin"],
        )
        generations = output / identity
        head = generations / "head.json"
        first_head = json.loads(head.read_text(encoding="utf-8"))
        self.assertFalse((content / ".cooked").exists())

        self.run_cli(["gltf", "--record", record, *published], succeeds=True)
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
        self.run_cli(["reclaim", "--record", record], succeeds=False)
        self.assertTrue(previous.is_dir())
        reclaim = ["reclaim", "--record", record, "--output", output]
        self.run_cli(reclaim, succeeds=True)
        self.assertFalse(previous.exists())
        self.assertTrue(selected.is_dir())
        self.assertEqual(record.read_bytes(), original_record)
        self.assertEqual(buffer.read_bytes(), POSITIONS)
        self.run_cli(reclaim, succeeds=True)
        self.assertTrue(selected.is_dir())
        self.assertEqual(record.read_bytes(), original_record)
        self.run_cli(
            [*reclaim[:2], root / "missing.import.json", *reclaim[3:]],
            succeeds=False,
        )
        self.assertTrue(selected.is_dir())
        self.assertEqual(record.read_bytes(), original_record)

        buffer.write_bytes(POSITIONS[:-1])
        self.run_cli(["gltf", "--record", record, *published], succeeds=False)
        self.assertEqual(record.read_bytes(), original_record)
        buffer.write_bytes(POSITIONS)

        conflicts = [
            ["gltf", "--record", record, *published, "--normals", "generate"],
            [*apply_recipe, "--name", "override"],
            [*apply_recipe, "--unit-scale", "2"],
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
                    "--root",
                    content,
                    "--mount",
                    f"Content={content}",
                    "--virtual-path",
                    virtual_path,
                    "--report",
                    report,
                    "--output",
                    self.root / "generations",
                ],
                succeeds=succeeds,
            )

        import_model(source, "/Content/Models/source.gltf")
        (record,) = self.records(content)
        identity = json.loads(record.read_text(encoding="utf-8"))["source_identity"]
        self.assertEqual(
            record,
            content / "Models" / "source.gltf.import.json",
        )

        # Moved with its virtual path: the virtual path names the model.
        moved_dir = content / "Moved"
        moved_dir.mkdir()
        shutil.move(source, moved_dir / "source.gltf")
        shutil.move(content / "Models" / "positions.bin", moved_dir / "positions.bin")
        moved = moved_dir / "source.gltf"
        import_model(moved, "/Content/Models/source.gltf")
        self.assertIn("import.source_rebound", self.report_codes(report))
        self.assertEqual(self.records(content), [record])

        # Moved again, virtual path changed, identical bytes: found by proof.
        renamed_dir = content / "Renamed"
        shutil.move(moved_dir, renamed_dir)
        import_model(renamed_dir / "source.gltf", "/Content/Renamed/source.gltf")
        self.assertIn("import.source_relocated", self.report_codes(report))
        # The model keeps its identity, its record's location: the import
        # rebinds the record's source; only moving the record renames it.
        self.assertEqual(self.records(content), [record])
        relocated = json.loads(record.read_text(encoding="utf-8"))
        self.assertEqual(relocated["source_identity"], identity)
        self.assertTrue(relocated["source"].endswith("Renamed/source.gltf"))
        # A record never names its model's identity: its location does.
        self.assertNotIn("virtual_path", relocated)

        # A copy under a new virtual path is a new model, with a hint.
        copy_dir = content / "Copy"
        shutil.copytree(
            renamed_dir, copy_dir, ignore=shutil.ignore_patterns("*.import.json")
        )
        import_model(copy_dir / "source.gltf", "/Content/Copy/source.gltf")
        self.assertIn("import.source_copy", self.report_codes(report))
        self.assertEqual(len(self.records(content)), 2)

        # A recipe without --record finds, or makes, the record of its job.
        write_model(content / "Recipe")
        recipe = self.root / "recipe.json"
        recipe.write_text(
            json.dumps(
                {
                    "schema_version": MANIFEST_VERSION,
                    "jobs": [
                        {
                            "type": "gltf",
                            "source": "/Content/Recipe/source.gltf",
                            "virtual_path": "/Content/Recipe/source.gltf",
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )
        self.run_cli(
            [
                "gltf",
                "--recipe",
                recipe,
                "--root",
                content,
                "--mount",
                f"Content={content}",
                "--output",
                self.root / "generations",
            ],
            succeeds=True,
        )
        self.assertEqual(len(self.records(content)), 3)

        # A replay whose source is gone says so.
        (renamed_dir / "source.gltf").unlink()
        (renamed_dir / "positions.bin").unlink()
        result = self.run_cli(
            [
                "gltf",
                "--record",
                record,
                "--mount",
                f"Content={content}",
                "--output",
                self.root / "generations",
            ],
            succeeds=False,
        )
        self.assertIn("import.source_missing", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
