"""Exercise retained import modes through the actual CLI and native publisher."""

import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


IMPORT_TOOL = Path(sys.argv.pop(1)).resolve()


class RetainedImportCliTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="oxygen-retained-cli-")
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

    def test_recipe_replay_reclamation_identity_and_conflicts(self):
        root = self.root
        source_dir = root / ("source-" + "s" * 100) / ("t" * 100)
        source_dir.mkdir(parents=True)
        source = source_dir / "source.gltf"
        buffer = source_dir / "positions.bin"
        positions = struct.pack("<9f", 0, 0, 0, 1, 0, 0, 0, 1, 0)
        buffer.write_bytes(positions)
        source.write_text(
            json.dumps({
                "asset": {"version": "2.0"},
                "buffers": [{"uri": buffer.name, "byteLength": len(positions)}],
                "bufferViews": [{"buffer": 0, "byteLength": len(positions)}],
                "accessors": [{
                    "bufferView": 0, "componentType": 5126, "count": 3,
                    "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0],
                }],
                "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
                "nodes": [{"name": "Root", "mesh": 0}],
                "scenes": [{"nodes": [0]}],
                "scene": 0,
            }),
            encoding="utf-8",
        )
        recipe = root / "recipe.json"
        recipe.write_text(
            json.dumps({
                "version": 1,
                "defaults": {"texture": {
                    "mip_policy": "full",
                    "mip_filter": "kaiser",
                    "output_format": "bc7_srgb",
                    "data_format": "bc7",
                }},
                "jobs": [{
                    "type": "gltf", "source": source.relative_to(root).as_posix(),
                }],
            }),
            encoding="utf-8",
        )
        content = root / ("deep-" + "a" * 100) / ("b" * 100) / "Content"
        record = content / "imports" / "model.import.json"

        apply_recipe = [
            "gltf", "--recipe", recipe, "--record", record,
            "--content-root", content,
        ]
        self.run_cli(apply_recipe, succeeds=True)
        first = json.loads(record.read_text(encoding="utf-8"))
        self.assertNotIn("\\\\?\\", record.read_text(encoding="utf-8"))
        self.run_cli(["gltf", "--record", record], succeeds=True)
        replayed = json.loads(record.read_text(encoding="utf-8"))
        self.assertEqual(
            first["material_slot_provenance"], replayed["material_slot_provenance"])
        self.assertNotEqual(
            first["published_generation"], replayed["published_generation"])
        original_record = record.read_bytes()

        generations = (content / ".cooked" / "imports"
                       / replayed["material_slot_provenance"]["source_identity"])
        previous = generations / first["published_generation"]["id"]
        selected = generations / replayed["published_generation"]["id"]
        self.assertTrue(previous.is_dir())
        self.assertTrue(selected.is_dir())
        self.run_cli(["reclaim", "--record", record], succeeds=True)
        self.assertFalse(previous.exists())
        self.assertTrue(selected.is_dir())
        self.assertEqual(record.read_bytes(), original_record)
        self.assertEqual(buffer.read_bytes(), positions)
        self.run_cli(["reclaim", "--record", record], succeeds=True)
        self.assertTrue(selected.is_dir())
        self.assertEqual(record.read_bytes(), original_record)
        self.run_cli(
            ["reclaim", "--record", root / "missing.import.json"], succeeds=False)
        self.assertTrue(selected.is_dir())
        self.assertEqual(record.read_bytes(), original_record)

        buffer.write_bytes(positions[:-1])
        self.run_cli(["gltf", "--record", record], succeeds=False)
        self.assertEqual(record.read_bytes(), original_record)
        buffer.write_bytes(positions)

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


if __name__ == "__main__":
    unittest.main()
