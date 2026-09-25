"""Verify configuration selection preserves CMake's compiler commands verbatim."""

import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ENGINE = Path(__file__).resolve().parents[3]
CMAKE = os.environ.get("OXYGEN_TEST_CMAKE") or shutil.which("cmake")
spec = importlib.util.spec_from_file_location("prepare_clangd", ENGINE / ".vscode/prepare_clangd.py")
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)


class ClangdDatabaseTests(unittest.TestCase):
    def entry(self, config, *, windows=False):
        output = f"src/CMakeFiles/a.dir/{config}/main.cpp.obj"
        if windows:
            output = output.replace("/", "\\")
        return {"file": "/sources/main.cpp", "directory": "/build with spaces",
                "command": f'cl /DSELECTED_{config} /c "main.cpp"', "output": output}

    def test_preserves_each_configuration_and_multiple_targets(self):
        debug = self.entry("Debug", windows=True)
        release = self.entry("Release")
        second = {**debug, "output": "CMakeFiles/other.dir/Debug/main.cpp.obj", "command": "other flags"}
        result = helper.split_commands([debug, release, second], ["Debug", "Release"], True)
        self.assertEqual(result, {"Debug": [debug, second], "Release": [release]})

    def test_short_paths_preserve_configurations_and_duplicate_sources(self):
        debug = {**self.entry("Debug"), "output": "F:/build with spaces/.o/5219d702/Debug/bfc69912.obj"}
        release = {**self.entry("Release"), "output": "F:\\build\\.o\\5219d702\\Release\\bfc69912.obj"}
        second = {**debug, "output": ".o/abcdef12/Debug/bfc69912.obj", "command": "other flags"}
        result = helper.split_commands([debug, release, second], ["Debug", "Release"], True)
        self.assertEqual(result, {"Debug": [debug, second], "Release": [release]})
        for output in (".o/not-a-hash/Debug/a.obj", ".o/1234abcd/Other/a.obj"):
            with self.subTest(output=output), self.assertRaises(ValueError):
                helper.split_commands([{**debug, "output": output}], ["Debug", "Release"], True)

    def test_unknown_output_never_falls_back(self):
        with self.assertRaises(ValueError):
            helper.split_commands([self.entry("Other")], ["Debug"], True)
        with self.assertRaises(ValueError):
            helper.split_commands([{**self.entry("Debug"), "output": "unclassified.obj"}], ["Debug"], True)

    def test_separate_trees_keep_their_own_commands(self):
        with tempfile.TemporaryDirectory() as tmp:
            roots = [Path(tmp) / "ordinary", Path(tmp) / "tracy"]
            for root in roots:
                root.mkdir()
                (root / "CMakeCache.txt").write_text(
                    "CMAKE_GENERATOR:INTERNAL=Ninja Multi-Config\n"
                    "CMAKE_CONFIGURATION_TYPES:STRING=Debug\n")
                entry = {**self.entry("Debug"), "command": f"cl /DVARIANT_{root.name} main.cpp"}
                (root / "compile_commands.json").write_text(json.dumps([entry]))
            helper.prepare(roots[0])
            first = roots[0] / "clangd/Debug/compile_commands.json"
            before = first.read_bytes()
            helper.prepare(roots[1])
            self.assertEqual(first.read_bytes(), before)
            second = roots[1] / "clangd/Debug/compile_commands.json"
            self.assertIn("VARIANT_tracy", second.read_text())

    def test_atomic_outputs_and_noop_timestamps(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "CMakeCache.txt").write_text(
                "CMAKE_GENERATOR:INTERNAL=Ninja Multi-Config\n"
                "CMAKE_CONFIGURATION_TYPES:STRING=Debug;Release\n", encoding="utf-8",
            )
            commands = [self.entry("Debug"), self.entry("Release")]
            source = root / "compile_commands.json"
            source.write_text(json.dumps(commands), encoding="utf-8")
            helper.prepare(root)
            output = root / "clangd/Debug/compile_commands.json"
            before = output.stat().st_mtime_ns
            helper.prepare(root)
            self.assertEqual(output.stat().st_mtime_ns, before)
            self.assertEqual(json.loads(output.read_text()), [commands[0]])
            source.write_text(json.dumps([self.entry("Other")]))
            with self.assertRaises(ValueError):
                helper.prepare(root)
            self.assertEqual(output.stat().st_mtime_ns, before)

    def test_single_config_and_visual_studio_rejection(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            cache = root / "CMakeCache.txt"
            cache.write_text("CMAKE_GENERATOR:INTERNAL=Ninja\nCMAKE_BUILD_TYPE:STRING=Release\n")
            entry = self.entry("unused")
            (root / "compile_commands.json").write_text(json.dumps([entry]))
            self.assertEqual(helper.prepare(root), {"Release": 1})
            self.assertEqual(json.loads((root / "clangd/Release/compile_commands.json").read_text()), [entry])
            cache.write_text("CMAKE_GENERATOR:INTERNAL=Visual Studio 18 2026\n")
            with self.assertRaisesRegex(ValueError, "Ninja build tree"):
                helper.prepare(root)

    @unittest.skipUnless(os.environ.get("OXYGEN_RUN_CLANGD_INTEGRATION")
                         and shutil.which("clangd") and CMAKE,
                         "Opt in with OXYGEN_RUN_CLANGD_INTEGRATION=1 and native compiler/clangd")
    def test_clangd_reads_selected_native_ninja_configuration(self):
        with tempfile.TemporaryDirectory(prefix="oxygen-clangd-") as tmp:
            root = Path(tmp)
            (root / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 4.2)\nproject(ClangdProbe CXX)\n'
                'set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n'
                'add_executable(probe main.cpp)\n'
                'target_compile_definitions(probe PRIVATE EXPECTED_DEBUG=$<IF:$<CONFIG:Debug>,1,0>)\n',
                encoding="utf-8",
            )
            source = root / "main.cpp"
            source.write_text(
                '#ifdef NDEBUG\nstatic_assert(EXPECTED_DEBUG == 0);\n'
                '#else\nstatic_assert(EXPECTED_DEBUG == 1);\n#endif\n'
                'int main() { return 0; }\n', encoding="utf-8",
            )
            for strategy in ("FULL", "SHORT"):
                with self.subTest(strategy=strategy):
                    build = root / ("build-" + strategy.lower())
                    result = subprocess.run(
                        [CMAKE, "-S", str(root), "-B", str(build), "-G", "Ninja Multi-Config",
                         f"-DCMAKE_INTERMEDIATE_DIR_STRATEGY={strategy}"],
                        capture_output=True, text=True,
                    )
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    helper.prepare(build)
                    for config, expected in (("Debug", "1"), ("Release", "0")):
                        result = subprocess.run(
                            ["clangd", f"--check={source}", f"--compile-commands-dir={build / 'clangd' / config}"],
                            capture_output=True, text=True,
                        )
                        log = result.stdout + result.stderr
                        self.assertEqual(result.returncode, 0, log)
                        self.assertIn(f"EXPECTED_DEBUG={expected}", log)


if __name__ == "__main__":
    unittest.main()
