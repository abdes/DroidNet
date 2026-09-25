"""Opt-in installed SDK checks for Oxygen's explicit CMakeConfigDeps generator."""
import os
from pathlib import Path
import shutil
import tempfile
import unittest

from test_build_contract import CMAKE, CONAN, ENGINE, CommandTests


@unittest.skipUnless(os.name == "nt" and CMAKE and CONAN
                     and os.environ.get("OXYGEN_RUN_SDK_GENERATOR_TESTS"),
                     "Requires an MSVC developer shell and OXYGEN_RUN_SDK_GENERATOR_TESTS=1")
class SdkGeneratorTests(CommandTests):
    def test_config_deps_exports_relocatable_dependencies(self):
        for libdir in ("lib", "lib/native"):
            with self.subTest(libdir=libdir):
                self.check_generator(libdir)

    def check_generator(self, libdir):
        with tempfile.TemporaryDirectory(prefix="oxygen sdk ") as temporary:
            root = Path(temporary)
            recipe = root / "recipe"
            recipe.mkdir()
            for name in ("conanfile.py", "VERSION"):
                shutil.copyfile(ENGINE / name, recipe / name)
            command = [CONAN, "install", str(recipe),
                       "-pr:h", str(ENGINE / "profiles/windows-msvc.ini"),
                       "-pr:b", str(ENGINE / "profiles/windows-msvc.ini"),
                       "-s", "build_type=Debug", "-o", "shared=True", "-o", "with_tracy=True",
                       "-o", "tools=False", "-o", "tests=False", "-o", "examples=False",
                       "-o", "benchmarks=False", "-o", "docs=False",
                       "-c", "tools.cmake.cmaketoolchain:generator=Ninja Multi-Config",
                       "--no-remote", "--build=never"]
            deploy = root / "deployed dependencies"
            command += [f"--deployer-folder={deploy.as_posix()}",
                        "--deployer-package=oxygen/" + (ENGINE / "VERSION").read_text().strip()]
            self.run_command(command, root)
            self.assertTrue((deploy / "Debug/bin/SDL3.dll").is_file())
            self.assertTrue((deploy / "Debug/bin/TracyClient.dll").is_file())
            self.assertFalse((deploy / "Debug/bin/Debug").exists())
            self.assertEqual(list((deploy / "Debug/bin").rglob("*.exe")), [])
            rules = recipe / "out/build-tracy-ninja/generators/oxygen-sdk"
            (root / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 4.2)
project(SdkProbe VERSION 1.0.0 LANGUAGES NONE)
set(CMAKE_INSTALL_LIBDIR "{libdir}")
set(META_VERSION 1.0.0)
set(META_MODULE_NAME Oxygen.Probe)
set(OXYGEN_PROJECT_SOURCE_DIR "{ENGINE.as_posix()}")
set(OXYGEN_SDK_DEPENDENCY_DIR "{rules.as_posix()}")
include("{ENGINE.as_posix()}/cmake/Install.cmake")
add_library(asio::asio INTERFACE IMPORTED)
add_library(Tracy::TracyClient INTERFACE IMPORTED)
add_library(SDL3::SDL3 INTERFACE IMPORTED)
add_library(oxygen-probe INTERFACE)
target_link_libraries(oxygen-probe INTERFACE asio::asio Tracy::TracyClient SDL3::SDL3)
oxygen_module_install(TARGETS oxygen-probe EXPORT oxygen)
oxygen_finalize_install()
''', encoding="utf-8")
            sdk = root / "sdk"
            self.run_command([CMAKE, "-S", str(root), "-B", str(root / "build"), "-G", "Ninja",
                              f"-DCMAKE_INSTALL_PREFIX={sdk.as_posix()}"], root)
            self.run_command([CMAKE, "--install", str(root / "build"), "--config", "Debug"], root)
            relocated = root / "relocated SDK"
            sdk.rename(relocated)
            self.assertTrue((relocated / libdir / "TracyClient.lib").is_file())
            self.assertTrue((relocated / "bin/TracyClient.dll").is_file())
            self.assertFalse((relocated / "bin/Debug").exists())
            self.assertFalse((relocated / libdir / "Debug").exists())
            self.assertEqual(list((relocated / "bin").rglob("*.exe")), [])
            registry = (relocated / libdir / "cmake/Oxygen/OxygenDependencies.cmake").read_text()
            self.assertIn("find_dependency(opengl_system", registry)
            for path in (relocated / libdir / "cmake").rglob("*.cmake"):
                self.assertNotIn("/.conan2/", path.read_text().replace("\\", "/"), str(path))
            consumer = root / "consumer"
            consumer.mkdir()
            (consumer / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 4.2)
project(SdkConsumer LANGUAGES CXX)
find_package(Oxygen CONFIG REQUIRED COMPONENTS Probe)
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
''', encoding="utf-8")
            (consumer / "main.cpp").write_text(
                '#include <asio/io_context.hpp>\n#include <SDL3/SDL_version.h>\n'
                'int main() { asio::io_context io; return io.poll() == 0 && SDL_GetVersion() >= 3000000 ? 0 : 1; }\n',
                encoding="utf-8")
            build = consumer / "build"
            self.run_command([CMAKE, "-S", str(consumer), "-B", str(build), "-G", "Ninja",
                              "-DCMAKE_BUILD_TYPE=Debug", f"-DCMAKE_PREFIX_PATH={relocated.as_posix()}",
                              f"-DOxygen_DIR={(relocated / libdir / 'cmake/Oxygen').as_posix()}",
                              "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF",
                              "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF"], root)
            self.run_command([CMAKE, "--build", str(build), "--parallel", "2"], root)
            env = dict(os.environ)
            env["PATH"] = os.path.join(os.environ["SystemRoot"], "System32")
            self.run_command([str(build / "consumer.exe")], root, env=env)


if __name__ == "__main__":
    unittest.main()
