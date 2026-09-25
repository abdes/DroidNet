"""Pure SDK path-policy checks; no Conan lifecycle, generation or installation."""

import ast
from pathlib import Path
import re
import tempfile
from types import SimpleNamespace
import unittest


ENGINE = Path(__file__).resolve().parents[3]


def recipe_helpers():
    # Conan may be distributed as a standalone executable, outside the repo's
    # Python environment. Load only the actual pure helpers, not lifecycle hooks.
    source = ast.parse((ENGINE / "conanfile.py").read_text(encoding="utf-8"))
    recipe = next(node for node in source.body if isinstance(node, ast.ClassDef))
    names = {"_sdk_runtime_file", "_sdk_payload", "_sdk_rebase_binary_paths"}
    methods = [node for node in recipe.body if isinstance(node, ast.FunctionDef) and node.name in names]
    assert len(methods) == len(names)
    harness = ast.Module(body=[ast.ClassDef(name="Recipe", bases=[], keywords=[],
                                         body=methods, decorator_list=[])], type_ignores=[])
    ast.fix_missing_locations(harness)
    namespace = {"Path": Path, "re": re, "ConanInvalidConfiguration": ValueError}
    exec(compile(harness, str(ENGINE / "conanfile.py"), "exec"), namespace)
    return namespace["Recipe"]


Recipe = recipe_helpers()


class SdkPayloadTests(unittest.TestCase):
    def package(self, root, name, bindir, contents):
        folder = root / name
        runtime = folder / bindir
        runtime.mkdir(parents=True)
        (runtime / "TracyClient.dll").write_bytes(contents)
        info = SimpleNamespace(includedirs=[], bindirs=[str(runtime)], resdirs=[], libs=[], libdirs=[])
        return SimpleNamespace(ref=SimpleNamespace(name=name), package_folder=str(folder),
                               cpp_info=SimpleNamespace(aggregated_components=lambda: info))

    def payload(self, *packages):
        recipe = Recipe()
        recipe.dependencies = SimpleNamespace(host=SimpleNamespace(
            items=lambda: [(SimpleNamespace(test=False), package) for package in packages]))
        return recipe._sdk_payload()

    def test_flat_runtime_collision_is_detected_across_upstream_subdirectories(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = self.package(root, "first", "bin/Debug", b"first")
            second = self.package(root, "second", "bin/native", b"second")
            with self.assertRaisesRegex(ValueError, "SDK file collision: bin/tracyclient.dll"):
                self.payload(first, second)

    def test_identical_runtime_files_may_share_destination(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.payload(self.package(root, "first", "bin/Debug", b"same"),
                         self.package(root, "second", "bin/native", b"same"))

    def test_runtime_selection_keeps_only_dlls_and_matching_symbols(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("client.dll", "client.pdb", "tool.exe", "tool.pdb"):
                (root / name).touch()
            self.assertEqual({p.name for p in root.iterdir() if Recipe._sdk_runtime_file(p)},
                             {"client.dll", "client.pdb"})

    def test_rebased_imports_use_flat_runtime_and_library_directories(self):
        for config in ("Debug", "Release", "RelWithDebInfo"):
            with self.subTest(config=config):
                root = Path("C:/conan-cache/tracy")
                dll = root / "bin" / config / "TracyClient.dll"
                library = root / "lib" / config / "TracyClient.lib"
                variable = "${tracy_PACKAGE_FOLDER_" + config.upper() + "}"
                source = f'''set(runtime "{variable}/bin/{config}/TracyClient.dll")
set(absolute_runtime "{dll.as_posix()}")
set(implib "{variable}/lib/{config}/TracyClient.lib")
set(include "{variable}/include/tracy")
'''
                rebased = Recipe._sdk_rebase_binary_paths(source, [dll], [root], "_OXYGEN_SDK_RUNTIME_DIR")
                rebased = Recipe._sdk_rebase_binary_paths(rebased, [library], [root], "_OXYGEN_SDK_LIBRARY_DIR")
                self.assertEqual(rebased.count("${_OXYGEN_SDK_RUNTIME_DIR}/TracyClient.dll"), 2)
                self.assertIn("${_OXYGEN_SDK_LIBRARY_DIR}/TracyClient.lib", rebased)
                self.assertIn(f"{variable}/include/tracy", rebased)
                self.assertNotIn(f"bin/{config}", rebased)
                self.assertNotIn(f"lib/{config}", rebased)


if __name__ == "__main__":
    unittest.main()
