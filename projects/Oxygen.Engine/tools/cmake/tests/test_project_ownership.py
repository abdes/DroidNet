# pyright: strict
"""Root ownership checks, plus real embedded module builds with provisioned tools."""

import itertools
import os
import unittest
from pathlib import Path

from cmake_testing import (
    CMAKE,
    CONAN,
    ENGINE,
    EXE_SUFFIX,
    NINJA,
    WINDOWS_PROFILE,
    CommandTestCase,
    Result,
    cmake_project,
    include,
    read,
    requires_cmake,
    requires_env,
    write,
)

PROJECT_OPTIONS = include("ProjectOptions.cmake")
MODULE_SELECTION = include("ModuleSelection.cmake")
CONFLICT = "conflicts with the Conan configuration"


@requires_cmake
class OwnershipTests(CommandTestCase):
    def setUp(self) -> None:
        super().setUp()
        self.projects = itertools.count()

    def configure_policy(self, body: str, *, success: bool = True) -> Result:
        """Configure a fresh full-engine parent project running ``body``."""
        root = self.root / f"policy-{next(self.projects)}"
        write(root / "CMakeLists.txt", cmake_project("Policy", "NONE", "set(OXYGEN_BUILD_FULL_ENGINE TRUE)\n" + body))
        return self.configure(root, root / "build", NINJA, success=success)

    def assert_rejected(self, body: str, message: str) -> None:
        self.assertIn(message, self.configure_policy(body, success=False).stderr)

    def test_parent_normal_variable_beats_stale_cache(self) -> None:
        self.configure_policy(
            'set(OXYGEN_BUILD_TESTS ON CACHE BOOL "stale")\nset(OXYGEN_BUILD_TESTS OFF)\n'
            + PROJECT_OPTIONS
            + 'if(OXYGEN_BUILD_TESTS)\nmessage(FATAL_ERROR "Parent ignored")\nendif()\n'
        )

    def test_embedded_caching_is_owned_by_parent(self) -> None:
        self.assert_rejected(
            "set(PROJECT_IS_TOP_LEVEL FALSE)\nset(OXYGEN_USE_CCACHE ON)\n" + PROJECT_OPTIONS,
            "CMAKE_CXX_COMPILER_LAUNCHER",
        )

    def test_ui_instrumentation_matches_dependency_abi(self) -> None:
        def ui_tests(requested: str, available: str, extra: str = "") -> str:
            return (
                f"set(OXYGEN_BUILD_UI_TESTS {requested})\n"
                f"set(OXYGEN_IMGUI_TEST_ENGINE_AVAILABLE {available})\n{extra}{PROJECT_OPTIONS}"
            )

        self.configure_policy(ui_tests("ON", "ON"))
        for requested, available in (("ON", "OFF"), ("OFF", "ON")):
            self.assert_rejected(ui_tests(requested, available), "UI test option differs from ImGui")
        self.assert_rejected(
            "set(OXYGEN_BUILD_UI_TESTS OFF)\nset(OXYGEN_CONAN_EXPECT_OXYGEN_BUILD_UI_TESTS ON)\n" + PROJECT_OPTIONS,
            CONFLICT,
        )
        for prerequisite in ("OXYGEN_BUILD_FULL_ENGINE", "OXYGEN_BUILD_EXAMPLES"):
            self.assert_rejected(ui_tests("ON", "ON", f"set({prerequisite} OFF)\n"), "requires the full engine")

    def test_conan_allows_only_local_narrowing(self) -> None:
        for option in (
            "OXYGEN_BUILD_TESTS",
            "OXYGEN_BUILD_EXAMPLES",
            "OXYGEN_BUILD_DOCS",
            "OXYGEN_BUILD_TOOLS",
            "OXYGEN_BUILD_BENCHMARKS",
        ):
            with self.subTest(option=option):
                self.configure_policy(f"set(OXYGEN_CONAN_EXPECT_{option} ON)\nset({option} OFF)\n" + PROJECT_OPTIONS)
                self.assert_rejected(
                    f"set(OXYGEN_CONAN_EXPECT_{option} OFF)\nset({option} ON)\n" + PROJECT_OPTIONS, CONFLICT
                )

    def test_binary_settings_and_package_outputs_are_locked(self) -> None:
        for setting in ("BUILD_SHARED_LIBS", "OXYGEN_WITH_TRACY", "OXYGEN_WITH_ASAN", "OXYGEN_BUILD_TESTS"):
            with self.subTest(setting=setting):
                self.assert_rejected(
                    "set(OXYGEN_CONAN_PACKAGE_BUILD ON)\n"
                    f"set(OXYGEN_CONAN_EXPECT_{setting} ON)\nset({setting} OFF)\n" + PROJECT_OPTIONS,
                    CONFLICT,
                )

    def test_selection_closes_dependencies_and_rejects_unknowns(self) -> None:
        self.configure_policy(
            "set(OXYGEN_MODULES Clap)\n"
            + MODULE_SELECTION
            + 'if(NOT OXYGEN_ENABLED_MODULES STREQUAL "Base;TextWrap;Clap")\n'
            'message(FATAL_ERROR "Wrong module closure: ${OXYGEN_ENABLED_MODULES}")\nendif()\n'
        )
        self.configure_policy(
            "set(OXYGEN_MODULES Input)\n"
            + MODULE_SELECTION
            + 'if(NOT OXYGEN_ENABLED_MODULES STREQUAL '
            '"Base;Composition;OxCo;Config;Console;Core;Platform;Input")\n'
            'message(FATAL_ERROR "Wrong module closure: ${OXYGEN_ENABLED_MODULES}")\nendif()\n'
        )
        self.configure_policy(
            "set(OXYGEN_MODULES Platform)\n"
            + MODULE_SELECTION
            + 'if(NOT OXYGEN_ENABLED_MODULES STREQUAL "Base;Composition;OxCo;Config;Core;Platform")\n'
            'message(FATAL_ERROR "Wrong module closure: ${OXYGEN_ENABLED_MODULES}")\nendif()\n'
        )
        for selection in ("Vortex", "OFF"):
            self.assert_rejected(
                f"set(OXYGEN_MODULES {selection})\n" + MODULE_SELECTION, "Unknown reusable Oxygen module"
            )
        self.assert_rejected(
            "set(PROJECT_IS_TOP_LEVEL FALSE)\n" + MODULE_SELECTION, "requires an explicit OXYGEN_MODULES"
        )


PARENT_MAIN = """#include <Oxygen/Base/Hash.h>
#include <Oxygen/Composition/Component.h>
#include <Oxygen/Composition/ComponentMacros.h>
#include <Oxygen/OxCo/Coroutine.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/TextWrap/TextWrap.h>
struct Parent { virtual ~Parent() = default; };
struct Child : Parent {};
bool check_component();
int main() { Child child; Parent* ptr = &child;
return dynamic_cast<Child*>(ptr) && check_component() ? 0 : 1; }
"""

PARENT_COMPONENT = """#include <Oxygen/Composition/Component.h>
#include <Oxygen/Composition/ComponentMacros.h>
class ConsumerComponent final : public oxygen::Component {
OXYGEN_COMPONENT(ConsumerComponent)
};
bool check_component() { ConsumerComponent component;
return component.GetTypeId() == ConsumerComponent::ClassTypeId(); }
"""

PARENT_PROJECT = f"""list(PREPEND CMAKE_MODULE_PATH "${{CMAKE_CURRENT_SOURCE_DIR}}/cmake")
set_property(GLOBAL PROPERTY USE_FOLDERS OFF)
set(CMAKE_INSTALL_PREFIX "${{CMAKE_BINARY_DIR}}/parent-install" CACHE PATH "" FORCE)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${{CMAKE_BINARY_DIR}}/parent-bin")
set(BUILD_SHARED_LIBS ON CACHE BOOL "stale")
set(BUILD_SHARED_LIBS OFF)
set(OXYGEN_BUILD_TESTS ON CACHE BOOL "stale")
set(OXYGEN_BUILD_TESTS OFF)
set(OXYGEN_MODULES "Clap;Composition;OxCo;Serio")
add_subdirectory("{ENGINE.as_posix()}" oxygen)
get_property(_folders GLOBAL PROPERTY USE_FOLDERS)
if(_folders OR NOT CMAKE_INSTALL_PREFIX STREQUAL "${{CMAKE_BINARY_DIR}}/parent-install")
  message(FATAL_ERROR "Oxygen changed parent policy")
endif()
if(OXYGEN_BUILD_TESTS OR OXYGEN_BUILD_EXAMPLES OR OXYGEN_BUILD_DOCS OR OXYGEN_INSTALL)
  message(FATAL_ERROR "Unexpected embedded auxiliary targets")
endif()
if(TARGET oxygen-vortex OR TARGET oxygen-platform OR TARGET oxygen-testing)
  message(FATAL_ERROR "Unselected engine module present")
endif()
get_target_property(_type oxygen-base TYPE)
if(NOT _type STREQUAL "STATIC_LIBRARY")
  message(FATAL_ERROR "Parent linkage ignored")
endif()
add_executable(parent_probe main.cpp component.cpp)
set_source_files_properties(component.cpp PROPERTIES COMPILE_OPTIONS
  "$<$<CXX_COMPILER_ID:MSVC>:/GR->;$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-rtti>")
target_link_libraries(parent_probe PRIVATE oxygen::base oxygen::composition
  oxygen::oxco oxygen::serio oxygen::clap oxygen::textwrap)
"""


@unittest.skipUnless(CONAN and CMAKE, "Conan and CMake are required")
@requires_env("OXYGEN_RUN_MODULE_INTEGRATION", "Builds the reusable engine modules with Conan dependencies")
class ModuleIntegrationTests(CommandTestCase):
    def test_real_embedded_modules_preserve_parent(self) -> None:
        root = self.root
        write(
            root / "conanfile.txt",
            "[requires]\nfmt/12.1.0\nasio/1.36.0\nmagic_enum/0.9.7\n"
            "[generators]\nCMakeToolchain\nCMakeDeps\n"
            "[options]\nfmt/*:header_only=True\n",
        )
        profile = os.environ.get("OXYGEN_TEST_PROFILE", str(WINDOWS_PROFILE))
        self.run_command(
            [
                CONAN,
                "install",
                root,
                "--output-folder",
                root / "deps",
                "--build=missing",
                "-pr:h",
                profile,
                "-pr:b",
                profile,
                "-s",
                "build_type=Release",
                "-s",
                "compiler.cppstd=23",
                "-c",
                "tools.cmake.cmaketoolchain:generator=Ninja",
            ]
        )
        write(root / "cmake/BuildHelpers.cmake", 'message(FATAL_ERROR "Parent helper intercepted")\n')
        write(root / ".clangd", "# Parent owns this file\n")
        write(root / "main.cpp", PARENT_MAIN)
        write(root / "component.cpp", PARENT_COMPONENT)
        write(root / "CMakeLists.txt", cmake_project("Parent", "CXX", PARENT_PROJECT))
        build = root / "build"
        toolchain = Path(root / "deps/conan_toolchain.cmake").as_posix()
        self.configure(root, build, NINJA, f"-DCMAKE_TOOLCHAIN_FILE={toolchain}", "-DCMAKE_BUILD_TYPE=Release")
        self.run_command([CMAKE, "--build", build, "--parallel", "4"])
        self.run_command([build / "parent-bin" / f"parent_probe{EXE_SUFFIX}"])
        self.assertEqual(read(root / ".clangd"), "# Parent owns this file\n")


if __name__ == "__main__":
    unittest.main()
