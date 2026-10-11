# pyright: strict
"""Installed SDK checks for Oxygen's explicit CMakeConfigDeps generator."""

import os
import shutil
import unittest
from pathlib import Path

from cmake_testing import (
    CMAKE,
    CONAN,
    ENGINE,
    NATIVE_COMPILER,
    NINJA,
    WINDOWS,
    WINDOWS_PROFILE,
    CommandTestCase,
    cmake_project,
    include,
    options,
    read,
    requires_env,
    write,
)

PROBE_SDK = """set(CMAKE_INSTALL_LIBDIR "{libdir}")
set(META_VERSION 1.0.0)
set(META_MODULE_NAME Oxygen.Probe)
set(OXYGEN_PROJECT_SOURCE_DIR "{engine}")
set(OXYGEN_SDK_DEPENDENCY_DIR "{rules}")
{install}add_library(asio::asio INTERFACE IMPORTED)
add_library(Tracy::TracyClient INTERFACE IMPORTED)
add_library(SDL3::SDL3 INTERFACE IMPORTED)
add_library(oxygen-probe INTERFACE)
target_link_libraries(oxygen-probe INTERFACE asio::asio Tracy::TracyClient SDL3::SDL3)
oxygen_module_install(TARGETS oxygen-probe EXPORT oxygen)
oxygen_finalize_install()
"""

CONSUMER = """find_package(Oxygen CONFIG REQUIRED COMPONENTS Probe)
get_target_property(tracy_runtime Tracy::TracyClient IMPORTED_LOCATION_DEBUG)
if(NOT tracy_runtime STREQUAL "${OXYGEN_RUNTIME_DIR}/TracyClient.dll")
  message(FATAL_ERROR "Tracy runtime path was not rebased: ${tracy_runtime}")
endif()
add_executable(consumer main.cpp)
target_compile_features(consumer PRIVATE cxx_std_23)
target_compile_definitions(consumer PRIVATE _WIN32_WINNT=0x0A00)
target_link_libraries(consumer PRIVATE oxygen::probe)
file(GLOB dlls "${OXYGEN_RUNTIME_DIR}/*.dll")
add_custom_command(TARGET consumer POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${dlls} "$<TARGET_FILE_DIR:consumer>"
  COMMAND_EXPAND_LISTS VERBATIM)
"""

CONSUMER_MAIN = """#include <asio/io_context.hpp>
#include <SDL3/SDL_version.h>
int main() { asio::io_context io; return io.poll() == 0 && SDL_GetVersion() >= 3000000 ? 0 : 1; }
"""


@unittest.skipUnless(
    WINDOWS and CMAKE and CONAN and NATIVE_COMPILER, "Windows, CMake, Conan and an MSVC developer shell are required"
)
@requires_env("OXYGEN_RUN_SDK_GENERATOR_TESTS", "Deploys the engine's packages from the local Conan cache")
class SdkGeneratorTests(CommandTestCase):
    temp_prefix = "oxygen sdk "

    def test_config_deps_exports_relocatable_dependencies(self) -> None:
        for libdir in ("lib", "lib/native"):
            with self.subTest(libdir=libdir):
                self.check_generator(self.root / libdir.replace("/", "-"), libdir)

    def check_generator(self, root: Path, libdir: str) -> None:
        recipe = root / "recipe"
        recipe.mkdir(parents=True)
        for name in ("conanfile.py", "VERSION"):
            shutil.copyfile(ENGINE / name, recipe / name)
        deploy = root / "deployed dependencies"
        version = read(ENGINE / "VERSION").strip()
        self.run_command(
            [
                CONAN,
                "install",
                recipe,
                "-pr:h",
                WINDOWS_PROFILE,
                "-pr:b",
                WINDOWS_PROFILE,
                "-s",
                "build_type=Debug",
                *options(
                    "shared=True",
                    "with_tracy=True",
                    "tools=False",
                    "tests=False",
                    "examples=False",
                    "benchmarks=False",
                    "docs=False",
                ),
                "-c",
                "tools.cmake.cmaketoolchain:generator=Ninja Multi-Config",
                "--no-remote",
                "--build=never",
                f"--deployer-folder={deploy.as_posix()}",
                f"--deployer-package=oxygen/{version}",
            ]
        )
        runtime = deploy / "Debug/bin"
        self.assertTrue((runtime / "SDL3.dll").is_file())
        self.assertTrue((runtime / "TracyClient.dll").is_file())
        self.assertFalse((runtime / "Debug").exists())
        self.assertEqual(list(runtime.rglob("*.exe")), [])

        rules = recipe / "out/build-tracy-ninja/generators/oxygen-sdk"
        write(
            root / "CMakeLists.txt",
            cmake_project(
                "SdkProbe",
                "VERSION 1.0.0 LANGUAGES NONE",
                PROBE_SDK.format(
                    libdir=libdir,
                    engine=ENGINE.as_posix(),
                    rules=rules.as_posix(),
                    install=include("Install.cmake"),
                ),
            ),
        )
        sdk = root / "sdk"
        self.configure(root, root / "build", NINJA, f"-DCMAKE_INSTALL_PREFIX={sdk.as_posix()}")
        self.run_command([CMAKE, "--install", root / "build", "--config", "Debug"])
        relocated = sdk.rename(root / "relocated SDK")
        self.assert_relocated(relocated, libdir)

        consumer = root / "consumer"
        write(consumer / "CMakeLists.txt", cmake_project("SdkConsumer", "LANGUAGES CXX", CONSUMER))
        write(consumer / "main.cpp", CONSUMER_MAIN)
        build = consumer / "build"
        self.configure(
            consumer,
            build,
            NINJA,
            "-DCMAKE_BUILD_TYPE=Debug",
            f"-DCMAKE_PREFIX_PATH={relocated.as_posix()}",
            f"-DOxygen_DIR={(relocated / libdir / 'cmake/Oxygen').as_posix()}",
            "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF",
            "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF",
        )
        self.run_command([CMAKE, "--build", build, "--parallel", "2"])
        isolated = {**os.environ, "PATH": str(Path(os.environ["SYSTEMROOT"]) / "System32")}
        self.run_command([build / "consumer.exe"], env=isolated)

    def assert_relocated(self, sdk: Path, libdir: str) -> None:
        self.assertTrue((sdk / libdir / "TracyClient.lib").is_file())
        self.assertTrue((sdk / "bin/TracyClient.dll").is_file())
        self.assertFalse((sdk / "bin/Debug").exists())
        self.assertFalse((sdk / libdir / "Debug").exists())
        self.assertEqual(list((sdk / "bin").rglob("*.exe")), [])
        self.assertIn("find_dependency(opengl_system", read(sdk / libdir / "cmake/Oxygen/OxygenDependencies.cmake"))
        for path in (sdk / libdir / "cmake").rglob("*.cmake"):
            self.assertNotIn("/.conan2/", read(path).replace("\\", "/"), str(path))


if __name__ == "__main__":
    unittest.main()
