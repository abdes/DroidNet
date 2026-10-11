# pyright: strict
"""Verify configuration selection preserves CMake's compiler commands verbatim."""

import json
import unittest
from pathlib import Path

from cmake_testing import (
    CLANGD,
    CMAKE,
    ENGINE,
    NINJA_MULTI,
    CommandTestCase,
    cmake_project,
    load_module,
    mtimes,
    read,
    write,
)

helper = load_module("prepare_clangd", ENGINE / ".vscode/prepare_clangd.py")

CONFIGURATIONS = ["Debug", "Release"]

type CompileCommand = dict[str, str]


def entry(config: str, *, windows: bool = False, **overrides: str) -> CompileCommand:
    output = f"src/CMakeFiles/a.dir/{config}/main.cpp.obj"
    if windows:
        output = output.replace("/", "\\")
    return {
        "file": "/sources/main.cpp",
        "directory": "/build with spaces",
        "command": f'cl /DSELECTED_{config} /c "main.cpp"',
        "output": output,
        **overrides,
    }


class ClangdDatabaseTests(CommandTestCase):
    def build_tree(self, cache: str, commands: list[CompileCommand], name: str = "build") -> Path:
        root = self.root / name
        write(root / "CMakeCache.txt", cache)
        write(root / "compile_commands.json", json.dumps(commands))
        return root

    @staticmethod
    def multi_config_cache(configurations: str) -> str:
        return f"CMAKE_GENERATOR:INTERNAL=Ninja Multi-Config\nCMAKE_CONFIGURATION_TYPES:STRING={configurations}\n"

    def test_preserves_each_configuration_and_multiple_targets(self) -> None:
        debug = entry("Debug", windows=True)
        release = entry("Release")
        second = {**debug, "output": "CMakeFiles/other.dir/Debug/main.cpp.obj", "command": "other flags"}
        result = helper.split_commands([debug, release, second], CONFIGURATIONS, True)
        self.assertEqual(result, {"Debug": [debug, second], "Release": [release]})

    def test_short_paths_preserve_configurations_and_duplicate_sources(self) -> None:
        debug = entry("Debug", output="F:/build with spaces/.o/5219d702/Debug/bfc69912.obj")
        release = entry("Release", output="F:\\build\\.o\\5219d702\\Release\\bfc69912.obj")
        second = {**debug, "output": ".o/abcdef12/Debug/bfc69912.obj", "command": "other flags"}
        result = helper.split_commands([debug, release, second], CONFIGURATIONS, True)
        self.assertEqual(result, {"Debug": [debug, second], "Release": [release]})
        for output in (".o/not-a-hash/Debug/a.obj", ".o/1234abcd/Other/a.obj"):
            with self.subTest(output=output), self.assertRaises(ValueError):
                helper.split_commands([{**debug, "output": output}], CONFIGURATIONS, True)

    def test_unknown_output_never_falls_back(self) -> None:
        for unknown in (entry("Other"), entry("Debug", output="unclassified.obj")):
            with self.subTest(output=unknown["output"]), self.assertRaises(ValueError):
                helper.split_commands([unknown], ["Debug"], True)

    def test_separate_trees_keep_their_own_commands(self) -> None:
        ordinary, tracy = (
            self.build_tree(
                self.multi_config_cache("Debug"), [entry("Debug", command=f"cl /DVARIANT_{name} main.cpp")], name
            )
            for name in ("ordinary", "tracy")
        )
        helper.prepare(ordinary)
        first = ordinary / "clangd/Debug/compile_commands.json"
        before = first.read_bytes()
        helper.prepare(tracy)
        self.assertEqual(first.read_bytes(), before)
        self.assertIn("VARIANT_tracy", read(tracy / "clangd/Debug/compile_commands.json"))

    def test_atomic_outputs_and_noop_timestamps(self) -> None:
        commands = [entry("Debug"), entry("Release")]
        root = self.build_tree(self.multi_config_cache("Debug;Release"), commands)
        helper.prepare(root)
        output = root / "clangd/Debug/compile_commands.json"
        before = mtimes([output])
        helper.prepare(root)
        self.assertEqual(mtimes([output]), before)
        self.assertEqual(json.loads(read(output)), [commands[0]])
        write(root / "compile_commands.json", json.dumps([entry("Other")]))
        with self.assertRaises(ValueError):
            helper.prepare(root)
        self.assertEqual(mtimes([output]), before)

    def test_single_config_and_visual_studio_rejection(self) -> None:
        unused = entry("unused")
        root = self.build_tree("CMAKE_GENERATOR:INTERNAL=Ninja\nCMAKE_BUILD_TYPE:STRING=Release\n", [unused])
        self.assertEqual(helper.prepare(root), {"Release": 1})
        self.assertEqual(json.loads(read(root / "clangd/Release/compile_commands.json")), [unused])
        write(root / "CMakeCache.txt", "CMAKE_GENERATOR:INTERNAL=Visual Studio 18 2026\n")
        with self.assertRaisesRegex(ValueError, "Ninja build tree"):
            helper.prepare(root)

    @unittest.skipUnless(CLANGD and CMAKE, "clangd and CMake are required")
    def test_clangd_reads_selected_native_ninja_configuration(self) -> None:
        write(
            self.root / "CMakeLists.txt",
            cmake_project(
                "ClangdProbe",
                "CXX",
                "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\nadd_executable(probe main.cpp)\n"
                "target_compile_definitions(probe PRIVATE EXPECTED_DEBUG=$<IF:$<CONFIG:Debug>,1,0>)\n",
            ),
        )
        source = write(
            self.root / "main.cpp",
            "#ifdef NDEBUG\nstatic_assert(EXPECTED_DEBUG == 0);\n"
            "#else\nstatic_assert(EXPECTED_DEBUG == 1);\n#endif\n"
            "int main() { return 0; }\n",
        )
        for strategy in ("FULL", "SHORT"):
            with self.subTest(strategy=strategy):
                build = self.root / f"build-{strategy.lower()}"
                self.configure(self.root, build, NINJA_MULTI, f"-DCMAKE_INTERMEDIATE_DIR_STRATEGY={strategy}")
                helper.prepare(build)
                for config, expected in (("Debug", "1"), ("Release", "0")):
                    result = self.run_command(
                        [CLANGD, f"--check={source}", f"--compile-commands-dir={build / 'clangd' / config}"]
                    )
                    self.assertIn(f"EXPECTED_DEBUG={expected}", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
