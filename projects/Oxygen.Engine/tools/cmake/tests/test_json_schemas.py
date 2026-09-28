"""Embedded schema generation keeps one build project per output header."""

import os
from pathlib import Path
import tempfile
import unittest

from test_build_contract import CMAKE, ENGINE, CommandTests


@unittest.skipUnless(CMAKE, "CMake is required")
class JsonSchemaTests(CommandTests):
    def exercise_generation(self, generator):
        with tempfile.TemporaryDirectory(prefix="oxygen schema spaces ") as tmp:
            root = Path(tmp)
            build = root / "build"
            header = build / "generated/schema.h"
            for name in ("first", "second"):
                (root / f"{name}.json").write_text(
                    '{"type":"object"}\n', encoding="utf-8"
                )

            def configure(*, expanded=False, namespace="schemas", chunk=8192,
                          normalized=False):
                output = "${CMAKE_BINARY_DIR}/generated/unused/../schema.h"
                if normalized:
                    output = "${CMAKE_BINARY_DIR}/generated/schema.h"
                second = 'kSecond "${CMAKE_CURRENT_SOURCE_DIR}/second.json"' if expanded else ""
                (root / "CMakeLists.txt").write_text(
                    f'''cmake_minimum_required(VERSION 4.2)
project(Schemas NONE)
include("{ENGINE.as_posix()}/cmake/JsonSchemaHelpers.cmake")
add_custom_target(schema_consumer)
oxygen_embed_json_schemas(
  TARGET schema_consumer
  OUTPUT_HEADER "{output}"
  NAMESPACE "{namespace}"
  CHUNK_SIZE {chunk}
  SCHEMAS kFirst "${{CMAKE_CURRENT_SOURCE_DIR}}/first.json" {second}
)
get_target_property(generation schema_consumer OXYGEN_JSON_SCHEMA_TARGETS)
file(WRITE "${{CMAKE_BINARY_DIR}}/target.txt" "${{generation}}")
''', encoding="utf-8"
                )
                self.run_command(
                    [CMAKE, "-S", str(root), "-B", str(build), "-G", generator], root
                )
                return (build / "target.txt").read_text()

            target = configure()
            manifest = next(build.glob("oxygen_json_schemas_*.cmake"))
            stamp = manifest.with_suffix(".stamp")
            self.assertNotIn("kSecond", header.read_text())
            self.assertEqual(configure(expanded=True), target)
            self.assertIn("kSecond", manifest.read_text())
            self.assertIn("kSecond", header.read_text())
            self.assertEqual(configure(expanded=True, namespace="updated",
                                       chunk=64, normalized=True), target)

            def rebuild():
                self.run_command(
                    [CMAKE, "--build", str(build), "--config", "Debug",
                     "--target", target], root
                )

            # Build the ORIGINAL target name after changing its schema inputs.
            rebuild()
            header.unlink()
            rebuild()
            self.assertIn("kSecond", header.read_text())
            self.assertIn("namespace updated", header.read_text())
            paths = (header, manifest, stamp)
            timestamps = tuple(path.stat().st_mtime_ns for path in paths)
            configure(expanded=True, namespace="updated", chunk=64, normalized=True)
            rebuild()
            self.assertEqual(tuple(path.stat().st_mtime_ns for path in paths), timestamps)

    def test_ninja_regeneration_keeps_output_owner(self):
        self.exercise_generation("Ninja")

    @unittest.skipUnless(os.name == "nt", "Visual Studio requires Windows")
    def test_visual_studio_regeneration_keeps_output_owner(self):
        self.exercise_generation("Visual Studio 18 2026")
