"""Root ownership checks, plus opt-in real module builds with provisioned tools."""

import os
from pathlib import Path
import tempfile
import unittest

from test_build_contract import CMAKE, CONAN, ENGINE, CommandTests


@unittest.skipUnless(CMAKE, "CMake is required")
class OwnershipTests(CommandTests):
    def configure_policy(self, body, *, success=True):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = (
                "cmake_minimum_required(VERSION 4.2)\nproject(Policy NONE)\n"
                'set(OXYGEN_BUILD_FULL_ENGINE TRUE)\n' + body
            )
            (root / "CMakeLists.txt").write_text(source, encoding="utf-8")
            return self.run_command(
                [CMAKE, "-S", str(root), "-B", str(root / "build"), "-G", "Ninja"],
                root, success=success,
            )

    def options(self):
        return f'include("{ENGINE.as_posix()}/cmake/ProjectOptions.cmake")\n'

    def test_parent_normal_variable_beats_stale_cache(self):
        self.configure_policy(
            'set(OXYGEN_BUILD_TESTS ON CACHE BOOL "stale")\n'
            'set(OXYGEN_BUILD_TESTS OFF)\n' + self.options()
            + 'if(OXYGEN_BUILD_TESTS)\nmessage(FATAL_ERROR "Parent ignored")\nendif()\n'
        )

    def test_embedded_caching_is_owned_by_parent(self):
        result = self.configure_policy(
            'set(PROJECT_IS_TOP_LEVEL FALSE)\nset(OXYGEN_USE_CCACHE ON)\n'
            + self.options(), success=False,
        )
        self.assertIn("CMAKE_CXX_COMPILER_LAUNCHER", result.stderr)

    def test_ui_instrumentation_matches_dependency_abi(self):
        self.configure_policy(
            'set(OXYGEN_BUILD_UI_TESTS ON)\n'
            'set(OXYGEN_IMGUI_TEST_ENGINE_AVAILABLE ON)\n' + self.options()
        )
        for requested, available in (("ON", "OFF"), ("OFF", "ON")):
            result = self.configure_policy(
                f'set(OXYGEN_BUILD_UI_TESTS {requested})\n'
                f'set(OXYGEN_IMGUI_TEST_ENGINE_AVAILABLE {available})\n'
                + self.options(), success=False,
            )
            self.assertIn("UI test option differs from ImGui", result.stderr)
        result = self.configure_policy(
            'set(OXYGEN_BUILD_UI_TESTS OFF)\n'
            'set(OXYGEN_CONAN_EXPECT_OXYGEN_BUILD_UI_TESTS ON)\n'
            + self.options(), success=False,
        )
        self.assertIn("conflicts with the Conan configuration", result.stderr)
        for prerequisite in ("OXYGEN_BUILD_FULL_ENGINE", "OXYGEN_BUILD_EXAMPLES"):
            result = self.configure_policy(
                'set(OXYGEN_BUILD_UI_TESTS ON)\n'
                'set(OXYGEN_IMGUI_TEST_ENGINE_AVAILABLE ON)\n'
                f'set({prerequisite} OFF)\n' + self.options(), success=False,
            )
            self.assertIn("requires the full engine", result.stderr)

    def test_conan_allows_only_local_narrowing(self):
        for optional in ("OXYGEN_BUILD_TESTS", "OXYGEN_BUILD_EXAMPLES", "OXYGEN_BUILD_DOCS",
                         "OXYGEN_BUILD_TOOLS", "OXYGEN_BUILD_BENCHMARKS"):
            with self.subTest(option=optional):
                self.configure_policy(
                    f"set(OXYGEN_CONAN_EXPECT_{optional} ON)\nset({optional} OFF)\n"
                    + self.options()
                )
                result = self.configure_policy(
                    f"set(OXYGEN_CONAN_EXPECT_{optional} OFF)\nset({optional} ON)\n"
                    + self.options(), success=False,
                )
                self.assertIn("conflicts with the Conan configuration", result.stderr)

    def test_binary_settings_and_package_outputs_are_locked(self):
        for setting in ("BUILD_SHARED_LIBS", "OXYGEN_WITH_TRACY", "OXYGEN_WITH_ASAN",
                        "OXYGEN_BUILD_TESTS"):
            with self.subTest(setting=setting):
                result = self.configure_policy(
                    'set(OXYGEN_CONAN_PACKAGE_BUILD ON)\n'
                    f"set(OXYGEN_CONAN_EXPECT_{setting} ON)\nset({setting} OFF)\n"
                    + self.options(), success=False,
                )
                self.assertIn("conflicts with the Conan configuration", result.stderr)

    def test_selection_closes_dependencies_and_rejects_unknowns(self):
        include = f'include("{ENGINE.as_posix()}/cmake/ModuleSelection.cmake")\n'
        self.configure_policy(
            'set(OXYGEN_MODULES Clap)\n' + include
            + 'if(NOT OXYGEN_ENABLED_MODULES STREQUAL "Base;TextWrap;Clap")\n'
            'message(FATAL_ERROR "Wrong module closure: ${OXYGEN_ENABLED_MODULES}")\nendif()\n'
        )
        result = self.configure_policy('set(OXYGEN_MODULES Vortex)\n' + include, success=False)
        self.assertIn("Unknown reusable Oxygen module", result.stderr)
        result = self.configure_policy('set(OXYGEN_MODULES OFF)\n' + include, success=False)
        self.assertIn("Unknown reusable Oxygen module", result.stderr)
        result = self.configure_policy(
            'set(PROJECT_IS_TOP_LEVEL FALSE)\n' + include, success=False,
        )
        self.assertIn("requires an explicit OXYGEN_MODULES", result.stderr)


@unittest.skipUnless(CONAN and CMAKE and os.environ.get("OXYGEN_RUN_MODULE_INTEGRATION"),
                     "Opt in with OXYGEN_RUN_MODULE_INTEGRATION=1 and a native Conan profile")
class ModuleIntegrationTests(CommandTests):
    def test_real_embedded_modules_preserve_parent(self):
        with tempfile.TemporaryDirectory(prefix="oxygen-parent-") as tmp:
            root = Path(tmp)
            (root / "conanfile.txt").write_text(
                "[requires]\nfmt/12.1.0\nasio/1.36.0\nmagic_enum/0.9.7\n"
                "[generators]\nCMakeToolchain\nCMakeDeps\n"
                "[options]\nfmt/*:header_only=True\n", encoding="utf-8",
            )
            profile = os.environ.get("OXYGEN_TEST_PROFILE", str(ENGINE / "profiles/windows-msvc.ini"))
            self.run_command([
                CONAN, "install", str(root), "--output-folder", str(root / "deps"),
                "--build=missing", "-pr:h", profile, "-pr:b", profile,
                "-s", "build_type=Release", "-s", "compiler.cppstd=23",
                "-c", "tools.cmake.cmaketoolchain:generator=Ninja",
            ], root)
            trap = root / "cmake"
            trap.mkdir()
            (trap / "BuildHelpers.cmake").write_text('message(FATAL_ERROR "Parent helper intercepted")\n')
            (root / ".clangd").write_text("# Parent owns this file\n")
            (root / "main.cpp").write_text(
                "#include <Oxygen/Base/Hash.h>\n"
                "#include <Oxygen/Composition/Component.h>\n"
                "#include <Oxygen/Composition/ComponentMacros.h>\n"
                "#include <Oxygen/OxCo/Coroutine.h>\n"
                "#include <Oxygen/Serio/MemoryStream.h>\n"
                "#include <Oxygen/Clap/Fluent/DSL.h>\n"
                "#include <Oxygen/TextWrap/TextWrap.h>\n"
                "struct Parent { virtual ~Parent() = default; };\n"
                "struct Child : Parent {};\n"
                "bool check_component();\n"
                "int main() { Child child; Parent* ptr = &child;\n"
                "return dynamic_cast<Child*>(ptr) && check_component() ? 0 : 1; }\n", encoding="utf-8",
            )
            (root / "component.cpp").write_text(
                '#include <Oxygen/Composition/Component.h>\n'
                '#include <Oxygen/Composition/ComponentMacros.h>\n'
                'class ConsumerComponent final : public oxygen::Component {\n'
                'OXYGEN_COMPONENT(ConsumerComponent)\n};\n'
                'bool check_component() { ConsumerComponent component;\n'
                'return component.GetTypeId() == ConsumerComponent::ClassTypeId(); }\n',
                encoding="utf-8",
            )
            (root / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 4.2)\nproject(Parent CXX)\n'
                'list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")\n'
                'set_property(GLOBAL PROPERTY USE_FOLDERS OFF)\n'
                'set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/parent-install" CACHE PATH "" FORCE)\n'
                'set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/parent-bin")\n'
                'set(BUILD_SHARED_LIBS ON CACHE BOOL "stale")\nset(BUILD_SHARED_LIBS OFF)\n'
                'set(OXYGEN_BUILD_TESTS ON CACHE BOOL "stale")\nset(OXYGEN_BUILD_TESTS OFF)\n'
                'set(OXYGEN_MODULES "Clap;Composition;OxCo;Serio")\n'
                f'add_subdirectory("{ENGINE.as_posix()}" oxygen)\n'
                'get_property(_folders GLOBAL PROPERTY USE_FOLDERS)\n'
                'if(_folders OR NOT CMAKE_INSTALL_PREFIX STREQUAL "${CMAKE_BINARY_DIR}/parent-install")\n'
                'message(FATAL_ERROR "Oxygen changed parent policy")\nendif()\n'
                'if(OXYGEN_BUILD_TESTS OR OXYGEN_BUILD_EXAMPLES OR OXYGEN_BUILD_DOCS OR OXYGEN_INSTALL)\n'
                'message(FATAL_ERROR "Unexpected embedded auxiliary targets")\nendif()\n'
                'if(TARGET oxygen-vortex OR TARGET oxygen-platform OR TARGET oxygen-testing)\n'
                'message(FATAL_ERROR "Unselected engine module present")\nendif()\n'
                'get_target_property(_type oxygen-base TYPE)\n'
                'if(NOT _type STREQUAL "STATIC_LIBRARY")\nmessage(FATAL_ERROR "Parent linkage ignored")\nendif()\n'
                'add_executable(parent_probe main.cpp component.cpp)\n'
                'set_source_files_properties(component.cpp PROPERTIES COMPILE_OPTIONS\n'
                '  "$<$<CXX_COMPILER_ID:MSVC>:/GR->;$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-rtti>")\n'
                'target_link_libraries(parent_probe PRIVATE oxygen::base oxygen::composition '
                'oxygen::oxco oxygen::serio oxygen::clap oxygen::textwrap)\n', encoding="utf-8",
            )
            self.run_command([
                CMAKE, "-S", str(root), "-B", str(root / "build"), "-G", "Ninja",
                f"-DCMAKE_TOOLCHAIN_FILE={(root / 'deps/conan_toolchain.cmake').as_posix()}",
                "-DCMAKE_BUILD_TYPE=Release",
            ], root)
            self.run_command([CMAKE, "--build", str(root / "build"), "--parallel", "4"], root)
            exe = "parent_probe.exe" if os.name == "nt" else "parent_probe"
            self.run_command([str(root / "build/parent-bin" / exe)], root)
            self.assertEqual((root / ".clangd").read_text(), "# Parent owns this file\n")


if __name__ == "__main__":
    unittest.main()
