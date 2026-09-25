"""Exercise the real recipe's preset generation without building dependencies."""

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ENGINE = Path(__file__).resolve().parents[3]
CONAN = shutil.which("conan")
CMAKE = shutil.which("cmake")
POWERSHELL = shutil.which("pwsh")


@unittest.skipUnless(CONAN and CMAKE, "Conan and CMake are required")
class PresetGenerationTests(unittest.TestCase):
    def copy_build_tree_cli(self, root):
        for relative in ("tools/build-tree.ps1", "tools/cli/BuildSelection.ps1", ".vscode/prepare_clangd.py"):
            target = root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ENGINE / relative, target)

    def make_fixture(self, root):
        recipe = ENGINE / "conanfile.py"
        (root / "conanfile.py").write_text(
            "import importlib.util\n"
            f"spec = importlib.util.spec_from_file_location('oxygen_recipe', {str(recipe)!r})\n"
            "module = importlib.util.module_from_spec(spec)\nspec.loader.exec_module(module)\n"
            "class Fixture(module.OxygenConan):\n"
            "    def requirements(self): pass\n"
            "    def build_requirements(self): pass\n", encoding="utf-8")
        (root / "VERSION").write_text("0.1.0\n", encoding="utf-8")
        (root / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 4.2)\nproject(Fixture NONE)\n", encoding="utf-8")
        shutil.copyfile(ENGINE / "CMakePresets.json", root / "CMakePresets.json")

    def test_incompatible_install_preserves_existing_metadata(self):
        scenarios = (
            ("Ninja Multi-Config", [], ["-o", "shared=False"]),
            ("Ninja Multi-Config", [], ["-s", "arch=x86"]),
            ("Ninja Multi-Config", [], ["-s", "compiler.version=194"]),
            ("Ninja Multi-Config", ["-o", "shared=False"], ["-s", "compiler.runtime=static"]),
            ("Visual Studio 18 2026", [], ["-c", "tools.cmake.cmaketoolchain:generator=Visual Studio 17 2022"]),
        )
        for generator, initial, changed in scenarios:
            with self.subTest(changed=changed), tempfile.TemporaryDirectory(prefix="oxygen-identity-") as directory:
                root = Path(directory)
                self.make_fixture(root)
                common = ["-o", "modules=Base"] + initial
                self.install(root, generator, False, False, "Release", common)
                self.install(root, generator, False, False, "Debug", common)
                before = {p.relative_to(root): p.read_bytes() for p in (root / "out").rglob("*") if p.is_file()}
                presets = (root / "CMakeUserPresets.json").read_bytes()
                # deploy() executes before generate(), so both entry points must
                # reject before the existing toolchain/SDK can be overwritten.
                result = self.install(root, generator, False, False, "Release", common + changed + [
                    "--deployer-package=oxygen/0.1.0", f"--deployer-folder={root / 'out/install'}",
                ], succeeds=False)
                self.assertIn("Incompatible Oxygen build-tree reuse", result.stderr)
                after = {p.relative_to(root): p.read_bytes() for p in (root / "out").rglob("*") if p.is_file()}
                self.assertEqual(before, after)
                self.assertEqual(presets, (root / "CMakeUserPresets.json").read_bytes())

    def test_unmarked_tree_requires_explicit_regeneration(self):
        with tempfile.TemporaryDirectory(prefix="oxygen-legacy-identity-") as directory:
            root = Path(directory)
            self.make_fixture(root)
            self.install(root, "Ninja Multi-Config", False, False, "Release")
            folder = root / "out/build-ninja/generators"
            (folder / "oxygen-build-identity.json").unlink()
            before = (folder / "conan_toolchain.cmake").read_bytes()
            result = self.install(root, "Ninja Multi-Config", False, False, "Release", succeeds=False)
            self.assertIn("predates build-identity validation", result.stderr)
            self.assertEqual(before, (folder / "conan_toolchain.cmake").read_bytes())

    def test_single_and_multi_config_ninja_coexist(self):
        with tempfile.TemporaryDirectory(prefix="oxygen-ninja-generators-") as directory:
            root = Path(directory)
            self.make_fixture(root)
            expected = set()
            for generator, suffix in (("Ninja", "ninja-single"), ("Ninja Multi-Config", "ninja")):
                for config in ("Debug", "Release"):
                    self.install(root, generator, False, False, config)
                    expected.add(f"conan-{suffix}-{config.lower()}")
                    self.assert_presets(root, expected)

    def test_configurations_share_configure_time_options(self):
        with tempfile.TemporaryDirectory(prefix="oxygen-config-options-") as directory:
            root = Path(directory)
            self.make_fixture(root)
            self.install(root, "Ninja Multi-Config", False, False, "Debug")
            before = (root / "out/build-ninja/generators/conan_toolchain.cmake").read_bytes()
            for changed in (("-o", "tests=False"), ("-o", "modules=Base"),
                            ("-o", "awaitable_state_checker=True")):
                result = self.install(root, "Ninja Multi-Config", False, False, "Release", changed, succeeds=False)
                self.assertIn("Incompatible Oxygen build-tree options", result.stderr)
                self.assertEqual(before, (root / "out/build-ninja/generators/conan_toolchain.cmake").read_bytes())
            self.install(root, "Ninja Multi-Config", False, False, "Release")

    def test_ide_environment_does_not_override_conan_generator(self):
        with tempfile.TemporaryDirectory(prefix="oxygen-default-generator-") as directory:
            root = Path(directory)
            self.make_fixture(root)
            profile = ENGINE / "profiles/windows-msvc.ini"
            result = subprocess.run([
                CONAN, "install", str(root), "--no-remote", "--build=never",
                f"-pr:h={profile}", f"-pr:b={profile}", "-s", "build_type=Release",
            ], cwd=root, env={**os.environ, "VSCODE_PID": "12345"},
                capture_output=True, text=True, encoding="utf-8", errors="replace")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            native = json.loads((root / "out/build-vs/generators/CMakePresets.json").read_text())
            self.assertEqual(native["configurePresets"][0]["generator"], "Visual Studio 18 2026")
            self.assertFalse((root / "out/build-ninja").exists())

    def test_sdk_install_destinations(self):
        with tempfile.TemporaryDirectory(prefix="oxygen-sdk-roots-") as directory:
            root = Path(directory)
            self.make_fixture(root)
            (root / "AUTHORS").write_text("fixture", encoding="utf-8")
            (root / "LICENSE").write_text("fixture", encoding="utf-8")
            (root / "payload.txt").write_text("SDK payload", encoding="utf-8")
            (root / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 4.2)\nproject(Fixture NONE)\n"
                "set(OXYGEN_PROJECT_SOURCE_DIR ${CMAKE_SOURCE_DIR})\n"
                "set(OXYGEN_BUILD_FULL_ENGINE FALSE)\nset(CMAKE_INSTALL_LIBDIR lib)\n"
                f'include("{ENGINE.as_posix()}/cmake/Install.cmake")\n'
                'install(FILES payload.txt DESTINATION share)\n', encoding="utf-8")
            for generator, suffix in (("Ninja Multi-Config", "ninja"), ("Visual Studio 18 2026", "vs")):
                for tracy, asan in ((False, False), (True, False), (False, True), (True, True)):
                    variant = ("tracy-" if tracy else "") + ("asan-" if asan else "") + suffix
                    config = "Debug" if asan else "Release"
                    self.install(root, generator, tracy, asan, config)
                    self.run_command([CMAKE, "--preset", f"oxygen-{variant}-default"], root)
                    self.run_command([CMAKE, "--install", str(root / f"out/build-{variant}"), "--config", config], root)
                    destination = root / "out" / ("install-tracy" if tracy else "install") / ("Asan" if asan else config)
                    self.assertEqual((destination / "share/payload.txt").read_text(), "SDK payload")
                    cache = (root / f"out/build-{variant}/CMakeCache.txt").read_text()
                    self.assertIn(f"CMAKE_INSTALL_PREFIX:PATH={destination.parent.as_posix()}", cache)

    def test_local_narrowing_resets_on_normal_preset_configure(self):
        with tempfile.TemporaryDirectory(prefix="oxygen-policy-") as directory:
            root = Path(directory)
            recipe = ENGINE / "conanfile.py"
            (root / "conanfile.py").write_text(
                "import importlib.util\n"
                f"spec = importlib.util.spec_from_file_location('oxygen_recipe', {str(recipe)!r})\n"
                "module = importlib.util.module_from_spec(spec)\nspec.loader.exec_module(module)\n"
                "class PolicyFixture(module.OxygenConan):\n"
                "    def requirements(self):\n        pass\n"
                "    def build_requirements(self): pass\n", encoding="utf-8",
            )
            (root / "VERSION").write_text("0.1.0\n", encoding="utf-8")
            shutil.copyfile(ENGINE / "CMakePresets.json", root / "CMakePresets.json")
            (root / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 4.2)\nproject(Policy NONE)\n"
                "set(OXYGEN_BUILD_FULL_ENGINE TRUE)\n"
                f'include("{ENGINE.as_posix()}/cmake/ProjectOptions.cmake")\n'
                'file(WRITE "${CMAKE_BINARY_DIR}/policy.txt" "${OXYGEN_BUILD_TESTS}")\n',
                encoding="utf-8",
            )
            self.install(root, "Ninja Multi-Config", False, False, "Release")
            configure = [CMAKE, "--preset", "oxygen-ninja-default"]
            self.run_command(configure + ["-DOXYGEN_BUILD_TESTS=OFF"], root)
            state = root / "out/build-ninja/policy.txt"
            self.assertEqual(state.read_text(), "OFF")
            self.run_command(configure, root)
            self.assertEqual(state.read_text(), "ON")
            result = subprocess.run(configure + ["-DBUILD_SHARED_LIBS=OFF"], cwd=root,
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("conflicts with the Conan configuration", result.stderr)
            profile = ENGINE / "profiles/windows-msvc.ini"
            self.run_command([
                CONAN, "install", str(root), "--no-remote", "--build=never",
                f"-pr:h={profile}", f"-pr:b={profile}", "-s", "build_type=Release",
                "-o", "tests=False", "-c", "tools.cmake.cmaketoolchain:generator=Ninja Multi-Config",
            ], root)
            self.run_command(configure, root)
            self.assertEqual(state.read_text(), "OFF")
            result = subprocess.run(configure + ["-DOXYGEN_BUILD_TESTS=ON"], cwd=root,
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("conflicts with the Conan configuration", result.stderr)

    @unittest.skipUnless(POWERSHELL, "PowerShell is required")
    def test_project_file_api_repairs_codemodel_only_trees(self):
        source = (ENGINE / "CMakeLists.txt").read_text(encoding="utf-8")
        query = re.search(r"(?ms)^cmake_file_api\(\s*QUERY\b.*?^\)", source)
        self.assertIsNotNone(query, "The project must own its File API requests")
        with tempfile.TemporaryDirectory(prefix="oxygen-file-api-") as directory:
            root = Path(directory)
            build = root / "build"
            legacy_query = build / ".cmake/api/v1/query/codemodel-v2"
            legacy_query.parent.mkdir(parents=True)
            legacy_query.touch()
            project = "cmake_minimum_required(VERSION 3.30)\nproject(FileApi NONE)\n"
            (root / "CMakeLists.txt").write_text(project, encoding="utf-8")
            environment = {**os.environ, "CMAKE_CONFIG_DIR": str(root / "user-cmake")}
            command = [CMAKE, "-S", str(root), "-B", str(build), "-G", "Ninja"]

            def configure():
                result = subprocess.run(command, cwd=root, env=environment, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                index = sorted((build / ".cmake/api/v1/reply").glob("index-*.json"))[-1]
                return json.loads(index.read_text(encoding="utf-8"))

            self.assertNotIn("cache", {obj["kind"] for obj in configure()["objects"]})
            helper = ENGINE / "tools/cli/BuildSelection.ps1"
            check = root / "check.ps1"
            check.write_text(f". '{helper}'\nif (Test-OxygenFileApiReply '{build}') {{ throw 'Accepted partial reply' }}\n", encoding="utf-8")
            self.run_command([POWERSHELL, "-NoProfile", "-File", str(check)], root)

            (root / "CMakeLists.txt").write_text(project + query.group() + "\n", encoding="utf-8")
            repaired = configure()
            self.assertEqual({obj["kind"] for obj in repaired["objects"]}, {"cache", "codemodel", "cmakeFiles", "toolchains"})
            self.assertTrue(all((build / ".cmake/api/v1/reply" / obj["jsonFile"]).is_file() for obj in repaired["objects"]))
            self.assertEqual(list(legacy_query.parent.iterdir()), [legacy_query])
            check.write_text(f". '{helper}'\nif (-not (Test-OxygenFileApiReply '{build}')) {{ throw 'Rejected complete reply' }}\n", encoding="utf-8")
            self.run_command([POWERSHELL, "-NoProfile", "-File", str(check)], root)

    def test_preserves_project_defaults(self):
        data = json.loads((ENGINE / "CMakePresets.json").read_text(encoding="utf-8"))
        configure = {p["name"]: p for p in data["configurePresets"]}
        self.assertEqual(configure["oxygen-configure-defaults"]["cacheVariables"], {
            "CMAKE_EXPORT_COMPILE_COMMANDS": "ON", "OXYGEN_USE_CCACHE": "ON",
            "CMAKE_INTERMEDIATE_DIR_STRATEGY": "SHORT",
        })
        self.assertEqual(configure["oxygen-windows-defaults"]["cacheVariables"], {"OXYGEN_PHYSICS_BACKEND": "jolt"})
        self.assertNotIn("installDir", configure["oxygen-posix-defaults"])
        self.assertEqual(configure["oxygen-posix-defaults"]["environment"]["caexcludepath"], "${sourceDir}/third_party;${sourceDir}/out")
        build = data["buildPresets"][0]
        self.assertEqual(build["jobs"], 8)
        self.assertIs(build["verbose"], False)
        tests = {p["name"]: p for p in data["testPresets"]}
        self.assertEqual(tests["oxygen-test-defaults"]["output"], {"outputOnFailure": True, "verbosity": "default"})
        self.assertEqual(tests["oxygen-test-debug-defaults"]["inherits"], "oxygen-test-defaults")
        self.assertEqual(tests["oxygen-test-debug-defaults"]["output"]["verbosity"], "verbose")

    @unittest.skipUnless(POWERSHELL, "PowerShell is required")
    def test_removed_deployment_override_is_rejected(self):
        result = subprocess.run([
            POWERSHELL, "-NoProfile", "-File", str(ENGINE / "tools/build-tree.ps1"),
            "-Help", "-DeployerFolder", "unused",
        ], cwd=ENGINE, capture_output=True, text=True, encoding="utf-8", errors="replace")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("DeployerFolder", result.stderr)

    @unittest.skipUnless(POWERSHELL, "PowerShell is required")
    def test_build_tree_generation_selection_and_failure_propagation(self):
        for tracy, asan, failing_tool, generator, clean in (
            (False, False, None, "All", True), (True, False, None, "All", True),
            (False, True, None, "All", True), (True, True, None, "All", True),
            (False, False, "conan", "All", True), (False, False, "ninja", "All", True),
            (False, False, "python", "All", True),
            (True, False, "vs", "All", True),
            (False, False, None, "Ninja", False), (True, False, None, "VisualStudio", False),
            (False, True, None, "Ninja", False), (False, False, None, "All", False),
        ):
            with self.subTest(tracy=tracy, asan=asan, failure=failing_tool), tempfile.TemporaryDirectory(prefix="oxygen-generate-") as directory:
                root = Path(directory)
                (root / "tools").mkdir()
                (root / "caller").mkdir()
                for family in ("install", "install-tracy"):
                    for config in ("Debug", "Release", "RelWithDebInfo", "Asan"):
                        folder = root / "out" / family / config
                        folder.mkdir(parents=True)
                        (folder / "sentinel").write_text("keep", encoding="utf-8")
                self.copy_build_tree_cli(root)
                (root / "profile.ini").write_text("[conf]\nuser.oxygen:sanitizer=" + ("asan" if asan else "none") + "\n", encoding="utf-8")
                wrapper = root / "invoke.ps1"
                wrapper.write_text(
                    "$global:CallLog = Join-Path $PSScriptRoot 'calls.jsonl'\n"
                    f"$global:FailTool = '{failing_tool or ''}'\n"
                    "function global:conan {\n"
                    "  @{ tool='conan'; cwd=$PWD.Path; arguments=@($args) } | ConvertTo-Json -Compress | Add-Content $global:CallLog\n"
                    "  $global:LASTEXITCODE = if ($global:FailTool -eq 'conan') { 37 } else { 0 }\n"
                    f"  if ($args[0] -eq 'profile') {{ '{{\"host\":{{\"conf\":{{\"user.oxygen:sanitizer\":\"{'asan' if asan else 'none'}\"}}}}}}' }}\n"
                    "}\n"
                    "function global:cmake {\n"
                    "  @{ tool='cmake'; cwd=$PWD.Path; arguments=@($args) } | ConvertTo-Json -Compress | Add-Content $global:CallLog\n"
                    "  $folder = Join-Path $PWD ('out/' + $args[1].Replace('oxygen-', 'build-').Replace('-default', ''))\n"
                    "  New-Item -ItemType Directory -Path $folder -Force | Out-Null\n"
                    "  $generator = if ($args[1] -match '-vs-') { 'Visual Studio 18 2026' } else { 'Ninja Multi-Config' }\n"
                    "  Set-Content (Join-Path $folder 'CMakeCache.txt') \"CMAKE_GENERATOR:INTERNAL=$generator`nCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON\"\n"
                    "  $global:LASTEXITCODE = if ($global:FailTool -and $args[1] -match ($global:FailTool + '-default$')) { 37 } else { 0 }\n"
                    "}\n"
                    "function global:python {\n"
                    "  @{ tool='python'; cwd=$PWD.Path; arguments=@($args) } | ConvertTo-Json -Compress | Add-Content $global:CallLog\n"
                    "  $global:LASTEXITCODE = if ($global:FailTool -eq 'python') { 37 } else { 0 }\n"
                    "}\n"
                    "function global:python3 { python @args }\n"
                    "& (Join-Path $PSScriptRoot 'tools/build-tree.ps1') generate profile.ini "
                    + f"-Generator {generator} "
                    + ("-Clean " if clean else "")
                    + ("-WithTracy" if tracy else "") + "\nexit $LASTEXITCODE\n",
                    encoding="utf-8",
                )
                result = subprocess.run([POWERSHELL, "-NoProfile", "-File", str(wrapper)], cwd=root / "caller", capture_output=True, text=True, encoding="utf-8", errors="replace")
                self.assertEqual(result.returncode, 37 if failing_tool else 0, result.stdout + result.stderr)
                records = [json.loads(line) for line in (root / "calls.jsonl").read_text(encoding="utf-8-sig").splitlines()]
                self.assertTrue(all(Path(record["cwd"]) == root for record in records))
                if failing_tool:
                    self.assertNotIn("=== Success ===", result.stdout)
                    self.assertEqual(records[-1]["tool"], failing_tool if failing_tool in ("conan", "python") else "cmake")
                    continue
                prefix = "oxygen-" + ("tracy-" if tracy else "") + ("asan-" if asan else "")
                trees = ["ninja", "vs"] if generator == "All" else ["ninja" if generator == "Ninja" else "vs"]
                preparations = [record for record in records if record["tool"] == "python"]
                self.assertEqual(len(preparations), int("ninja" in trees))
                if preparations:
                    variant = ("tracy-" if tracy else "") + ("asan-" if asan else "") + "ninja"
                    self.assertEqual(preparations[0]["arguments"], [str(root / ".vscode/prepare_clangd.py"),
                                     "--build-dir", str(root / f"out/build-{variant}")])
                    preparation_index = records.index(preparations[0])
                    self.assertEqual(records[preparation_index - 1]["tool"], "cmake")
                self.assertEqual([record["arguments"] for record in records if record["tool"] == "cmake"],
                                 [["--preset", prefix + tree + "-default"] for tree in trees])
                installs = [record for record in records if record["tool"] == "conan" and record["arguments"][0] == "install"]
                self.assertEqual(len(installs), len(trees) * (1 if asan else 3))
                self.assertTrue(all(f"&:with_tracy={tracy}" in call["arguments"] for call in installs))
                self.assertTrue(all("--build=missing" in call["arguments"] for call in installs))
                self.assertTrue(all("--deployer-package=oxygen/*" in call["arguments"] for call in installs))
                sdk_root = root / "out" / ("install-tracy" if tracy else "install")
                self.assertTrue(all(f"--deployer-folder={sdk_root}" in call["arguments"] for call in installs))
                self.assertTrue(all(not any("will_break_next" in arg for arg in call["arguments"]) for call in installs))
                for family in ("install", "install-tracy"):
                    for config in ("Debug", "Release", "RelWithDebInfo", "Asan"):
                        selected = clean and family == sdk_root.name and ((config == "Asan") == asan)
                        self.assertEqual((root / "out" / family / config / "sentinel").exists(), not selected)
                self.assertTrue(all(not any("with_asan=" in arg or "user.oxygen:sanitizer=" in arg
                                            for arg in call["arguments"]) for call in installs))

    def test_build_tree_resolves_an_inherited_asan_profile(self):
        with tempfile.TemporaryDirectory(prefix="oxygen-inherited-profile-") as directory:
            root = Path(directory)
            (root / "tools").mkdir()
            self.copy_build_tree_cli(root)
            (root / "profile.ini").write_text(
                f"include({(ENGINE / 'profiles/windows-msvc-asan.ini').as_posix()})\n",
                encoding="utf-8",
            )
            (root / "invoke.ps1").write_text(
                "$global:CallLog = Join-Path $PSScriptRoot 'calls.jsonl'\n"
                "function global:conan {\n"
                f"  if ($args[0] -eq 'profile') {{ & '{CONAN}' @args; return }}\n"
                "  @{ tool='conan'; arguments=@($args) } | ConvertTo-Json -Compress | Add-Content $global:CallLog\n"
                "  $global:LASTEXITCODE = 0\n}\n"
                "function global:cmake {\n"
                "  @{ tool='cmake'; arguments=@($args) } | ConvertTo-Json -Compress | Add-Content $global:CallLog\n"
                "  New-Item -ItemType Directory -Path 'out/build-asan-ninja' -Force | Out-Null\n"
                "  Set-Content 'out/build-asan-ninja/CMakeCache.txt' \"CMAKE_GENERATOR:INTERNAL=Ninja Multi-Config`nCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON\"\n"
                "  $global:LASTEXITCODE = 0\n}\n"
                "function global:python { $global:LASTEXITCODE = 0 }\n"
                "function global:python3 { python @args }\n"
                "& (Join-Path $PSScriptRoot 'tools/build-tree.ps1') generate profile.ini -Generator Ninja\n"
                "exit $LASTEXITCODE\n", encoding="utf-8",
            )
            self.run_command([POWERSHELL, "-NoProfile", "-File", str(root / "invoke.ps1")], root)
            calls = [json.loads(line) for line in (root / "calls.jsonl").read_text(encoding="utf-8-sig").splitlines()]
            self.assertEqual(len(calls), 2)
            self.assertIn("build_type=Debug", calls[0]["arguments"])
            self.assertEqual(calls[1]["arguments"], ["--preset", "oxygen-asan-ninja-default"])

    def test_native_generation_coexists_and_migrates(self):
        with tempfile.TemporaryDirectory(prefix="oxygen-presets-") as directory:
            root = Path(directory)
            recipe = ENGINE / "conanfile.py"
            # Inherit production layout/generate unchanged. Omit only external
            # packages; this test neither downloads nor compiles dependencies.
            (root / "conanfile.py").write_text(
                "import importlib.util\n"
                f"spec = importlib.util.spec_from_file_location('oxygen_recipe', {str(recipe)!r})\n"
                "module = importlib.util.module_from_spec(spec)\n"
                "spec.loader.exec_module(module)\n"
                "class PresetFixture(module.OxygenConan):\n"
                "    def requirements(self):\n"
                "        pass\n"
                "    def build_requirements(self): pass\n",
                encoding="utf-8",
            )
            (root / "VERSION").write_text("0.1.0\n", encoding="utf-8")
            (root / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.30)\nproject(Presets NONE)\n",
                encoding="utf-8",
            )
            shutil.copyfile(ENGINE / "CMakePresets.json", root / "CMakePresets.json")
            self.run_command([CMAKE, "--list-presets=all"], root)

            expected = set()
            for generator, suffix in (
                ("Ninja Multi-Config", "ninja"),
                ("Visual Studio 18 2026", "vs"),
            ):
                for tracy, asan in ((False, False), (True, False), (False, True), (True, True)):
                    variant = ("tracy-" if tracy else "") + ("asan-" if asan else "") + suffix
                    configurations = ("Debug",) if asan else ("Release", "Debug", "RelWithDebInfo")
                    for config in configurations:
                        self.install(root, generator, tracy, asan, config)
                        expected.add(f"conan-{variant}-{config.lower()}")
                        # In particular: Release alone must be a usable checkout.
                        self.assert_presets(root, expected)

            # Repeated installs must retain other configurations and other trees.
            self.install(root, "Ninja Multi-Config", False, False, "Debug")
            self.assert_presets(root, expected)
            tracy_path = root / "out/build-tracy-ninja/generators/CMakePresets.json"
            tracy_before = tracy_path.read_bytes()

            # Simulate the old shared names, including ASan's postprocessed suffix.
            old_path = root / "out/build-ninja/generators/CMakePresets.json"
            legacy = json.loads(old_path.read_text(encoding="utf-8"))
            for section in ("configurePresets", "buildPresets", "testPresets"):
                for preset in legacy[section]:
                    preset["name"] = preset["name"].replace("conan-ninja-", "conan-")
                    if "configurePreset" in preset:
                        preset["configurePreset"] = "conan-default"
            old_path.write_text(json.dumps(legacy), encoding="utf-8")
            for config in ("Debug", "Release", "RelWithDebInfo"):
                self.install(root, "Ninja Multi-Config", False, False, config)
            self.assert_presets(root, expected)
            self.assertEqual(tracy_before, tracy_path.read_bytes())

            # Migrate the previous implementation that edited Conan files.
            native = json.loads(old_path.read_text(encoding="utf-8"))
            native["version"] = 9
            native["include"] = ["${sourceDir}/CMakePresets.json"]
            native["configurePresets"][0]["inherits"] = "oxygen-windows-defaults"
            old_path.write_text(json.dumps(native), encoding="utf-8")
            for config in ("Release", "Debug", "RelWithDebInfo"):
                self.install(root, "Ninja Multi-Config", False, False, config)
            self.assert_presets(root, expected)

            # A removed optional tree must not leave inherited stubs behind.
            shutil.rmtree(root / "out/build-tracy-asan-vs")
            expected.remove("conan-tracy-asan-vs-debug")
            self.install(root, "Ninja Multi-Config", False, False, "Release")
            self.assert_presets(root, expected)

            self.assert_effective_defaults(root)

    def install(self, root, generator, tracy, asan, config, extra=(), succeeds=True):
        profile = ENGINE / "profiles" / ("windows-msvc-asan.ini" if asan else "windows-msvc.ini")
        command = [
            CONAN, "install", str(root), "--no-remote", "--build=never",
            f"--profile:host={profile}", f"--profile:build={profile}",
            "-s", f"build_type={config}",
            "-o", f"with_tracy={tracy}", "-o", f"with_asan={asan}",
            "-o", "&:tests=True",
            "-c", f"tools.cmake.cmaketoolchain:generator={generator}",
        ] + list(extra)
        if succeeds:
            return self.run_command(command, root)
        result = subprocess.run(command, cwd=root, capture_output=True, text=True,
                                encoding="utf-8", errors="replace")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def assert_presets(self, root, expected):
        user = json.loads((root / "CMakeUserPresets.json").read_text(encoding="utf-8"))
        includes = user["include"]
        self.assertEqual(len(includes), len(set(includes)))
        builds, tests, configurations = set(), set(), set()
        for included in includes:
            path = Path(included)
            if not path.is_absolute():
                path = root / path
            data = json.loads(path.read_text(encoding="utf-8"))
            configure = data["configurePresets"][0]
            name = configure["name"]
            self.assertNotIn(name, configurations)
            configurations.add(name)
            self.assertEqual(Path(configure["binaryDir"]), path.parent.parent)
            self.assertEqual(data["version"], 3)
            self.assertNotIn("include", data)
            self.assertNotIn("inherits", configure)
            for section, names in (("buildPresets", builds), ("testPresets", tests)):
                for preset in data[section]:
                    self.assertNotIn(preset["name"], names)
                    names.add(preset["name"])
                    self.assertEqual(preset["configurePreset"], name)
                    self.assertNotIn("inherits", preset)
                    if section == "buildPresets":
                        self.assertGreater(preset["jobs"], 0)
        self.assertEqual(builds, expected)
        self.assertEqual(tests, expected)
        for section in ("buildPresets", "testPresets"):
            self.assertEqual({p["name"] for p in user[section]},
                             {name.replace("conan-", "oxygen-", 1) for name in expected})
        self.run_command([CMAKE, "--list-presets=all"], root)

    def assert_effective_defaults(self, root):
        helper = ENGINE / "tools/cli/BuildSelection.ps1"
        script = root / "effective.ps1"
        script.write_text(f". '{helper}'\n" + """
$graph = Read-OxygenPresetGraph $PSScriptRoot
@{
    configure = Resolve-OxygenPreset $graph.configurePresets oxygen-ninja-default
    build = Resolve-OxygenPreset $graph.buildPresets oxygen-ninja-release
    debug = Resolve-OxygenPreset $graph.testPresets oxygen-ninja-debug
    release = Resolve-OxygenPreset $graph.testPresets oxygen-ninja-release
    candidates = @(Get-OxygenBuildCandidates $PSScriptRoot)
} | ConvertTo-Json -Depth 20
""", encoding="utf-8")
        data = json.loads(self.run_command([POWERSHELL, "-NoProfile", "-File", str(script)], root).stdout)
        self.assertEqual(data["configure"]["cacheVariables"]["OXYGEN_USE_CCACHE"], "ON")
        self.assertEqual(data["configure"]["cacheVariables"]["CMAKE_EXPORT_COMPILE_COMMANDS"], "ON")
        self.assertEqual(data["configure"]["cacheVariables"]["OXYGEN_PHYSICS_BACKEND"], "jolt")
        self.assertEqual(data["build"]["jobs"], 8)
        self.assertIs(data["build"]["verbose"], False)
        self.assertEqual(data["debug"]["output"], {"outputOnFailure": True, "verbosity": "verbose"})
        self.assertEqual(data["release"]["output"], {"outputOnFailure": True, "verbosity": "default"})
        native = json.loads((root / "out/build-ninja/generators/CMakePresets.json").read_text(encoding="utf-8"))
        native_tests = {p["configuration"]: p for p in native["testPresets"]}
        self.assertEqual(data["debug"]["execution"], native_tests["Debug"]["execution"])
        for key, value in native_tests["Release"].get("environment", {}).items():
            self.assertEqual(data["release"]["environment"][key], value)
        self.assertTrue(all(p["BuildPreset"].startswith("oxygen-") for p in data["candidates"]))
        self.assertEqual(len({(p["BuildRoot"], p["Config"]) for p in data["candidates"]}), len(data["candidates"]))
        # Let CMake itself evaluate configure inheritance, without compiling.
        self.run_command([CMAKE, "--preset", "oxygen-ninja-default"], root)
        cache = (root / "out/build-ninja/CMakeCache.txt").read_text(encoding="utf-8")
        self.assertRegex(cache, r"OXYGEN_USE_CCACHE:[^=]+=ON")
        self.assertRegex(cache, r"CMAKE_EXPORT_COMPILE_COMMANDS:[^=]+=ON")
        # Policy changes must flow through inheritance without regenerating.
        project = root / "CMakePresets.json"
        defaults = json.loads(project.read_text(encoding="utf-8"))
        defaults["buildPresets"][0].update(jobs=5, verbose=True)
        project.write_text(json.dumps(defaults), encoding="utf-8")
        changed = json.loads(self.run_command([POWERSHELL, "-NoProfile", "-File", str(script)], root).stdout)
        self.assertEqual(changed["build"]["jobs"], 5)
        self.assertIs(changed["build"]["verbose"], True)
        self.run_command([CMAKE, "--list-presets=all"], root)

    def run_command(self, command, root):
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, encoding="utf-8", errors="replace")
        self.assertEqual(result.returncode, 0, " ".join(command) + "\n" + result.stdout + result.stderr)
        return result


if __name__ == "__main__":
    unittest.main()
