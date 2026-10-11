# pyright: strict
"""Embedded schema generation keeps one build project per output header."""

import unittest
from dataclasses import dataclass

from cmake_testing import (
    NINJA,
    VISUAL_STUDIO,
    CommandTestCase,
    cmake_project,
    include,
    mtimes,
    read,
    requires_cmake,
    windows_only,
    write,
)


@dataclass(frozen=True)
class SchemaInputs:
    """What the fixture passes to oxygen_embed_json_schemas()."""

    expanded: bool = False
    namespace: str = "schemas"
    chunk: int = 8192
    normalized: bool = False


INITIAL = SchemaInputs()
EXPANDED = SchemaInputs(expanded=True)
UPDATED = SchemaInputs(expanded=True, namespace="updated", chunk=64, normalized=True)


@requires_cmake
class JsonSchemaTests(CommandTestCase):
    temp_prefix = "oxygen schema spaces "

    def setUp(self) -> None:
        super().setUp()
        self.binary = self.root / "build"
        self.header = self.binary / "generated/schema.h"
        for name in ("first", "second"):
            write(self.root / f"{name}.json", '{"type":"object"}\n')

    def configure_schemas(self, generator: str, inputs: SchemaInputs) -> str:
        """Configure and return the name of the generation target."""
        output = "${CMAKE_BINARY_DIR}/generated/" + ("schema.h" if inputs.normalized else "unused/../schema.h")
        second = 'kSecond "${CMAKE_CURRENT_SOURCE_DIR}/second.json"' if inputs.expanded else ""
        write(
            self.root / "CMakeLists.txt",
            cmake_project(
                "Schemas",
                "NONE",
                include("JsonSchemaHelpers.cmake")
                + f"""
add_custom_target(schema_consumer)
oxygen_embed_json_schemas(
  TARGET schema_consumer
  OUTPUT_HEADER "{output}"
  NAMESPACE "{inputs.namespace}"
  CHUNK_SIZE {inputs.chunk}
  SCHEMAS kFirst "${{CMAKE_CURRENT_SOURCE_DIR}}/first.json" {second}
)
get_target_property(generation schema_consumer OXYGEN_JSON_SCHEMA_TARGETS)
file(WRITE "${{CMAKE_BINARY_DIR}}/target.txt" "${{generation}}")
""",
            ),
        )
        self.configure(self.root, self.binary, generator)
        return read(self.binary / "target.txt")

    def exercise_generation(self, generator: str) -> None:
        target = self.configure_schemas(generator, INITIAL)
        manifest = next(self.binary.glob("oxygen_json_schemas_*.cmake"))
        stamp = manifest.with_suffix(".stamp")
        self.assertNotIn("kSecond", read(self.header))
        self.assertEqual(self.configure_schemas(generator, EXPANDED), target)
        self.assertIn("kSecond", read(manifest))
        self.assertIn("kSecond", read(self.header))
        self.assertEqual(self.configure_schemas(generator, UPDATED), target)

        def rebuild() -> None:
            self.build(self.binary, "--target", target)

        # Build the ORIGINAL target name after changing its schema inputs.
        rebuild()
        self.header.unlink()
        rebuild()
        self.assertIn("kSecond", read(self.header))
        self.assertIn("namespace updated", read(self.header))
        before = mtimes((self.header, manifest, stamp))
        self.configure_schemas(generator, UPDATED)
        rebuild()
        self.assertEqual(mtimes(before), before)

    def test_ninja_regeneration_keeps_output_owner(self) -> None:
        self.exercise_generation(NINJA)

    @windows_only
    def test_visual_studio_regeneration_keeps_output_owner(self) -> None:
        self.exercise_generation(VISUAL_STUDIO)


if __name__ == "__main__":
    unittest.main()
