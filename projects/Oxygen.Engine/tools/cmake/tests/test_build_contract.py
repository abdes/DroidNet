"""Exercise Oxygen's real build policy without provisioning engine dependencies.

Run from an x64 VS 2026 developer shell. OXYGEN_TEST_CMAKE can select an exact
CMake executable for local checks; CI exercises only the supported minimum.
"""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ENGINE = Path(__file__).resolve().parents[3]
CMAKE = os.environ.get("OXYGEN_TEST_CMAKE") or shutil.which("cmake")
CONAN = shutil.which("conan")


class CommandTests(unittest.TestCase):
    def run_command(self, command, root, *, success=True, env=None):
        result = subprocess.run(
            command, cwd=root, capture_output=True, text=True,
            encoding="utf-8", errors="replace", env=env,
        )
        output = result.stdout + result.stderr
        if success:
            self.assertEqual(result.returncode, 0, output)
        else:
            self.assertNotEqual(result.returncode, 0, output)
        return result


@unittest.skipUnless(CMAKE, "CMake is required")
class ConfigurationTests(CommandTests):
    def configure(self, root, generator, *arguments):
        return self.run_command(
            [CMAKE, "-S", str(root), "-B", str(root / "build"),
             "-G", generator, *arguments], root,
        )

    def write_project(self, root, *, toolchain=False, before=""):
        language = "CXX" if toolchain else "NONE"
        modules = ENGINE / "cmake"
        source = f"cmake_minimum_required(VERSION 4.2)\nproject(Contract {language})\n"
        if toolchain:
            source += f'include("{modules.as_posix()}/ToolchainRequirements.cmake")\n'
        source += before
        source += f'include("{modules.as_posix()}/BuildConfiguration.cmake")\n'
        source += 'file(WRITE "${CMAKE_BINARY_DIR}/selected.txt" "${CMAKE_BUILD_TYPE}")\n'
        if toolchain:
            source += (
                'add_executable(contract main.cpp)\n'
                'target_compile_features(contract PRIVATE cxx_std_23)\n'
            )
            (root / "main.cpp").write_text(
                "#include <expected>\n"
                "constexpr std::expected<int, int> result(23);\n"
                "static_assert(result.value() == 23);\n"
                "int main() { return result.value() == 23 ? 0 : 1; }\n",
                encoding="utf-8",
            )
        (root / "CMakeLists.txt").write_text(source, encoding="utf-8")

    def test_single_config_default_and_explicit_values(self):
        for supplied, expected in ((None, "Debug"), ("", "Debug"),
                                   ("Debug", "Debug"), ("Release", "Release"),
                                   ("RelWithDebInfo", "RelWithDebInfo"),
                                   ("OFF", "OFF")):
            with self.subTest(build_type=supplied), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                self.write_project(root)
                args = [] if supplied is None else [f"-DCMAKE_BUILD_TYPE={supplied}"]
                self.configure(root, "Ninja", *args)
                self.assertEqual((root / "build/selected.txt").read_text(), expected)
                # Reconfiguration must preserve the caller's next choice, too.
                self.configure(root, "Ninja", "-DCMAKE_BUILD_TYPE=Release")
                self.assertEqual((root / "build/selected.txt").read_text(), "Release")

    def test_empty_normal_variable_defaults(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.write_project(root, before='set(CMAKE_BUILD_TYPE "")\n')
            self.configure(root, "Ninja")
            self.assertEqual((root / "build/selected.txt").read_text(), "Debug")

    def test_multi_config_preserves_configurations(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.write_project(root)
            self.configure(root, "Ninja Multi-Config",
                           "-DCMAKE_CONFIGURATION_TYPES=Debug;Release")
            self.assertEqual((root / "build/selected.txt").read_text(), "")
            cache = (root / "build/CMakeCache.txt").read_text()
            self.assertRegex(cache, r"CMAKE_CONFIGURATION_TYPES:[^=\n]+=Debug;Release")
            self.assertNotIn("CMAKE_DEFAULT_CONFIGS:", cache)

    @unittest.skipUnless(os.name == "nt", "Windows toolchain integration")
    def test_supported_generators_compile_cpp23(self):
        for generator in ("Ninja", "Ninja Multi-Config", "Visual Studio 18 2026"):
            with self.subTest(generator=generator), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                self.write_project(root, toolchain=True)
                args = ["-A", "x64"] if generator.startswith("Visual Studio") else []
                self.configure(root, generator, *args)
                for config in ("Debug", "Release"):
                    if generator == "Ninja":
                        self.configure(root, generator, f"-DCMAKE_BUILD_TYPE={config}")
                    self.run_command(
                        [CMAKE, "--build", str(root / "build"), "--config", config], root,
                    )
                    executable = root / "build"
                    if generator != "Ninja":
                        executable /= config
                    self.run_command([str(executable / "contract.exe")], root)

    @unittest.skipUnless(os.name == "nt", "Windows toolchain integration")
    def test_actual_win32_compiler_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.write_project(root, toolchain=True)
            result = self.run_command(
                [CMAKE, "-S", str(root), "-B", str(root / "build"),
                 "-G", "Visual Studio 18 2026", "-A", "Win32"], root, success=False,
            )
            self.assertIn("requires an MSVC x64 target", result.stdout + result.stderr)


@unittest.skipUnless(CONAN, "Conan is required")
class RecipeContractTests(CommandTests):
    def test_validation_and_package_scoped_language(self):
        cases = (
            ([], None),
            (["-s", "Oxygen/*:compiler.cppstd=20"], "C++ standard"),
            (["-s", "compiler.version=194"], "requires MSVC 19.50"),
            (["-s", "compiler=clang", "-s", "compiler.version=18"], "requires MSVC 19.50"),
            (["-s", "arch=x86"], "requires Windows x64"),
            (["-s", "os=Linux"], "requires Windows x64"),
        )
        for arguments, error in cases:
            with self.subTest(arguments=arguments), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                recipe = ENGINE / "conanfile.py"
                (root / "conanfile.py").write_text(
                    "import importlib.util\n"
                    f"spec = importlib.util.spec_from_file_location('oxygen', {str(recipe)!r})\n"
                    "module = importlib.util.module_from_spec(spec)\n"
                    "spec.loader.exec_module(module)\n"
                    "class ContractFixture(module.OxygenConan):\n"
                    "    def requirements(self):\n"
                    "        pass\n"
                    "    def build_requirements(self):\n"
                    "        pass\n",
                    encoding="utf-8",
                )
                (root / "VERSION").write_text("0.1.0\n", encoding="utf-8")
                profile = ENGINE / "profiles/windows-msvc.ini"
                command = [
                    CONAN, "install", str(root), "--no-remote", "--build=never",
                    f"-pr:h={profile}", f"-pr:b={profile}",
                    "-s", "build_type=Release", *arguments,
                ]
                result = self.run_command(command, root, success=error is None)
                if error:
                    self.assertIn(error, result.stdout + result.stderr)
                else:
                    result = self.run_command(
                        [CONAN, "graph", "info", str(root), "--no-remote",
                         f"-pr:h={profile}", f"-pr:b={profile}",
                         "-s", "build_type=Release", "--format=json"], root,
                    )
                    graph = json.loads(result.stdout)["graph"]["nodes"]
                    self.assertEqual(graph["0"]["settings"]["compiler.cppstd"], "23")


if __name__ == "__main__":
    unittest.main()
