"""Module metadata, hierarchy and IDE inventory contracts at the CMake minimum."""

import os
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

from test_build_contract import CMAKE, ENGINE, CommandTests


@unittest.skipUnless(CMAKE, "CMake is required")
class HelperFixture(CommandTests):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="oxygen helper spaces ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "source"
        self.source.mkdir()
        self.build = self.root / "build"

    def write(self, path, text="// fixture\n"):
        path = self.source / path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def project(self, body, *, language="NONE"):
        self.write("CMakeLists.txt",
                   f'cmake_minimum_required(VERSION 4.2)\nproject(Helpers {language})\n'
                   f'include("{ENGINE.as_posix()}/cmake/BuildHelpers.cmake")\n'
                   f'include("{ENGINE.as_posix()}/cmake/LogHelpers.cmake")\n' + body)

    def configure(self, *, generator="Ninja", success=True):
        return self.run_command([CMAKE, "-S", str(self.source), "-B", str(self.build),
                                 "-G", generator, "-DCMAKE_BUILD_TYPE=Debug"],
                                self.root, success=success)


class HelperTests(HelperFixture):
    def test_all_production_module_declarations(self):
        # Exercise the real declaration preambles, including examples not
        # provisioned by the local Conan graph. No dependency discovery/build.
        bodies = []
        for base in (ENGINE / "src", ENGINE / "Examples"):
            for path in sorted(base.rglob("CMakeLists.txt")):
                source = path.read_text(encoding="utf-8")
                if "asap_module_declare(" not in source:
                    continue
                preamble = source.split('asap_push_module("${META_MODULE_NAME}")', 1)[0]
                self.assertNotEqual(preamble, source, str(path))
                bodies.append(preamble + '''
asap_push_module("${META_MODULE_NAME}")
add_library(${META_MODULE_TARGET} INTERFACE)
asap_pop_module("${META_MODULE_NAME}")
''')
        self.assertGreater(len(bodies), 0)
        self.project("\n".join(bodies))
        result = self.configure()
        self.assertNotIn("Warning", result.stderr)

    def test_metadata_hierarchy_nested_scope_and_reinclude(self):
        self.project('''
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
''')
        self.write("child/CMakeLists.txt", f'''
include("{ENGINE.as_posix()}/cmake/LogHelpers.cmake")
include("{ENGINE.as_posix()}/cmake/BuildHelpers.cmake")
asap_module_declare(MODULE_NAME Oxygen.Parent.Child DESCRIPTION "child")
if(NOT META_MODULE_TARGET STREQUAL "oxygen-parent-child" OR
   NOT META_MODULE_TARGET_ALIAS STREQUAL "oxygen::parent-child")
  message(FATAL_ERROR "Incorrect target names")
endif()
asap_push_module("${{META_MODULE_NAME}}")
add_library(${{META_MODULE_TARGET}} INTERFACE)
asap_pop_module("${{META_MODULE_NAME}}")
''')
        result = self.configure()
        self.assertNotIn("Warning", result.stderr)
        self.assertIn("[Oxygen] > (Oxygen.Parent) > (Oxygen.Parent.Child)", result.stdout)

    def test_invalid_declarations_and_collisions(self):
        cases = [
            ('asap_module_declare()', 'MODULE_NAME'),
            ('asap_module_declare(MODULE_NAME)', 'missing values'),
            ('asap_module_declare(MODULE_NAME "")', 'MODULE_NAME'),
            ('asap_module_declare(MODULE_NAME Oxygen..Base)', 'identifier'),
            ('asap_module_declare(MODULE_NAME Oxygen.Base DESCRIPTION)', 'missing values'),
            ('asap_module_declare(MODULE_NAME Oxygen.Base TYPO value)', 'invalid arguments'),
            ('asap_module_declare(MODULE_NAME Oxygen.Base MODULE_TARGET_NAME custom)', 'invalid arguments'),
            ('asap_module_declare(MODULE_NAME Oxygen.Base WITHOUT_VERSION_H)', 'invalid arguments'),
            ('add_library(oxygen-base INTERFACE)\nasap_module_declare(MODULE_NAME Oxygen.Base)', 'conflicts'),
        ]
        for body, diagnostic in cases:
            with self.subTest(body=body):
                self.project(body)
                result = self.configure(success=False)
                self.assertIn(diagnostic, result.stderr)

    def test_invalid_hierarchy_operations(self):
        cases = [
            ('asap_pop_project(Oxygen)', 'empty stack'),
            ('asap_push_project(Oxygen)\nasap_pop_project(Other)', 'top of stack'),
            ('asap_push_module(Oxygen.Base)', 'declare module'),
            ('asap_push_project(Oxygen extra)', 'exactly one name'),
            ('asap_push_project("")', 'nonempty'),
            ('asap_push_project("bad;name")', 'nonempty'),
            ('asap_push_project("bad(name)")', 'nonempty'),
        ]
        for body, diagnostic in cases:
            with self.subTest(body=body):
                self.project(body)
                result = self.configure(success=False)
                self.assertIn(diagnostic, result.stderr)

    def inventory(self):
        for name in ("relative.h", "absolute.h", "public.h", "interface.h",
                     "platform.h", "space dir/header.h", "Bindings/ignored.cpp",
                     "Test/ignored.cpp", "Tools/ignored.cpp", "Benchmarks/ignored.cpp",
                     "Examples/ignored.cpp"):
            self.write(name)
        self.project('''
add_library(subject INTERFACE)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/generated/header.h" PROPERTIES GENERATED TRUE)
target_sources(subject PRIVATE relative.h "${CMAKE_CURRENT_SOURCE_DIR}/absolute.h"
  "space dir/header.h" "$<$<BOOL:0>:platform.h>"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/header.h"
  PUBLIC FILE_SET HEADERS BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}" FILES public.h
  INTERFACE FILE_SET api TYPE HEADERS BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}" FILES interface.h)
arrange_target_files_for_ide(subject EXCLUDE_PATTERNS "bindings/")
''')

    def test_inventory_paths_file_sets_generated_and_inactive_sources(self):
        self.inventory()
        result = self.configure()
        self.assertNotIn("NOT part", result.stderr)
        self.assertNotIn("cannot inspect", result.stderr)
        # A newly added file is reported on the next configure, including a
        # directory whose name only ends in 'test' (not an excluded Test dir).
        self.write("Contest/forgotten.cpp")
        result = self.configure()
        self.assertIn("Contest/forgotten.cpp", result.stderr)
        self.assertNotIn("Bindings/ignored.cpp", result.stderr)
        self.assertNotIn("platform.h", result.stderr)

    def test_unsupported_expression_is_explicit(self):
        self.write("one.h")
        self.write("two.h")
        self.project('''
add_library(subject INTERFACE)
target_sources(subject PRIVATE "$<IF:$<BOOL:1>,one.h,two.h>")
arrange_target_files_for_ide(subject)
''')
        result = self.configure()
        self.assertIn("cannot inspect expression", result.stderr)
        self.assertIn("Inventory is incomplete", result.stderr)

    def test_invalid_inventory_calls(self):
        cases = [
            ('arrange_target_files_for_ide(missing)', 'unknown target'),
            ('arrange_target_files_for_ide(subject TYPO)', 'invalid arguments'),
            ('arrange_target_files_for_ide(subject EXCLUDE_PATTERNS)', 'missing values'),
            ('add_library(alias ALIAS subject)\narrange_target_files_for_ide(alias)', 'non-alias'),
            ('add_library(external INTERFACE IMPORTED)\narrange_target_files_for_ide(external)', 'locally defined'),
        ]
        for body, diagnostic in cases:
            with self.subTest(body=body):
                self.project('add_library(subject INTERFACE)\n' + body)
                result = self.configure(success=False)
                self.assertIn(diagnostic, result.stderr)

    @unittest.skipUnless(os.name == "nt", "Visual Studio project generation")
    def test_visual_studio_header_only_project_and_groups(self):
        self.inventory()
        self.configure(generator="Visual Studio 18 2026")
        project = ET.parse(self.build / "subject.vcxproj")
        includes = [e.attrib.get("Include", "").replace("\\", "/")
                    for e in project.iter() if e.tag.endswith("ClInclude")]
        for header in ("relative.h", "absolute.h", "public.h", "space dir/header.h", "generated/header.h"):
            self.assertTrue(any(p.endswith(header) for p in includes), header)
        # INTERFACE-only headers are usage requirements; explicitly list them
        # on the native target too when IDE visibility is required (as OxCo does).
        with (self.source / "CMakeLists.txt").open("a", encoding="utf-8") as stream:
            stream.write('target_sources(subject PRIVATE interface.h)\n')
        # Prove visibility does not depend on stale projects/solution entries.
        solution_files = list(self.build.glob("*.sln")) + list(self.build.glob("*.slnx"))
        self.assertEqual(len(solution_files), 1)
        solution = solution_files[0]
        for path in (self.build / "subject.vcxproj", self.build / "subject.vcxproj.filters", solution):
            path.unlink()
            self.assertFalse(path.exists())
        self.configure(generator="Visual Studio 18 2026")
        project = ET.parse(self.build / "subject.vcxproj")
        includes = [e.attrib.get("Include", "").replace("\\", "/")
                    for e in project.iter() if e.tag.endswith("ClInclude")]
        for header in ("relative.h", "absolute.h", "public.h", "interface.h", "space dir/header.h", "generated/header.h"):
            self.assertTrue(any(p.endswith(header) for p in includes), header)
        self.assertIn("subject.vcxproj", solution.read_text(encoding="utf-8-sig"))
        filters = ET.parse(self.build / "subject.vcxproj.filters")
        filter_items = {e.attrib["Include"].replace("\\", "/"): next(iter(e)).text
                        for e in filters.iter() if e.tag.endswith("ClInclude")}
        for header in ("relative.h", "absolute.h", "public.h", "interface.h", "space dir/header.h", "generated/header.h"):
            group = next((v for k, v in filter_items.items() if k.endswith(header)), None)
            self.assertIsNotNone(group, header)
            self.assertTrue(group.startswith("generated" if header.startswith("generated/") else "src"), group)


@unittest.skipUnless(os.environ.get("OXYGEN_RUN_HELPER_BUILD_TESTS") == "1",
                     "Build tests require explicit opt-in on shared hosts")
class HelperBuildTests(HelperFixture):
    def test_explicit_build_shortcut_has_one_artifact_and_is_incremental(self):
        generators = ["Ninja", "Ninja Multi-Config"]
        if os.name == "nt":
            generators.append("Visual Studio 18 2026")
        for index, generator in enumerate(generators):
            with self.subTest(generator=generator):
                self.build = self.root / f"build-{index}"
                self.write("main.cpp", 'int main() { return 0; }\n')
                self.project('''
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
''', language="CXX")
                self.configure(generator=generator)
                for config in (["Debug"] if generator == "Ninja" else ["Debug", "Release"]):
                    command = [CMAKE, "--build", str(self.build), "--config", config, "--parallel", "1", "--target"]
                    self.run_command(command + ["Oxygen.Tools.Probe"], self.root)
                    artifact = Path((self.build / f"artifact-{config}.txt").read_text())
                    self.run_command([str(artifact)], self.root)
                    def timestamps():
                        objects = [p for p in self.build.rglob("*") if p.suffix in (".obj", ".o")]
                        self.assertTrue(objects)
                        return {p: p.stat().st_mtime_ns for p in [artifact, *objects]}

                    before = timestamps()
                    self.run_command(command + ["oxygen-tools-probe"], self.root)
                    self.run_command(command + ["Oxygen.Tools.Probe"], self.root)
                    self.assertEqual(before, timestamps())


if __name__ == "__main__":
    unittest.main()
