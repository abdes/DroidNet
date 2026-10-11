# pyright: strict
"""IDE source groups of GoogleTest executables at the CMake minimum.

Groups are read from the CMake file API, so the layout that Visual Studio
filters show is checked with any generator.
"""

import unittest

from cmake_testing import (
    VISUAL_STUDIO,
    Json,
    ProjectTestCase,
    Result,
    include,
    msbuild_items,
    read_json,
    requires_cmake,
    windows_only,
)

TEST_DIR = """
gtest_program(
  Area.Functional.Tests
  SOURCES
    "main.cpp"
    "Import/Support.h"
    "Import/Emitters/Asset_test.cpp"
    $<$<BOOL:1>:Import/Emitters/Platform_test.cpp>
    $<$<BOOL:0>:Import/Emitters/Other_test.cpp>
)
# Added after the helper call, as module tests add internal sources.
target_sources(Area.Functional.Tests PRIVATE "../Internal/Shared.cpp")

set(META_MODULE_NAME Module)
set(META_MODULE_TARGET module)
m_gtest_program(
  Import
  SOURCES
    "Import/Module_test.cpp"
    $<$<BOOL:1>:Import/Platform_test.cpp>
)
"""

TEST_SOURCES = (
    "main.cpp",
    "Import/Support.h",
    "Import/Emitters/Asset_test.cpp",
    "Import/Emitters/Platform_test.cpp",
    "Import/Emitters/Other_test.cpp",
    "Import/Module_test.cpp",
    "Import/Platform_test.cpp",
)

EXPECTED_GROUPS = {
    "Area.Functional.Tests": {
        "Module/Test/main.cpp": "src",
        "Module/Test/Import/Support.h": "src\\Import",
        "Module/Test/Import/Emitters/Asset_test.cpp": "src\\Import\\Emitters",
        "Module/Test/Import/Emitters/Platform_test.cpp": "src\\Import\\Emitters",
        "Module/Internal/Shared.cpp": "external",
    },
    "Module.Import.Tests": {
        "Module/Test/Import/Module_test.cpp": "src\\Import",
        "Module/Test/Import/Platform_test.cpp": "src\\Import",
    },
}


@requires_cmake
class GTestHelperTests(ProjectTestCase):
    def setUp(self) -> None:
        super().setUp()
        for path in TEST_SOURCES:
            self.write(f"Module/Test/{path}")
        self.write("Module/Internal/Shared.cpp")
        self.write("Module/Test/CMakeLists.txt", TEST_DIR)
        self.project(
            "add_library(GTest::gtest INTERFACE IMPORTED)\n"
            "add_library(oxygen::testing INTERFACE IMPORTED)\n"
            "add_library(module INTERFACE)\n" + include("GTestHelpers.cmake") + "add_subdirectory(Module/Test)\n",
            language="CXX",
        )

    def source_groups(self) -> tuple[Result, dict[str, dict[str, str]]]:
        """Configure with a file-API query and map each target's sources to groups."""
        query = self.binary / ".cmake/api/v1/query/codemodel-v2"
        query.parent.mkdir(parents=True)
        query.touch()
        result = self.configure_project()
        reply = self.binary / ".cmake/api/v1/reply"

        def load(name: str) -> Json:
            return read_json(reply / name)

        index = load(next(reply.glob("index-*.json")).name)
        codemodel = load(next(o["jsonFile"] for o in index["objects"] if o["kind"] == "codemodel"))
        groups: dict[str, dict[str, str]] = {}
        for entry in codemodel["configurations"][0]["targets"]:
            target = load(entry["jsonFile"])
            names = [group["name"] for group in target.get("sourceGroups", [])]
            groups[target["name"]] = {
                source["path"]: names[source["sourceGroupIndex"]]
                for source in target.get("sources", [])
                if "sourceGroupIndex" in source
            }
        return result, groups

    def test_sources_are_grouped_by_their_directory(self) -> None:
        result, groups = self.source_groups()
        self.assertNotIn("Warning", result.stderr)
        for target, expected in EXPECTED_GROUPS.items():
            with self.subTest(target=target):
                self.assertEqual(groups[target], expected)

    @windows_only
    def test_visual_studio_filters(self) -> None:
        self.configure_project(VISUAL_STUDIO)
        filters = next(self.binary.rglob("Area.Functional.Tests.vcxproj.filters"))
        names = {item.attrib["Include"] for item in msbuild_items(filters, "Filter") if "Include" in item.attrib}
        self.assertEqual(names, {"src", "src\\Import", "src\\Import\\Emitters", "external"})
        self.assertEqual(msbuild_items(filters, "Object"), [])


if __name__ == "__main__":
    unittest.main()
