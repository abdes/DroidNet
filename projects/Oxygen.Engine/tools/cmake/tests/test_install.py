"""Exercise real install rules and destination ownership without engine builds."""
from pathlib import Path
import os
import tempfile
import unittest

from test_build_contract import CMAKE, ENGINE, CommandTests


@unittest.skipUnless(CMAKE, "CMake is required")
class InstallTests(CommandTests):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="oxygen install ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "build"
        self.default = self.root / "deployment"
        (self.root / "public.h").write_text("#pragma once\n", encoding="utf-8")
        (self.root / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 4.2)
project(InstallFixture VERSION 1.2.3 LANGUAGES NONE)
set(CMAKE_INSTALL_LIBDIR lib)
set(OXYGEN_PROJECT_SOURCE_DIR "{ENGINE.as_posix()}")
set(OXYGEN_CONAN_DEPLOY_DIR "{self.default.as_posix()}")
set(META_VERSION 1.2.3)
set(META_MODULE_NAME Oxygen.Fixture)
include("{ENGINE.as_posix()}/cmake/Install.cmake")
add_library(oxygen-fixture INTERFACE)
target_sources(oxygen-fixture INTERFACE FILE_SET HEADERS BASE_DIRS "${{CMAKE_CURRENT_SOURCE_DIR}}" FILES public.h)
oxygen_module_install(TARGETS oxygen-fixture EXPORT oxygen)
oxygen_finalize_install()
''', encoding="utf-8")

    def configure(self, *args):
        self.run_command([CMAKE, "-S", str(self.root), "-B", str(self.build),
                          "-G", "Ninja Multi-Config", *args], self.root)

    def install(self, *args):
        self.run_command([CMAKE, "--install", str(self.build), "--config", "Debug", *args], self.root)

    def assert_sdk(self, root):
        self.assertTrue((root / "include/public.h").is_file())
        targets = (root / "lib/cmake/Oxygen/OxygenTargets.cmake").read_text()
        self.assertIn("oxygen::fixture", targets)
        self.assertNotIn(str(self.root), targets)

    def test_default_full_and_component_install_match(self):
        self.configure()
        self.install("--component", "Oxygen_dev")
        self.assert_sdk(self.default / "Debug")
        self.assertFalse((self.default / "include").exists())
        self.install()
        self.assert_sdk(self.default / "Debug")

    def test_asan_default_install_uses_variant_directory(self):
        self.configure("-DOXYGEN_WITH_ASAN=ON")
        self.install()
        self.assert_sdk(self.default / "Asan")
        self.assertFalse((self.default / "Debug").exists())

    @unittest.skipUnless(os.name == "nt", "Visual Studio is Windows-only")
    def test_visual_studio_install_target(self):
        self.run_command([CMAKE, "-S", str(self.root), "-B", str(self.build),
                          "-G", "Visual Studio 18 2026", "-A", "x64"], self.root)
        self.run_command([CMAKE, "--build", str(self.build), "--config", "Debug", "--target", "INSTALL"], self.root)
        self.assert_sdk(self.default / "Debug")

    def test_explicit_prefix_stays_flat_even_when_equal_to_default(self):
        self.configure()
        self.install("--prefix", str(self.default))
        self.assert_sdk(self.default)
        self.assertFalse((self.default / "Debug").exists())
        other = self.root / "custom sdk"
        self.install("--prefix", str(other), "--component", "Oxygen_dev")
        self.assert_sdk(other)

    def test_conan_package_override_stays_flat(self):
        self.configure("-DOXYGEN_CONAN_PACKAGE_BUILD=ON")
        package = self.root / "conan package"
        self.install("--prefix", str(package))
        self.assert_sdk(package)
        self.assertFalse((package / "Debug").exists())
        self.assertFalse(self.default.exists())

    def test_native_install_target_uses_configuration(self):
        self.configure()
        self.run_command([CMAKE, "--build", str(self.build), "--config", "Release", "--target", "install"], self.root)
        self.assert_sdk(self.default / "Release")

    def test_reusable_export_finds_parent_supplied_dependencies(self):
        project = self.root / "CMakeLists.txt"
        content = project.read_text().replace("Oxygen.Fixture", "Oxygen.Base")
        content = content.replace("add_library(oxygen-fixture INTERFACE)",
            "set(OXYGEN_BUILD_FULL_ENGINE FALSE)\n"
            "add_library(fmt::fmt-header-only INTERFACE IMPORTED)\n"
            "add_library(oxygen-fixture INTERFACE)\n"
            "target_link_libraries(oxygen-fixture INTERFACE fmt::fmt-header-only)")
        project.write_text(content, encoding="utf-8")
        self.configure()
        self.install()
        dependency = self.root / "parent dependency"
        dependency.mkdir()
        (dependency / "fmt-config.cmake").write_text(
            "add_library(fmt::fmt-header-only INTERFACE IMPORTED)\n", encoding="utf-8")
        consumer = self.root / "consumer"
        consumer.mkdir()
        (consumer / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 4.2)\nproject(Consumer LANGUAGES NONE)\n'
            'find_package(Oxygen CONFIG REQUIRED COMPONENTS Base)\n'
            'if(NOT TARGET fmt::fmt-header-only)\nmessage(FATAL_ERROR "Dependency not resolved")\nendif()\n',
            encoding="utf-8")
        self.run_command([CMAKE, "-S", str(consumer), "-B", str(consumer / "build"),
                          "-G", "Ninja", "-DCMAKE_PREFIX_PATH="
                          + (self.default / "Debug").as_posix() + ";" + dependency.as_posix()], self.root)

    def test_configured_prefix_and_embedded_parent_are_owned_by_caller(self):
        custom = self.root / "configured sdk"
        self.configure(f"-DCMAKE_INSTALL_PREFIX={custom.as_posix()}")
        self.install()
        self.assert_sdk(custom)
        # A parent project must retain ownership even if its prefix happens to
        # equal Oxygen's normal development destination.
        parent = self.root / "parent"
        parent.mkdir()
        (parent / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 4.2)\nproject(Parent LANGUAGES NONE)\n'
            f'add_subdirectory("{self.root.as_posix()}" oxygen)\n', encoding="utf-8")
        self.run_command([CMAKE, "-S", str(parent), "-B", str(parent / "build"),
                          "-G", "Ninja Multi-Config", "-DOXYGEN_INSTALL=ON",
                          f"-DCMAKE_INSTALL_PREFIX={self.default.as_posix()}"], parent)
        self.run_command([CMAKE, "--install", str(parent / "build"), "--config", "Debug"], parent)
        self.assert_sdk(self.default)


if __name__ == "__main__":
    unittest.main()
