"""IDE source groups of GoogleTest executables at the CMake minimum.

Groups are read from the CMake file API, so the layout that Visual Studio
filters show is checked with any generator.
"""

import json
import os
import unittest
import xml.etree.ElementTree as ET

from test_build_contract import ENGINE
from test_build_helpers import HelperFixture


TEST_DIR = '''
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
'''

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


class GTestHelperTests(HelperFixture):
    def gtest_project(self):
        for path in ("main.cpp", "Import/Support.h", "Import/Emitters/Asset_test.cpp",
                     "Import/Emitters/Platform_test.cpp", "Import/Emitters/Other_test.cpp",
                     "Import/Module_test.cpp", "Import/Platform_test.cpp"):
            self.write(f"Module/Test/{path}")
        self.write("Module/Internal/Shared.cpp")
        self.write("Module/Test/CMakeLists.txt", TEST_DIR)
        self.project(
            "add_library(GTest::gtest INTERFACE IMPORTED)\n"
            "add_library(oxygen::testing INTERFACE IMPORTED)\n"
            "add_library(module INTERFACE)\n"
            f'include("{ENGINE.as_posix()}/cmake/GTestHelpers.cmake")\n'
            "add_subdirectory(Module/Test)\n", language="CXX")

    def source_groups(self):
        query = self.build / ".cmake/api/v1/query/codemodel-v2"
        query.parent.mkdir(parents=True, exist_ok=True)
        query.touch()
        result = self.configure()
        reply = self.build / ".cmake/api/v1/reply"
        index = json.loads(next(reply.glob("index-*.json")).read_text(encoding="utf-8"))
        codemodel = next(o for o in index["objects"] if o["kind"] == "codemodel")
        codemodel = json.loads((reply / codemodel["jsonFile"]).read_text(encoding="utf-8"))
        groups = {}
        for entry in codemodel["configurations"][0]["targets"]:
            target = json.loads((reply / entry["jsonFile"]).read_text(encoding="utf-8"))
            names = [g["name"] for g in target.get("sourceGroups", [])]
            groups[target["name"]] = {
                s["path"]: names[s["sourceGroupIndex"]]
                for s in target.get("sources", []) if "sourceGroupIndex" in s
            }
        return result, groups

    def test_sources_are_grouped_by_their_directory(self):
        self.gtest_project()
        result, groups = self.source_groups()
        self.assertNotIn("Warning", result.stderr)
        for target, expected in EXPECTED_GROUPS.items():
            with self.subTest(target=target):
                self.assertEqual(groups[target], expected)

    @unittest.skipUnless(os.name == "nt", "Visual Studio project generation")
    def test_visual_studio_filters(self):
        self.gtest_project()
        self.configure(generator="Visual Studio 18 2026")
        filters = ET.parse(next(self.build.rglob("Area.Functional.Tests.vcxproj.filters")))
        names = {e.attrib["Include"] for e in filters.iter() if e.tag.endswith("Filter")
                 and "Include" in e.attrib}
        self.assertEqual(names, {"src", "src\\Import", "src\\Import\\Emitters", "external"})
        objects = [e for e in filters.iter() if e.tag.endswith("Object")]
        self.assertEqual(objects, [])


if __name__ == "__main__":
    unittest.main()
