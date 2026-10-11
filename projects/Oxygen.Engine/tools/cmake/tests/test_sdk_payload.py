# pyright: strict
"""Pure SDK path-policy checks; no Conan lifecycle, generation or installation."""

import ast
import re
import unittest
from collections.abc import Sequence
from pathlib import Path
from types import SimpleNamespace
from typing import Any

from cmake_testing import ENGINE, CommandTestCase

RECIPE = ENGINE / "conanfile.py"
RECIPE_HELPERS = {"_sdk_runtime_file", "_sdk_payload", "_sdk_rebase_binary_paths"}


class SdkHelpers:
    """Typed access to the recipe's pure SDK helpers, without its Conan lifecycle hooks.

    Conan may be a standalone executable outside the repository's Python
    environment, so the recipe module itself cannot be imported here. Only the
    helper methods are compiled, into a stand-in class.
    """

    def __init__(self, recipe: Path) -> None:
        tree = ast.parse(recipe.read_text(encoding="utf-8"))
        recipe_class = next(node for node in tree.body if isinstance(node, ast.ClassDef))
        methods: list[ast.stmt] = [
            node for node in recipe_class.body if isinstance(node, ast.FunctionDef) and node.name in RECIPE_HELPERS
        ]
        if len(methods) != len(RECIPE_HELPERS):
            raise LookupError(f"{recipe} no longer defines {sorted(RECIPE_HELPERS)}")
        body: list[ast.stmt] = [ast.ClassDef(name="Recipe", bases=[], keywords=[], body=methods, decorator_list=[])]
        harness = ast.fix_missing_locations(ast.Module(body=body, type_ignores=[]))
        namespace: dict[str, Any] = {"Path": Path, "re": re, "ConanInvalidConfiguration": ValueError}
        exec(compile(harness, str(recipe), "exec"), namespace)
        self._recipe: Any = namespace["Recipe"]

    def runtime_file(self, path: Path) -> bool:
        return bool(self._recipe._sdk_runtime_file(path))

    def rebase_binary_paths(
        self, text: str, artifacts: Sequence[Path], roots: Sequence[Path], destination_variable: str
    ) -> str:
        return str(self._recipe._sdk_rebase_binary_paths(text, artifacts, roots, destination_variable))

    def payload(self, packages: Sequence[SimpleNamespace]) -> None:
        """Select the SDK payload from host dependency ``packages``; raises on conflicts."""
        recipe = self._recipe()
        recipe.dependencies = SimpleNamespace(
            host=SimpleNamespace(items=lambda: [(SimpleNamespace(test=False), package) for package in packages])
        )
        recipe._sdk_payload()


HELPERS = SdkHelpers(RECIPE)


class SdkPayloadTests(CommandTestCase):
    def package(self, name: str, bindir: str, contents: bytes) -> SimpleNamespace:
        """A stand-in Conan dependency with one runtime DLL under ``bindir``."""
        folder = self.root / name
        runtime = folder / bindir
        runtime.mkdir(parents=True)
        (runtime / "TracyClient.dll").write_bytes(contents)
        info = SimpleNamespace(includedirs=[], bindirs=[str(runtime)], resdirs=[], libs=[], libdirs=[])
        return SimpleNamespace(
            ref=SimpleNamespace(name=name),
            package_folder=str(folder),
            cpp_info=SimpleNamespace(aggregated_components=lambda: info),
        )

    def test_flat_runtime_collision_is_detected_across_upstream_subdirectories(self) -> None:
        first = self.package("first", "bin/Debug", b"first")
        second = self.package("second", "bin/native", b"second")
        with self.assertRaisesRegex(ValueError, "SDK file collision: bin/tracyclient.dll"):
            HELPERS.payload([first, second])

    def test_identical_runtime_files_may_share_destination(self) -> None:
        HELPERS.payload([self.package("first", "bin/Debug", b"same"), self.package("second", "bin/native", b"same")])

    def test_runtime_selection_keeps_only_dlls_and_matching_symbols(self) -> None:
        for name in ("client.dll", "client.pdb", "tool.exe", "tool.pdb"):
            (self.root / name).touch()
        selected = {path.name for path in self.root.iterdir() if HELPERS.runtime_file(path)}
        self.assertEqual(selected, {"client.dll", "client.pdb"})

    def test_rebased_imports_use_flat_runtime_and_library_directories(self) -> None:
        root = Path("C:/conan-cache/tracy")
        for config in ("Debug", "Release", "RelWithDebInfo"):
            with self.subTest(config=config):
                dll = root / "bin" / config / "TracyClient.dll"
                library = root / "lib" / config / "TracyClient.lib"
                variable = "${tracy_PACKAGE_FOLDER_" + config.upper() + "}"
                source = (
                    f'set(runtime "{variable}/bin/{config}/TracyClient.dll")\n'
                    f'set(absolute_runtime "{dll.as_posix()}")\n'
                    f'set(implib "{variable}/lib/{config}/TracyClient.lib")\n'
                    f'set(include "{variable}/include/tracy")\n'
                )
                rebased = HELPERS.rebase_binary_paths(source, [dll], [root], "_OXYGEN_SDK_RUNTIME_DIR")
                rebased = HELPERS.rebase_binary_paths(rebased, [library], [root], "_OXYGEN_SDK_LIBRARY_DIR")
                self.assertEqual(rebased.count("${_OXYGEN_SDK_RUNTIME_DIR}/TracyClient.dll"), 2)
                self.assertIn("${_OXYGEN_SDK_LIBRARY_DIR}/TracyClient.lib", rebased)
                self.assertIn(f"{variable}/include/tracy", rebased)
                self.assertNotIn(f"bin/{config}", rebased)
                self.assertNotIn(f"lib/{config}", rebased)


if __name__ == "__main__":
    unittest.main()
