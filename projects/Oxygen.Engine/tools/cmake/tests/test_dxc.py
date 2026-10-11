# pyright: strict
"""DXC tool/runtime ownership and deterministic discovery checks."""

import unittest
from collections.abc import Iterable
from pathlib import Path

from cmake_testing import CommandTestCase, Result, cmake_project, include, read, requires_cmake, write

EXECUTABLES = ("dxc", "dxc.exe")
RUNTIME_LIBRARIES = ("dxcompiler.dll", "dxil.dll")


def package(folder: Path, names: Iterable[str]) -> Path:
    """A fake package folder holding executable stand-ins for ``names``."""
    for name in names:
        write(folder / name, "fixture").chmod(0o755)
    return folder


@requires_cmake
class DxcDiscoveryTests(CommandTestCase):
    temp_prefix = "oxygen dxc "

    def setUp(self) -> None:
        super().setUp()
        self.binary = self.root / "build"
        self.host = package(self.root / "host package", EXECUTABLES + RUNTIME_LIBRARIES)
        self.tool = package(self.root / "build package", EXECUTABLES)
        write(self.root / "dxc-config.cmake", "add_library(dxc::dxcompiler INTERFACE IMPORTED)\n")
        write(
            self.root / "CMakeLists.txt",
            cmake_project(
                "DxcOwnership",
                "LANGUAGES NONE",
                include("Dxc.cmake") + 'file(WRITE "${CMAKE_BINARY_DIR}/resolved.txt" '
                '"${OXYGEN_DXC_EXECUTABLE}\\n${OXYGEN_DXCOMPILER_DLL}\\n${OXYGEN_DXIL_DLL}\\n")\n',
            ),
        )

    def configure_dxc(self, tool: Path | None = None, *, success: bool = True) -> Result:
        return self.configure(
            self.root,
            self.binary,
            "Ninja",
            f"-Ddxc_DIR={self.root.as_posix()}",
            f"-DOXYGEN_DXC_TOOL_BINDIRS={(tool or self.tool).as_posix()}",
            f"-DOXYGEN_DXC_RUNTIME_BINDIRS={self.host.as_posix()}",
            success=success,
        )

    def resolved(self) -> list[Path]:
        return [Path(path).parent for path in read(self.binary / "resolved.txt").splitlines()]

    def test_contexts_remain_separate_and_package_changes_replace_paths(self) -> None:
        self.configure_dxc()
        self.assertEqual(self.resolved(), [self.tool, self.host, self.host])
        replacement = package(self.root / "replacement build package", EXECUTABLES)
        self.configure_dxc(replacement)
        self.assertEqual(self.resolved()[0], replacement)

    def test_missing_build_tool_does_not_fall_back_to_host_binary(self) -> None:
        result = self.configure_dxc(self.root / "missing", success=False)
        self.assertIn("OXYGEN_DXC_EXECUTABLE", result.stdout + result.stderr)

    def test_runtime_validator_is_required_independently_of_import_library(self) -> None:
        (self.host / "dxil.dll").unlink()
        result = self.configure_dxc(success=False)
        self.assertIn("OXYGEN_DXIL_DLL", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
