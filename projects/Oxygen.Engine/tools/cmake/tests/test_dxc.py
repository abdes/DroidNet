"""DXC tool/runtime ownership and deterministic discovery checks."""
from pathlib import Path
import tempfile
import unittest

from test_build_contract import CMAKE, ENGINE, CommandTests


@unittest.skipUnless(CMAKE, "CMake is required")
class DxcDiscoveryTests(CommandTests):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="oxygen dxc ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "build"
        self.host = self.root / "host package"
        self.tool = self.root / "build package"
        for folder in (self.host, self.tool):
            folder.mkdir()
        for name in ("dxc", "dxc.exe"):
            for folder in (self.host, self.tool):
                path = folder / name
                path.write_text("fixture")
                path.chmod(0o755)
        for name in ("dxcompiler.dll", "dxil.dll"):
            (self.host / name).write_text("fixture")
        (self.root / "dxc-config.cmake").write_text(
            "add_library(dxc::dxcompiler INTERFACE IMPORTED)\n", encoding="utf-8")
        (self.root / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 4.2)
project(DxcOwnership LANGUAGES NONE)
include("{ENGINE.as_posix()}/cmake/Dxc.cmake")
file(WRITE "${{CMAKE_BINARY_DIR}}/resolved.txt" "${{OXYGEN_DXC_EXECUTABLE}}\\n${{OXYGEN_DXCOMPILER_DLL}}\\n${{OXYGEN_DXIL_DLL}}\\n")
''', encoding="utf-8")

    def configure(self, tool=None, *, success=True):
        return self.run_command([
            CMAKE, "-S", str(self.root), "-B", str(self.build), "-G", "Ninja",
            f"-Ddxc_DIR={self.root.as_posix()}",
            f"-DOXYGEN_DXC_TOOL_BINDIRS={(tool or self.tool).as_posix()}",
            f"-DOXYGEN_DXC_RUNTIME_BINDIRS={self.host.as_posix()}",
        ], self.root, success=success)

    def test_contexts_remain_separate_and_package_changes_replace_paths(self):
        self.configure()
        paths = (self.build / "resolved.txt").read_text().splitlines()
        self.assertEqual(Path(paths[0]).parent, self.tool)
        self.assertEqual([Path(p).parent for p in paths[1:]], [self.host, self.host])
        replacement = self.root / "replacement build package"
        replacement.mkdir()
        for name in ("dxc", "dxc.exe"):
            path = replacement / name
            path.write_text("fixture")
            path.chmod(0o755)
        self.configure(replacement)
        self.assertEqual(Path((self.build / "resolved.txt").read_text().splitlines()[0]).parent, replacement)

    def test_missing_build_tool_does_not_fall_back_to_host_binary(self):
        result = self.configure(self.root / "missing", success=False)
        self.assertIn("OXYGEN_DXC_EXECUTABLE", result.stdout + result.stderr)

    def test_runtime_validator_is_required_independently_of_import_library(self):
        (self.host / "dxil.dll").unlink()
        result = self.configure(success=False)
        self.assertIn("OXYGEN_DXIL_DLL", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
