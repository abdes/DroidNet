# pyright: strict
"""Exercise real install rules and destination ownership without engine builds."""

import unittest
from pathlib import Path

from cmake_testing import (
    CMAKE,
    ENGINE,
    NINJA,
    NINJA_MULTI,
    VISUAL_STUDIO,
    Argument,
    CommandTestCase,
    cmake_project,
    include,
    read,
    requires_cmake,
    windows_only,
    write,
)


@requires_cmake
class InstallTests(CommandTestCase):
    temp_prefix = "oxygen install "

    def setUp(self) -> None:
        super().setUp()
        self.binary = self.root / "build"
        self.default = self.root / "deployment"
        write(self.root / "public.h", "#pragma once\n")
        self.write_project()

    def write_project(
        self, *, module: str = "Oxygen.Fixture", target_setup: str = "add_library(oxygen-fixture INTERFACE)\n"
    ) -> None:
        write(
            self.root / "CMakeLists.txt",
            cmake_project(
                "InstallFixture",
                "VERSION 1.2.3 LANGUAGES NONE",
                "set(CMAKE_INSTALL_LIBDIR lib)\n"
                f'set(OXYGEN_PROJECT_SOURCE_DIR "{ENGINE.as_posix()}")\n'
                f'set(OXYGEN_CONAN_DEPLOY_DIR "{self.default.as_posix()}")\n'
                "set(META_VERSION 1.2.3)\n"
                f"set(META_MODULE_NAME {module})\n"
                + include("Install.cmake")
                + target_setup
                + 'target_sources(oxygen-fixture INTERFACE FILE_SET HEADERS BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}" '
                "FILES public.h)\n"
                "oxygen_module_install(TARGETS oxygen-fixture EXPORT oxygen)\n"
                "oxygen_finalize_install()\n",
            ),
        )

    def configure_install(self, *arguments: str, generator: str = NINJA_MULTI) -> None:
        self.configure(self.root, self.binary, generator, *arguments)

    def install(self, *arguments: Argument, build: Path | None = None) -> None:
        self.run_command([CMAKE, "--install", build or self.binary, "--config", "Debug", *arguments])

    def assert_sdk(self, root: Path) -> None:
        self.assertTrue((root / "include/public.h").is_file())
        targets = read(root / "lib/cmake/Oxygen/OxygenTargets.cmake")
        self.assertIn("oxygen::fixture", targets)
        self.assertNotIn(str(self.root), targets)

    def test_default_full_and_component_install_match(self) -> None:
        self.configure_install()
        self.install("--component", "Oxygen_dev")
        self.assert_sdk(self.default / "Debug")
        self.assertFalse((self.default / "include").exists())
        self.install()
        self.assert_sdk(self.default / "Debug")

    def test_asan_default_install_uses_variant_directory(self) -> None:
        self.configure_install("-DOXYGEN_WITH_ASAN=ON")
        self.install()
        self.assert_sdk(self.default / "Asan")
        self.assertFalse((self.default / "Debug").exists())

    @windows_only
    def test_visual_studio_install_target(self) -> None:
        self.configure_install("-A", "x64", generator=VISUAL_STUDIO)
        self.build(self.binary, "--target", "INSTALL")
        self.assert_sdk(self.default / "Debug")

    def test_explicit_prefix_stays_flat_even_when_equal_to_default(self) -> None:
        self.configure_install()
        self.install("--prefix", self.default)
        self.assert_sdk(self.default)
        self.assertFalse((self.default / "Debug").exists())
        other = self.root / "custom sdk"
        self.install("--prefix", other, "--component", "Oxygen_dev")
        self.assert_sdk(other)

    def test_conan_package_override_stays_flat(self) -> None:
        self.configure_install("-DOXYGEN_CONAN_PACKAGE_BUILD=ON")
        package = self.root / "conan package"
        self.install("--prefix", package)
        self.assert_sdk(package)
        self.assertFalse((package / "Debug").exists())
        self.assertFalse(self.default.exists())

    def test_native_install_target_uses_configuration(self) -> None:
        self.configure_install()
        self.build(self.binary, "--target", "install", config="Release")
        self.assert_sdk(self.default / "Release")

    def test_reusable_export_finds_parent_supplied_dependencies(self) -> None:
        self.write_project(
            module="Oxygen.Base",
            target_setup=(
                "set(OXYGEN_BUILD_FULL_ENGINE FALSE)\n"
                "add_library(fmt::fmt-header-only INTERFACE IMPORTED)\n"
                "add_library(oxygen-fixture INTERFACE)\n"
                "target_link_libraries(oxygen-fixture INTERFACE fmt::fmt-header-only)\n"
            ),
        )
        self.configure_install()
        self.install()
        dependency = self.root / "parent dependency"
        write(dependency / "fmt-config.cmake", "add_library(fmt::fmt-header-only INTERFACE IMPORTED)\n")
        consumer = self.root / "consumer"
        write(
            consumer / "CMakeLists.txt",
            cmake_project(
                "Consumer",
                "LANGUAGES NONE",
                "find_package(Oxygen CONFIG REQUIRED COMPONENTS Base)\n"
                'if(NOT TARGET fmt::fmt-header-only)\nmessage(FATAL_ERROR "Dependency not resolved")\nendif()\n',
            ),
        )
        prefixes = f"{(self.default / 'Debug').as_posix()};{dependency.as_posix()}"
        self.configure(consumer, consumer / "build", NINJA, f"-DCMAKE_PREFIX_PATH={prefixes}")

    def test_configured_prefix_and_embedded_parent_are_owned_by_caller(self) -> None:
        custom = self.root / "configured sdk"
        self.configure_install(f"-DCMAKE_INSTALL_PREFIX={custom.as_posix()}")
        self.install()
        self.assert_sdk(custom)
        # A parent project must retain ownership even if its prefix happens to
        # equal Oxygen's normal development destination.
        parent = self.root / "parent"
        write(
            parent / "CMakeLists.txt",
            cmake_project("Parent", "LANGUAGES NONE", f'add_subdirectory("{self.root.as_posix()}" oxygen)\n'),
        )
        self.configure(
            parent,
            parent / "build",
            NINJA_MULTI,
            "-DOXYGEN_INSTALL=ON",
            f"-DCMAKE_INSTALL_PREFIX={self.default.as_posix()}",
        )
        self.install(build=parent / "build")
        self.assert_sdk(self.default)


if __name__ == "__main__":
    unittest.main()
