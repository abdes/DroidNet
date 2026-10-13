# pyright: strict
"""Module metadata, hierarchy and IDE inventory contracts at the CMake minimum."""

import unittest
from collections.abc import Iterable, Sequence
from pathlib import Path

from cmake_testing import (
    ENGINE,
    NATIVE_GENERATORS,
    NINJA,
    VISUAL_STUDIO,
    ProjectTestCase,
    include,
    msbuild_items,
    mtimes,
    read,
    requires_cmake,
    requires_compiler,
    windows_only,
)

INVENTORY_HEADERS = ("relative.h", "absolute.h", "public.h", "space dir/header.h", "generated/header.h")


@requires_cmake
class HelperTests(ProjectTestCase):
    def assert_configure_fails(self, cases: Sequence[tuple[str, str]], prefix: str = "") -> None:
        for body, diagnostic in cases:
            with self.subTest(body=body):
                self.project(prefix + body)
                result = self.configure_project(success=False)
                self.assertIn(diagnostic, result.stderr)

    def test_all_production_module_declarations(self) -> None:
        # Exercise the real declaration preambles, including examples not
        # provisioned by the local Conan graph. No dependency discovery/build.
        marker = 'asap_push_module("${META_MODULE_NAME}")'
        bodies: list[str] = []
        for base in (ENGINE / "src", ENGINE / "Examples"):
            for path in sorted(base.rglob("CMakeLists.txt")):
                source = read(path)
                if "asap_module_declare(" not in source:
                    continue
                preamble, found, _ = source.partition(marker)
                self.assertTrue(found, str(path))
                bodies.append(
                    f"{preamble}{marker}\nadd_library(${{META_MODULE_TARGET}} INTERFACE)\n"
                    'asap_pop_module("${META_MODULE_NAME}")\n'
                )
        self.assertTrue(bodies)
        self.project("\n".join(bodies))
        self.assertNotIn("Warning", self.configure_project().stderr)

    def test_metadata_hierarchy_nested_scope_and_reinclude(self) -> None:
        self.project("""
set(depth sentinel-depth)
set(removed sentinel-removed)
set(OXYGEN_IS_MASTER_PROJECT TRUE)
asap_push_project("Oxygen")
asap_module_declare(MODULE_NAME Oxygen.Parent DESCRIPTION "words with spaces; and a semicolon")
if(NOT META_MODULE_DESCRIPTION STREQUAL "words with spaces; and a semicolon")
  message(FATAL_ERROR "Description was split")
endif()
asap_push_module("${META_MODULE_NAME}")
add_subdirectory(child)
if(NOT META_MODULE_NAME STREQUAL "Oxygen.Parent" OR
   NOT ASAP_LOG_PROJECT_HIERARCHY STREQUAL "[Oxygen] > (Oxygen.Parent)")
  message(FATAL_ERROR "Child overwrote parent metadata/stack")
endif()
asap_pop_module("${META_MODULE_NAME}")
asap_pop_project(Oxygen)
if(NOT ASAP_LOG_PROJECT_HIERARCHY STREQUAL "" OR
   NOT depth STREQUAL "sentinel-depth" OR NOT removed STREQUAL "sentinel-removed")
  message(FATAL_ERROR "State leaked")
endif()
""")
        self.write(
            "child/CMakeLists.txt",
            include("LogHelpers.cmake")
            + include("BuildHelpers.cmake")
            + """
asap_module_declare(MODULE_NAME Oxygen.Parent.Child DESCRIPTION "child")
if(NOT META_MODULE_TARGET STREQUAL "oxygen-parent-child" OR
   NOT META_MODULE_TARGET_ALIAS STREQUAL "oxygen::parent-child")
  message(FATAL_ERROR "Incorrect target names")
endif()
asap_push_module("${META_MODULE_NAME}")
add_library(${META_MODULE_TARGET} INTERFACE)
asap_pop_module("${META_MODULE_NAME}")
""",
        )
        result = self.configure_project()
        self.assertNotIn("Warning", result.stderr)
        self.assertIn("[Oxygen] > (Oxygen.Parent) > (Oxygen.Parent.Child)", result.stdout)

    def test_invalid_declarations_and_collisions(self) -> None:
        self.assert_configure_fails(
            [
                ("asap_module_declare()", "MODULE_NAME"),
                ("asap_module_declare(MODULE_NAME)", "missing values"),
                ('asap_module_declare(MODULE_NAME "")', "MODULE_NAME"),
                ("asap_module_declare(MODULE_NAME Oxygen..Base)", "identifier"),
                ("asap_module_declare(MODULE_NAME Oxygen.Base DESCRIPTION)", "missing values"),
                ("asap_module_declare(MODULE_NAME Oxygen.Base TYPO value)", "invalid arguments"),
                ("asap_module_declare(MODULE_NAME Oxygen.Base MODULE_TARGET_NAME custom)", "invalid arguments"),
                ("asap_module_declare(MODULE_NAME Oxygen.Base WITHOUT_VERSION_H)", "invalid arguments"),
                ("add_library(oxygen-base INTERFACE)\nasap_module_declare(MODULE_NAME Oxygen.Base)", "conflicts"),
            ]
        )

    def test_invalid_hierarchy_operations(self) -> None:
        self.assert_configure_fails(
            [
                ("asap_pop_project(Oxygen)", "empty stack"),
                ("asap_push_project(Oxygen)\nasap_pop_project(Other)", "top of stack"),
                ("asap_push_module(Oxygen.Base)", "declare module"),
                ("asap_push_project(Oxygen extra)", "exactly one name"),
                ('asap_push_project("")', "nonempty"),
                ('asap_push_project("bad;name")', "nonempty"),
                ('asap_push_project("bad(name)")', "nonempty"),
            ]
        )

    def test_invalid_inventory_calls(self) -> None:
        self.assert_configure_fails(
            [
                ("arrange_target_files_for_ide(missing)", "unknown target"),
                ("arrange_target_files_for_ide(subject TYPO)", "invalid arguments"),
                ("arrange_target_files_for_ide(subject EXCLUDE_PATTERNS)", "missing values"),
                ("add_library(alias ALIAS subject)\narrange_target_files_for_ide(alias)", "non-alias"),
                ("add_library(external INTERFACE IMPORTED)\narrange_target_files_for_ide(external)", "locally defined"),
            ],
            prefix="add_library(subject INTERFACE)\n",
        )

    def inventory(self) -> None:
        for name in (
            "relative.h",
            "absolute.h",
            "public.h",
            "interface.h",
            "platform.h",
            "space dir/header.h",
            "Bindings/ignored.cpp",
            "test/ignored.cpp",
            "Tools/ignored.cpp",
            "Benchmarks/ignored.cpp",
            "Examples/ignored.cpp",
        ):
            self.write(name)
        self.project("""
add_library(subject INTERFACE)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/generated/header.h" PROPERTIES GENERATED TRUE)
target_sources(subject PRIVATE relative.h "${CMAKE_CURRENT_SOURCE_DIR}/absolute.h"
  "space dir/header.h" "$<$<BOOL:0>:platform.h>"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/header.h"
  PUBLIC FILE_SET HEADERS BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}" FILES public.h
  INTERFACE FILE_SET api TYPE HEADERS BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}" FILES interface.h)
arrange_target_files_for_ide(subject EXCLUDE_PATTERNS "bindings/")
""")

    def test_inventory_paths_file_sets_generated_and_inactive_sources(self) -> None:
        self.inventory()
        result = self.configure_project()
        self.assertNotIn("NOT part", result.stderr)
        self.assertNotIn("cannot inspect", result.stderr)
        # A newly added file is reported on the next configure, including a
        # directory whose name only ends in 'test' (not an excluded Test dir).
        self.write("Contest/forgotten.cpp")
        result = self.configure_project()
        self.assertIn("Contest/forgotten.cpp", result.stderr)
        self.assertNotIn("Bindings/ignored.cpp", result.stderr)
        self.assertNotIn("platform.h", result.stderr)

    def test_unsupported_expression_is_explicit(self) -> None:
        self.write("one.h")
        self.write("two.h")
        self.project("""
add_library(subject INTERFACE)
target_sources(subject PRIVATE "$<IF:$<BOOL:1>,one.h,two.h>")
arrange_target_files_for_ide(subject)
""")
        result = self.configure_project()
        self.assertIn("cannot inspect expression", result.stderr)
        self.assertIn("Inventory is incomplete", result.stderr)

    def project_headers(self) -> list[str]:
        return [
            item.attrib.get("Include", "").replace("\\", "/")
            for item in msbuild_items(self.binary / "subject.vcxproj", "ClInclude")
        ]

    def assert_headers_listed(self, headers: Iterable[str]) -> None:
        listed = self.project_headers()
        for header in headers:
            self.assertTrue(any(path.endswith(header) for path in listed), header)

    @windows_only
    def test_visual_studio_header_only_project_and_groups(self) -> None:
        self.inventory()
        self.configure_project(VISUAL_STUDIO)
        self.assert_headers_listed(INVENTORY_HEADERS)
        # INTERFACE-only headers are usage requirements; explicitly list them
        # on the native target too when IDE visibility is required (as OxCo does).
        with (self.source / "CMakeLists.txt").open("a", encoding="utf-8") as stream:
            stream.write("target_sources(subject PRIVATE interface.h)\n")
        # Prove visibility does not depend on stale projects/solution entries.
        solutions = [*self.binary.glob("*.sln"), *self.binary.glob("*.slnx")]
        self.assertEqual(len(solutions), 1)
        solution = solutions[0]
        for path in (self.binary / "subject.vcxproj", self.binary / "subject.vcxproj.filters", solution):
            path.unlink()
        self.configure_project(VISUAL_STUDIO)
        headers = (*INVENTORY_HEADERS, "interface.h")
        self.assert_headers_listed(headers)
        self.assertIn("subject.vcxproj", solution.read_text(encoding="utf-8-sig"))
        groups = {
            item.attrib["Include"].replace("\\", "/"): item.findtext("{*}Filter", default="")
            for item in msbuild_items(self.binary / "subject.vcxproj.filters", "ClInclude")
        }
        for header in headers:
            group = next((name for path, name in groups.items() if path.endswith(header)), "")
            expected = "generated" if header.startswith("generated/") else "src"
            self.assertTrue(group.startswith(expected), f"{header}: {group!r}")


@requires_compiler
class HelperBuildTests(ProjectTestCase):
    def test_explicit_build_shortcut_has_one_artifact_and_is_incremental(self) -> None:
        self.write("main.cpp", "int main() { return 0; }\n")
        self.project(
            """
asap_module_declare(MODULE_NAME Oxygen.Tools.Probe DESCRIPTION "probe")
asap_push_module("${META_MODULE_NAME}")
add_executable(${META_MODULE_TARGET} main.cpp)
target_compile_features(${META_MODULE_TARGET} PRIVATE cxx_std_23)
set_target_properties(${META_MODULE_TARGET} PROPERTIES
  OUTPUT_NAME "${META_MODULE_NAME}"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
add_custom_target(${META_MODULE_NAME} DEPENDS ${META_MODULE_TARGET})
arrange_target_files_for_ide(${META_MODULE_TARGET})
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/artifact-$<CONFIG>.txt" CONTENT "$<TARGET_FILE:${META_MODULE_TARGET}>")
asap_pop_module("${META_MODULE_NAME}")
""",
            language="CXX",
        )
        for index, generator in enumerate(NATIVE_GENERATORS):
            with self.subTest(generator=generator):
                self.binary = self.root / f"build-{index}"
                self.configure_project(generator)
                for config in ("Debug",) if generator == NINJA else ("Debug", "Release"):
                    self.check_shortcut(config)

    def check_shortcut(self, config: str) -> None:
        def build(target: str) -> None:
            self.build(self.binary, "--parallel", "1", "--target", target, config=config)

        build("Oxygen.Tools.Probe")
        artifact = Path(read(self.binary / f"artifact-{config}.txt"))
        self.run_command([artifact])
        objects = [path for path in self.binary.rglob("*") if path.suffix in (".obj", ".o")]
        self.assertTrue(objects)
        before = mtimes([artifact, *objects])
        build("oxygen-tools-probe")
        build("Oxygen.Tools.Probe")
        self.assertEqual(before, mtimes(before))


if __name__ == "__main__":
    unittest.main()
