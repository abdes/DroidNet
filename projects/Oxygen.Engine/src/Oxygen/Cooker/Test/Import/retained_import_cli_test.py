"""Exercise retained import modes through the actual CLI and native publisher."""

import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="oxygen-retained-cli-") as temporary:
        root = Path(temporary)
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

        def run(arguments, *, succeeds):
            result = subprocess.run(
                [executable, "--no-tui", *map(str, arguments)],
                capture_output=True,
                text=True,
                timeout=60,
                check=False,
            )
            if (result.returncode == 0) != succeeds:
                raise AssertionError(
                    f"Unexpected exit {result.returncode}: {arguments}\n"
                    f"{result.stdout}\n{result.stderr}"
                )

        apply_recipe = [
            "gltf", "--recipe", recipe, "--record", record,
            "--content-root", content,
        ]
        run(apply_recipe, succeeds=True)
        first = json.loads(record.read_text(encoding="utf-8"))
        assert "\\\\?\\" not in record.read_text(encoding="utf-8")
        run(["gltf", "--record", record], succeeds=True)
        replayed = json.loads(record.read_text(encoding="utf-8"))
        assert first["material_slot_provenance"] == replayed["material_slot_provenance"]
        assert first["published_generation"] != replayed["published_generation"]
        original_record = record.read_bytes()

        generations = content / ".cooked" / "imports" / replayed["material_slot_provenance"]["source_identity"]
        previous = generations / first["published_generation"]["id"]
        selected = generations / replayed["published_generation"]["id"]
        assert previous.is_dir() and selected.is_dir()
        run(["reclaim", "--record", record], succeeds=True)
        assert not previous.exists() and selected.is_dir()
        assert record.read_bytes() == original_record
        assert buffer.read_bytes() == positions
        run(["reclaim", "--record", record], succeeds=True)
        assert selected.is_dir() and record.read_bytes() == original_record
        run(["reclaim", "--record", root / "missing.import.json"], succeeds=False)
        assert selected.is_dir() and record.read_bytes() == original_record

        buffer.write_bytes(positions[:-1])
        run(["gltf", "--record", record], succeeds=False)
        assert record.read_bytes() == original_record
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
            run(arguments, succeeds=False)
            assert record.read_bytes() == original_record

        invalid_recipe = json.loads(recipe.read_text(encoding="utf-8"))
        invalid_recipe["thread_pool_size"] = 2
        recipe.write_text(json.dumps(invalid_recipe), encoding="utf-8")
        run(apply_recipe, succeeds=False)
        assert record.read_bytes() == original_record

    print("Retained CLI recipe, replay, reclamation, identity and conflict checks passed.")


if __name__ == "__main__":
    main()
