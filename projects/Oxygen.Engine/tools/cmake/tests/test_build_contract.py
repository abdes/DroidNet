# pyright: strict
"""Oxygen's toolchain, build-type and recipe policy, without engine dependencies."""

import json
import unittest

from cmake_testing import (
    CMAKE,
    CONAN,
    NINJA,
    NINJA_MULTI,
    VISUAL_STUDIO,
    WINDOWS_PROFILE,
    CommandTestCase,
    ProjectTestCase,
    cmake_project,
    include,
    oxygen_recipe,
    read,
    requires_cmake,
    requires_compiler,
    requires_conan,
    windows_only,
    write,
)

CPP23_PROBE = """#include <expected>
constexpr std::expected<int, int> result(23);
static_assert(result.value() == 23);
int main() { return result.value() == 23 ? 0 : 1; }
"""


@requires_cmake
class WindowsToolHostTests(CommandTestCase):
    def test_host_policy_without_configuring_or_compiling(self) -> None:
        cases = (
            ("Hostx64", "x64", "amd64/", None),
            ("Hostx86", "x64", "amd64/", "x64-hosted MSVC compiler"),
            ("Hostx64", "x86", "amd64/", "toolset host=x64"),
            ("Hostx64", "x64", "", "64-bit MSBuild"),
        )
        for compiler_host, toolset_host, msbuild_host, error in cases:
            with self.subTest(compiler_host=compiler_host, toolset_host=toolset_host, msbuild_host=msbuild_host):
                result = self.cmake_script(
                    "set(CMAKE_SYSTEM_NAME Windows)\n"
                    "set(CMAKE_CXX_COMPILER_ID MSVC)\n"
                    "set(CMAKE_CXX_COMPILER_VERSION 19.51)\n"
                    f'set(CMAKE_CXX_COMPILER "C:/VS/bin/{compiler_host}/x64/cl.exe")\n'
                    f'set(CMAKE_GENERATOR "{VISUAL_STUDIO}")\n'
                    f"set(CMAKE_VS_PLATFORM_TOOLSET_HOST_ARCHITECTURE {toolset_host})\n"
                    f'set(CMAKE_MAKE_PROGRAM "C:/VS/Bin/{msbuild_host}MSBuild.exe")\n'
                    "set(CMAKE_CXX_COMPILE_FEATURES cxx_std_23)\n"
                    "set(OXYGEN_COMPILER_TARGETS_X64 TRUE)\n" + include("ToolchainRequirements.cmake"),
                    success=error is None,
                )
                if error:
                    self.assertIn(error, result.stdout + result.stderr)


@requires_cmake
class ConfigurationTests(ProjectTestCase):
    def write_project(self, *, toolchain: bool = False, before: str = "") -> None:
        body = include("ToolchainRequirements.cmake") if toolchain else ""
        body += before + include("BuildConfiguration.cmake")
        body += 'file(WRITE "${CMAKE_BINARY_DIR}/selected.txt" "${CMAKE_BUILD_TYPE}")\n'
        if toolchain:
            body += "add_executable(contract main.cpp)\ntarget_compile_features(contract PRIVATE cxx_std_23)\n"
            self.write("main.cpp", CPP23_PROBE)
        self.write("CMakeLists.txt", cmake_project("Contract", "CXX" if toolchain else "NONE", body))

    def selected(self) -> str:
        return read(self.binary / "selected.txt")

    def test_single_config_default_and_explicit_values(self) -> None:
        self.write_project()
        for index, (supplied, expected) in enumerate(
            (
                (None, "Debug"),
                ("", "Debug"),
                ("Debug", "Debug"),
                ("Release", "Release"),
                ("RelWithDebInfo", "RelWithDebInfo"),
                ("OFF", "OFF"),
            )
        ):
            with self.subTest(build_type=supplied):
                self.binary = self.root / f"build-{index}"
                arguments = [] if supplied is None else [f"-DCMAKE_BUILD_TYPE={supplied}"]
                self.configure(self.source, self.binary, NINJA, *arguments)
                self.assertEqual(self.selected(), expected)
                # Reconfiguration must preserve the caller's next choice, too.
                self.configure(self.source, self.binary, NINJA, "-DCMAKE_BUILD_TYPE=Release")
                self.assertEqual(self.selected(), "Release")

    def test_empty_normal_variable_defaults(self) -> None:
        self.write_project(before='set(CMAKE_BUILD_TYPE "")\n')
        self.configure(self.source, self.binary)
        self.assertEqual(self.selected(), "Debug")

    def test_multi_config_preserves_configurations(self) -> None:
        self.write_project()
        self.configure(self.source, self.binary, NINJA_MULTI, "-DCMAKE_CONFIGURATION_TYPES=Debug;Release")
        self.assertEqual(self.selected(), "")
        cache = read(self.binary / "CMakeCache.txt")
        self.assertRegex(cache, r"CMAKE_CONFIGURATION_TYPES:[^=\n]+=Debug;Release")
        self.assertNotIn("CMAKE_DEFAULT_CONFIGS:", cache)

    @windows_only
    @requires_compiler
    def test_supported_generators_compile_cpp23(self) -> None:
        self.write_project(toolchain=True)
        for index, generator in enumerate((NINJA, NINJA_MULTI, VISUAL_STUDIO)):
            with self.subTest(generator=generator):
                self.binary = self.root / f"build-{index}"
                platform = ["-A", "x64"] if generator == VISUAL_STUDIO else []
                self.configure(self.source, self.binary, generator, *platform)
                for config in ("Debug", "Release"):
                    if generator == NINJA:
                        self.configure(self.source, self.binary, generator, f"-DCMAKE_BUILD_TYPE={config}")
                    self.build(self.binary, config=config)
                    folder = self.binary if generator == NINJA else self.binary / config
                    self.run_command([folder / "contract.exe"])

    @windows_only
    @requires_compiler
    def test_actual_win32_compiler_is_rejected(self) -> None:
        self.write_project(toolchain=True)
        self.assert_fails_with(
            [CMAKE, "-S", self.source, "-B", self.binary, "-G", VISUAL_STUDIO, "-A", "Win32"],
            "requires an MSVC x64 target",
        )


@requires_conan
class RecipeContractTests(CommandTestCase):
    def test_validation_and_package_scoped_language(self) -> None:
        cases: tuple[tuple[list[str], str | None], ...] = (
            ([], None),
            (["-s", "oxygen/*:compiler.cppstd=20"], "C++ standard"),
            (["-s", "compiler.version=194"], "requires MSVC 19.50"),
            (["-s", "compiler=clang", "-s", "compiler.version=18"], "requires MSVC 19.50"),
            (["-s", "arch=x86"], "requires Windows x64"),
            (["-s", "os=Linux"], "requires Windows x64"),
        )
        profiles = [f"-pr:h={WINDOWS_PROFILE}", f"-pr:b={WINDOWS_PROFILE}", "-s", "build_type=Release"]
        for index, (arguments, error) in enumerate(cases):
            with self.subTest(arguments=arguments):
                recipe = self.root / f"recipe-{index}"
                write(recipe / "conanfile.py", oxygen_recipe())
                write(recipe / "VERSION", "0.1.0\n")
                install = [CONAN, "install", recipe, "--no-remote", "--build=never", *profiles, *arguments]
                if error:
                    self.assert_fails_with(install, error)
                    continue
                self.run_command(install)
                result = self.run_command([CONAN, "graph", "info", recipe, "--no-remote", *profiles, "--format=json"])
                nodes = json.loads(result.stdout)["graph"]["nodes"]
                self.assertEqual(nodes["0"]["settings"]["compiler.cppstd"], "23")


if __name__ == "__main__":
    unittest.main()
