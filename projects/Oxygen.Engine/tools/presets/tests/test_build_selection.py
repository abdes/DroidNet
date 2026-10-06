"""Selection and launcher regressions; no engine build, import or launch."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ENGINE = Path(__file__).resolve().parents[3]
PWSH = shutil.which("pwsh")


@unittest.skipUnless(PWSH and shutil.which("cmake"), "PowerShell and CMake required")
class BuildSelectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="oxygen selection ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        folder = self.root / "tools/cli"
        folder.mkdir(parents=True)
        for name in ("BuildSelection.ps1", "oxy-targets.ps1", "oxybuild.ps1", "oxyrun.ps1"):
            shutil.copyfile(ENGINE / "tools/cli" / name, folder / name)
        self.data = {"version": 9, "configurePresets": [], "buildPresets": [], "testPresets": []}

    def tree(self, variant, configs=("Debug", "Release"), exists=True, disabled=False):
        root = self.root / "out" / ("build-" + variant)
        if exists:
            root.mkdir(parents=True)
        configure = "conan-" + variant + "-default"
        self.data["configurePresets"].append({
            "name": configure, "generator": "Ninja Multi-Config" if variant.endswith("ninja") else "Visual Studio 18 2026",
            "binaryDir": "${sourceDir}/out/build-" + variant,
            "cacheVariables": {"OXYGEN_WITH_ASAN": "ON" if "asan" in variant else "OFF", "OXYGEN_WITH_TRACY": "ON" if "tracy" in variant else "OFF"},
        })
        for config in configs:
            preset = {"name": "conan-" + variant + "-" + config.lower(), "configurePreset": configure, "configuration": config}
            if disabled:
                preset["condition"] = False
            self.data["buildPresets"].append(preset)
            self.data["testPresets"].append({**preset, "environment": {"OXYGEN_SELECTED_RUNTIME": variant + "-" + config}})
        return root

    def save(self):
        (self.root / "CMakePresets.json").write_text(json.dumps(self.data), encoding="utf-8")

    def command(self, body, expect=0):
        self.save()
        path = self.root / "check.ps1"
        path.write_text(
            "$ErrorActionPreference='Stop'\nSet-StrictMode -Version Latest\n"
            ". (Join-Path $PSScriptRoot 'tools/cli/BuildSelection.ps1')\n" + body,
            encoding="utf-8",
        )
        result = subprocess.run([PWSH, "-NoProfile", "-File", str(path)], cwd=self.root.parent, capture_output=True, text=True, encoding="utf-8", errors="replace")
        self.assertEqual(result.returncode, expect, result.stdout + result.stderr)
        return result.stdout

    def test_priority_and_explicit_constraints(self):
        self.tree("vs")
        self.tree("tracy-ninja")
        self.tree("asan-ninja", ("Debug",))
        self.tree("ninja")
        output = self.command("""
@(
  (Resolve-OxygenBuildSelection).BuildPreset
  (Resolve-OxygenBuildSelection -Config Debug).BuildPreset
  (Resolve-OxygenBuildSelection -BuildTree build-tracy-ninja).BuildPreset
  (Resolve-OxygenBuildSelection -Preset conan-vs-debug).BuildPreset
  (Resolve-OxygenBuildSelection -Sanitized -Config Debug).BuildPreset
) | ConvertTo-Json -Compress
try { Resolve-OxygenBuildSelection -Sanitized -Config Release; throw 'Accepted conflict' }
catch { if ($_.Exception.Message -notlike 'No available preset*') { throw } }
try { Resolve-OxygenBuildSelection -BuildTree absent; throw 'Switched explicit tree' }
catch { if ($_.Exception.Message -notlike 'No available preset*') { throw } }
""")
        self.assertEqual(json.loads(output), ["conan-ninja-release", "conan-ninja-debug", "conan-tracy-ninja-release", "conan-vs-debug", "conan-asan-ninja-debug"])

    @unittest.skipUnless(os.name == "nt", "Windows build host policy")
    def test_windows_compiler_host_policy(self):
        self.command(r"""
Assert-OxygenWindowsBuildTools -Compiler 'C:\VS\VC\Tools\MSVC\14.51\bin\Hostx64\x64\cl.exe'
foreach ($compiler in @(
    'C:\VS\VC\Tools\MSVC\14.51\bin\Hostx86\x64\cl.exe',
    'C:\VS\VC\Tools\MSVC\14.51\bin\Hostx64\x86\cl.exe'
)) {
    try { Assert-OxygenWindowsBuildTools -Compiler $compiler; throw 'Accepted forbidden compiler' }
    catch { if ($_.Exception.Message -notlike 'Oxygen requires Hostx64*') { throw } }
}
""")

    @unittest.skipUnless(os.name == "nt", "Windows build host policy")
    def test_cached_hostx86_is_rejected_before_build_invocation(self):
        build = self.tree("ninja")
        (build / "CMakeCache.txt").write_text(
            "CMAKE_GENERATOR:INTERNAL=Ninja Multi-Config\n"
            "CMAKE_CXX_COMPILER:FILEPATH=C:/VS/VC/Tools/MSVC/14.51/bin/Hostx86/x64/cl.exe\n",
            encoding="utf-8",
        )
        self.command("""
$selection = Resolve-OxygenBuildSelection
function global:cmake { throw 'Must reject compiler before building' }
try { Invoke-OxygenBuild -Targets oxygen-cooker-importtool -Selection $selection; throw 'Accepted forbidden compiler' }
catch { if ($_.Exception.Message -notlike 'Oxygen requires Hostx64*') { throw } }
""")

    def test_help_needs_no_target_presets_or_cmake(self):
        for name in ("oxybuild.ps1", "oxyrun.ps1"):
            for flag in ("-Help", "-h"):
                with self.subTest(command=name, flag=flag):
                    path = self.root / "help.ps1"
                    path.write_text(
                        "$ErrorActionPreference = 'Stop'\n"
                        "function global:cmake { throw 'Help must not call CMake' }\n"
                        f"& (Join-Path $PSScriptRoot 'tools/cli/{name}') {flag}\n",
                        encoding="utf-8",
                    )
                    result = subprocess.run([PWSH, "-NoProfile", "-File", str(path)], cwd=self.root.parent, capture_output=True, text=True, encoding="utf-8", errors="replace")
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn("-ListBuilds", result.stdout)
                    self.assertIn("-Preset", result.stdout)

    def test_build_does_not_configure_for_newer_presets_or_missing_file_api(self):
        build = self.tree("ninja")
        cache = build / "CMakeCache.txt"
        cache.write_text("CMAKE_BUILD_TYPE:STRING=Release\n", encoding="utf-8")
        os.utime(cache, (1, 1))
        self.command("""
. (Join-Path $PSScriptRoot 'tools/cli/oxy-targets.ps1')
$selection = Resolve-OxygenBuildSelection -BuildTree build-ninja -Config Release
function global:cmake {
    if ($args[0] -ne '--build') { throw 'Unexpected standalone configure' }
    ConvertTo-Json -InputObject @($args) -Compress | Add-Content (Join-Path $PSScriptRoot 'calls.jsonl')
    $global:LASTEXITCODE = 0
}
$null = Invoke-BuildForTarget probe $selection
$null = Invoke-BuildForTarget probe $selection
""")
        calls = [json.loads(line) for line in (self.root / "calls.jsonl").read_text().splitlines()]
        self.assertEqual(calls, [["--build", "--preset", "conan-ninja-release", "--target", "probe"]] * 2)

    def test_ambiguous_target_cancels_on_empty_input_or_eof(self):
        root = self.tree("ninja")
        reply = root / ".cmake/api/v1/reply"
        reply.mkdir(parents=True)
        targets = []
        for name in ("Probe.One", "Probe.Two"):
            filename = f"target-{name}.json"
            (reply / filename).write_text(json.dumps({"name": name}), encoding="utf-8")
            targets.append({"name": name, "jsonFile": filename})
        (reply / "model.json").write_text(json.dumps({"configurations": [{"name": "Debug", "targets": targets}]}), encoding="utf-8")
        (reply / "index-001.json").write_text(json.dumps({"objects": [{"kind": "codemodel", "version": {"major": 2}, "jsonFile": "model.json"}]}), encoding="utf-8")
        for response in ("$null", "''"):
            with self.subTest(response=response):
                self.command("""
. (Join-Path $PSScriptRoot 'tools/cli/oxy-targets.ps1')
$script:prompts = 0
function global:Read-Host {
    $script:prompts++
    if ($script:prompts -gt 1) { throw 'Repeated a target prompt after input ended' }
    return RESPONSE
}
$result = Resolve-TargetName Probe (Join-Path $PSScriptRoot 'out/build-ninja')
if ($null -ne $result) { throw 'Cancellation selected a build target' }
if ($script:prompts -ne 1) { throw 'Did not exercise ambiguous selection' }
""".replace("RESPONSE", response))

    def test_unconfigured_tree_requires_explicit_setup(self):
        self.tree("ninja")
        output = self.command("""
. (Join-Path $PSScriptRoot 'tools/cli/oxy-targets.ps1')
$selection = Resolve-OxygenBuildSelection -BuildTree build-ninja -Config Release
function global:cmake { throw 'Unconfigured tree must not invoke CMake' }
try { $null = Invoke-BuildForTarget probe $selection; throw 'Accepted unconfigured tree' }
catch {
    if ($_.Exception.Message -notlike 'Build tree is not configured:*') { throw }
    $_.Exception.Message
}
""")
        self.assertIn("build-tree configure", output)

    @unittest.skipUnless(shutil.which("ninja"), "Ninja required")
    def test_native_build_still_regenerates_changed_cmake_inputs(self):
        build = self.tree("ninja")
        cmakelists = self.root / "CMakeLists.txt"
        project = (
            'cmake_minimum_required(VERSION 4.2)\nproject(Regeneration NONE)\n'
            'file(WRITE "${CMAKE_BINARY_DIR}/configured.txt" "before")\n'
            'add_custom_target(probe COMMAND "${CMAKE_COMMAND}" -E true)\n'
        )
        cmakelists.write_text(project, encoding="utf-8")
        configured = subprocess.run(["cmake", "-S", str(self.root), "-B", str(build), "-G", "Ninja"],
                                    capture_output=True, text=True)
        self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
        cmakelists.write_text(project.replace('"before"', '"after"'), encoding="utf-8")
        # Make ordering deterministic even on filesystems with coarse timestamps.
        os.utime(build / "build.ninja", (1, 1))
        self.command("""
. (Join-Path $PSScriptRoot 'tools/cli/oxy-targets.ps1')
$nativeCmake = (Get-Command cmake -CommandType Application).Source
function global:cmake {
    if ($args[0] -ne '--build') { throw 'Wrapper must not configure' }
    & $nativeCmake @args
    if ($LASTEXITCODE -ne 0) { throw 'Native build failed' }
}
$selection = [pscustomobject]@{
    SourceRoot=$PSScriptRoot; BuildRoot=(Join-Path $PSScriptRoot 'out/build-ninja')
    BuildPreset=$null; Config='Release'
}
$null = Invoke-BuildForTarget probe $selection
""")
        self.assertEqual((build / "configured.txt").read_text(), "after")

    def test_missing_disabled_and_artifact_availability(self):
        self.tree("ninja", exists=False)
        self.tree("vs", disabled=True)
        tracy = self.tree("tracy-ninja")
        asan = self.tree("asan-vs", ("Debug",))
        for root, config, files in ((tracy, "Release", ["Importer.exe"]), (asan, "Debug", ["Importer.exe", "Inspector.exe"])):
            folder = root / "bin" / config
            folder.mkdir(parents=True)
            for name in files:
                (folder / name).touch()
        output = self.command("""
@(
  (Resolve-OxygenBuildSelection).BuildPreset
  (Resolve-OxygenBuildSelection -RequiredExecutables Importer.exe,Inspector.exe).BuildPreset
) | ConvertTo-Json -Compress
try { Resolve-OxygenBuildSelection -Preset conan-tracy-ninja-release -RequiredExecutables Importer.exe,Inspector.exe; throw 'Mixed tool families' }
catch { if ($_.Exception.Message -notlike 'No available preset*') { throw } }
""")
        self.assertEqual(json.loads(output), ["conan-tracy-ninja-release", "conan-asan-vs-debug"])

    def test_configuration_precedes_instrumentation_and_generator(self):
        self.tree("ninja", ("Debug",))
        self.tree("tracy-vs", ("Release",))
        self.assertEqual(self.command("(Resolve-OxygenBuildSelection).BuildPreset").strip(), "conan-tracy-vs-release")

    def artifact(self, root, configs, available):
        (root / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Release\n", encoding="utf-8")
        reply = root / ".cmake/api/v1/reply"
        reply.mkdir(parents=True)
        model = {"configurations": []}
        for config in configs:
            target_file = f"target-App-{config}.json"
            model["configurations"].append({"name": config, "targets": [{"name": "App", "jsonFile": target_file}]})
            artifact = root / "bin" / config / "app.cmd"
            if config in available:
                artifact.parent.mkdir(parents=True)
                artifact.write_bytes(b'@echo off\r\necho RUNTIME=%OXYGEN_SELECTED_RUNTIME%\r\necho ARG=%~1\r\nexit /b 23\r\n')
            (reply / target_file).write_text(json.dumps({"name": "App", "type": "EXECUTABLE", "artifacts": [{"path": artifact.relative_to(root).as_posix()}]}), encoding="utf-8")
        (reply / "model.json").write_text(json.dumps(model), encoding="utf-8")
        (reply / "index-002.json").write_text(json.dumps({"objects": [{"kind": "codemodel", "version": {"major": 2}, "jsonFile": "model.json"}]}), encoding="utf-8")
        # A stale target file must never authorize the wrong configuration.
        (reply / "target-App-Release-stale.json").write_text(json.dumps({"type": "EXECUTABLE", "artifacts": [{"path": "bin/Debug/app.cmd"}]}), encoding="utf-8")

    def test_no_build_runs_available_configuration_and_restores_environment(self):
        root = self.tree("ninja")
        self.artifact(root, ("Debug", "Release"), ("Debug",))
        output = self.command("""
$env:OXYGEN_SELECTED_RUNTIME = 'parent'
$beforeLocation = $PWD.Path
& (Join-Path $PSScriptRoot 'tools/cli/oxyrun.ps1') App -NoBuild -- 'argument with spaces'
if ($LASTEXITCODE -ne 23) { throw "Lost exit code: $LASTEXITCODE" }
if ($env:OXYGEN_SELECTED_RUNTIME -ne 'parent') { throw 'Leaked child runtime environment' }
if ($PWD.Path -ne $beforeLocation) { throw 'Leaked working directory' }
exit 0
""")
        self.assertIn("conan-ninja-debug", output)
        self.assertIn("RUNTIME=ninja-Debug", output)
        self.assertIn("ARG=argument with spaces", output)

    def test_argument_separator_preserves_child_help(self):
        root = self.tree("ninja", ("Release",))
        self.artifact(root, ("Release",), ("Release",))
        output = self.command("""
& (Join-Path $PSScriptRoot 'tools/cli/oxyrun.ps1') App -NoBuild -- -h
exit $LASTEXITCODE
""", expect=23)
        self.assertIn("ARG=-h", output)

    def test_dry_run_is_read_only(self):
        root = self.tree("ninja")
        self.artifact(root, ("Debug", "Release"), ("Release",))
        self.save()
        before = sorted(p.relative_to(root) for p in root.rglob("*") if p.is_file())
        output = self.command("""
& (Join-Path $PSScriptRoot 'tools/cli/oxyrun.ps1') App -DryRun -- 'a; b'
""")
        self.assertIn("conan-ninja-release", output)
        self.assertIn("a; b", output)
        self.assertEqual(before, sorted(p.relative_to(root) for p in root.rglob("*") if p.is_file()))

    def test_explicit_missing_artifact_does_not_run_debug(self):
        root = self.tree("ninja")
        self.artifact(root, ("Debug", "Release"), ("Debug",))
        result = self.command("""
& (Join-Path $PSScriptRoot 'tools/cli/oxyrun.ps1') App -NoBuild -Config Release
exit $LASTEXITCODE
""", expect=1)
        self.assertNotIn("RUNTIME=", result)

    def test_vs_without_file_api_uses_only_unique_matching_config_output(self):
        root = self.tree("vs")
        for config in ("Debug", "Release"):
            folder = root / "bin" / config
            folder.mkdir(parents=True)
            (folder / "Oxygen.Examples.Scene.exe").touch()
        output = self.command("""
$choice = Resolve-OxygenBuildSelection -Preset conan-vs-release
Get-OxygenExecutableArtifact $choice.BuildRoot oxygen-examples-scene $choice.Config
""")
        self.assertEqual(Path(output.strip()), root / "bin/Release/Oxygen.Examples.Scene.exe")
        (root / "bin/Release/Oxygen-Examples-Scene.exe").touch()
        output = self.command("""
$choice = Resolve-OxygenBuildSelection -Preset conan-vs-release
if ($null -ne (Get-OxygenExecutableArtifact $choice.BuildRoot oxygen-examples-scene Release)) { throw 'Ambiguous output selected' }
""")
        self.assertEqual(output, "")

    def test_build_failure_preserves_exit_code_and_never_runs(self):
        root = self.tree("ninja")
        self.artifact(root, ("Debug", "Release"), ("Release",))
        output = self.command("""
$global:NativeCMake = (Get-Command cmake -CommandType Application | Select-Object -First 1).Source
function global:cmake {
    if ($args -contains '--list-presets=build') { & $global:NativeCMake @args; return }
    if ($args -contains '--build') { Write-Host 'SIMULATED BUILD FAILURE'; $global:LASTEXITCODE = 19 }
    else { Write-Output 'SIMULATED CONFIGURE OUTPUT'; $global:LASTEXITCODE = 0 }
}
& (Join-Path $PSScriptRoot 'tools/cli/oxyrun.ps1') App
exit $LASTEXITCODE
""", expect=19)
        self.assertIn("SIMULATED BUILD FAILURE", output)
        self.assertNotIn("RUNTIME=", output)

    def test_macros_inheritance_and_environment(self):
        self.tree("ninja", ("Release",))
        leaf = self.data["configurePresets"][0]
        leaf["binaryDir"] = "${fileDir}/out/build-ninja"
        leaf["inherits"] = "base"
        self.data["configurePresets"].append({
            "name": "base", "hidden": True,
            "environment": {"OXYGEN_TEST_PREFIX": "${sourceDir}/runtime"},
        })
        self.data["testPresets"][0]["environment"]["OXYGEN_TEST_EXPANDED"] = "$env{OXYGEN_TEST_PREFIX}/bin"
        output = self.command("(Resolve-OxygenBuildSelection).RuntimeEnvironment | ConvertTo-Json -Compress")
        self.assertEqual(json.loads(output)["OXYGEN_TEST_EXPANDED"].replace("\\", "/"), str(self.root).replace("\\", "/") + "/runtime/bin")

    def test_explicit_custom_cache_and_foreign_checkout(self):
        self.tree("ninja")
        custom = self.root / "out/custom"
        custom.mkdir()
        (custom / "CMakeCache.txt").write_text(
            f"CMAKE_HOME_DIRECTORY:INTERNAL={self.root}\nCMAKE_CONFIGURATION_TYPES:STRING=Debug;Release\nCMAKE_GENERATOR:INTERNAL=Ninja Multi-Config\n",
            encoding="utf-8",
        )
        output = self.command("(Resolve-OxygenBuildSelection -BuildTree './out/custom').Config")
        self.assertEqual(output.strip(), "Release")
        (custom / "CMakeCache.txt").write_text("CMAKE_HOME_DIRECTORY:INTERNAL=C:/another-checkout\nCMAKE_BUILD_TYPE:STRING=Debug\n", encoding="utf-8")
        self.command("Resolve-OxygenBuildSelection -BuildTree custom", expect=1)

    def fake_tool(self, folder, name, exit_code=0):
        folder.mkdir(parents=True, exist_ok=True)
        path = folder / (name + ".cmd")
        path.write_bytes((
            "@echo off\r\necho RUNTIME=%OXYGEN_SELECTED_RUNTIME%\r\n"
            "echo ARG1=%~1\r\necho ARG2=%~2\r\necho ARG3=%~3\r\necho ARG4=%~4\r\n"
            f"exit /b {exit_code}\r\n"
        ).encode())
        return path

    def copy_workflow(self, name):
        source = ENGINE / name
        target = self.root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        return target

    def test_cook_workflow_uses_shared_resolution_and_global_option_order(self):
        tree = self.tree("ninja")
        self.fake_tool(tree / "bin/Release", "Oxygen.Cooker.ImportTool")
        self.copy_workflow("Examples/Content/cook_scenes.ps1")
        scene = self.root / "Examples/Content/scenes/fixture"
        scene.mkdir(parents=True)
        (scene / "import-manifest.json").write_text("{}", encoding="utf-8")
        output = self.command("""
& (Join-Path $PSScriptRoot 'Examples/Content/cook_scenes.ps1') -Scene fixture -NoTUI
exit $LASTEXITCODE
""")
        self.assertIn("conan-ninja-release", output)
        self.assertIn("RUNTIME=ninja-Release", output)
        self.assertIn("ARG1=--no-tui", output)
        self.assertIn("ARG2=batch", output)
        self.assertIn("ARG3=--manifest", output)
        self.assertIn(str(scene / "import-manifest.json"), output)

    def test_explicit_tool_and_companion_still_use_shared_invocation(self):
        tool = self.fake_tool(self.root / "explicit tools", "CustomImporter", 17)
        inspector = self.fake_tool(tool.parent, "Oxygen.Cooker.Inspector")
        output = self.command("""
function global:cmake { throw 'Explicit tool must not query presets' }
$tools = Resolve-OxygenExecutables -Targets oxygen-cooker-importtool,oxygen-cooker-inspector -Overrides @{
  'oxygen-cooker-importtool' = (Join-Path $PSScriptRoot 'explicit tools/CustomImporter.cmd')
}
$tools.Paths['oxygen-cooker-inspector']
Invoke-OxygenTool -Context $tools -Target oxygen-cooker-importtool -Arguments @('batch','argument with spaces')
if ($LASTEXITCODE -ne 17) { throw 'Lost explicit-tool exit code' }
exit 0
""")
        self.assertIn(str(inspector), output)
        self.assertIn("ARG2=argument with spaces", output)

    def test_shared_tool_set_never_mixes_configurations(self):
        tree = self.tree("ninja")
        self.fake_tool(tree / "bin/Release", "Oxygen.Cooker.ImportTool")
        for name in ("Oxygen.Cooker.ImportTool", "Oxygen.Cooker.Inspector"):
            self.fake_tool(tree / "bin/Debug", name)
        output = self.command("""
$tools = Resolve-OxygenExecutables -Targets oxygen-cooker-importtool,oxygen-cooker-inspector
$tools.Selection.BuildPreset
$tools.Paths.Values | Sort-Object
""")
        self.assertIn("conan-ninja-debug", output)
        self.assertNotIn("bin\\Release", output)

    def test_reimport_build_failure_preserves_existing_content(self):
        tree = self.tree("ninja")
        (tree / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Release\n", encoding="utf-8")
        self.fake_tool(tree / "bin/Release", "Oxygen.Cooker.ImportTool")
        self.fake_tool(tree / "bin/Release", "Oxygen.Cooker.Inspector")
        self.copy_workflow("Examples/Content/import_models.ps1")
        self.copy_workflow("Examples/Content/import-sources.schema.json")
        folder = self.root / "Examples/Content"
        cooked = self.root / "Examples/Content/.cooked"
        cooked.mkdir(parents=True)
        (cooked / "keep.bin").write_bytes(b"previous generation")
        model = self.root / "original.gltf"
        model.write_text("{}", encoding="utf-8")
        (folder / "sources.json").write_text(json.dumps({"version": 1, "sources": [{"source": str(model), "name": "fixture"}]}), encoding="utf-8")
        output = self.command("""
$global:realCmake = (Get-Command cmake -CommandType Application).Source
function global:cmake {
    if ($args[0] -eq '--build') { Write-Host 'BUILD_FAILED'; $global:LASTEXITCODE = 17; return }
    & $global:realCmake @args
}
& (Join-Path $PSScriptRoot 'Examples/Content/import_models.ps1') (Join-Path $PSScriptRoot 'Examples/Content/sources.json')
""", expect=1)
        self.assertIn("BUILD_FAILED", output)
        self.assertNotIn("ARG1=", output)
        self.assertEqual((cooked / "keep.bin").read_bytes(), b"previous generation")

    def test_build_tools_creates_missing_release_executables_before_resolution(self):
        tree = self.tree("ninja")
        (tree / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Release\n", encoding="utf-8")
        self.fake_tool(tree / "bin/Debug", "Oxygen.Cooker.ImportTool")
        self.fake_tool(tree / "bin/Debug", "Oxygen.Cooker.Inspector")
        output = self.command("""
$global:realCmake = (Get-Command cmake -CommandType Application).Source
function global:cmake {
    if ($args[0] -eq '--build') {
        if (($args -join ' ') -ne '--build --preset conan-ninja-release --target oxygen-cooker-importtool oxygen-cooker-inspector') { throw 'Wrong build/configuration' }
        $directory = Join-Path $PSScriptRoot 'out/build-ninja/bin/Release'
        [IO.Directory]::CreateDirectory($directory) | Out-Null
        foreach ($name in @('ImportTool','Inspector')) {
            [IO.File]::WriteAllText((Join-Path $directory "Oxygen.Cooker.$name.cmd"), "@echo off`r`necho FRESH_RELEASE`r`n")
        }
        $env:OXYGEN_TEST_BUILD_ENV = 'temporary'
        $global:LASTEXITCODE = 0
        return
    }
    & $global:realCmake @args
}
$tools = Build-OxygenExecutables -Targets oxygen-cooker-importtool,oxygen-cooker-inspector
if ($tools.Selection.Config -ne 'Release') { throw 'Fell back to Debug' }
if (Test-Path Env:OXYGEN_TEST_BUILD_ENV) { throw 'Leaked build environment' }
Invoke-OxygenTool -Context $tools -Target oxygen-cooker-importtool
""")
        self.assertIn("FRESH_RELEASE", output)

    def retained_record(self):
        self.copy_workflow("Examples/Content/import_models.ps1")
        self.copy_workflow("src/Oxygen/Cooker/Import/Schemas/oxygen.retained-model-import.schema.json")
        record = self.root / "Examples/Content/imports/model.import.json"
        record.parent.mkdir(parents=True)
        record.write_text(json.dumps({
            "schema_version": 1,
            "content_root": "..",
            "recipe": {"jobs": [{"type": "gltf", "source": "original.gltf"}]},
            "material_slot_provenance": {},
            "published_generation": None,
        }), encoding="utf-8")
        return record

    def test_retained_record_preflight_needs_no_tools_and_preserves_record(self):
        record = self.retained_record()
        before = record.read_bytes()
        output = self.command("""
$script = Join-Path $PSScriptRoot 'Examples/Content/import_models.ps1'
& $script -All -WhatIf
& $script -All -Reclaim -WhatIf
& $script -Record (Join-Path $PSScriptRoot 'Examples/Content/imports/model.import.json') -WhatIf
""")
        self.assertIn("Publish 1 retained model imports", output)
        self.assertIn("Reclaim unused generations for 1 retained model imports", output)
        self.assertEqual(record.read_bytes(), before)

    def test_record_replay_with_explicit_tool_propagates_failure_without_build(self):
        record = self.retained_record()
        before = record.read_bytes()
        directory = self.root / "bin/Release"
        importer = self.fake_tool(directory, "Oxygen.Cooker.ImportTool", 23)
        importer.write_bytes(b"@echo off\r\necho ARGS=%*\r\nexit /b 23\r\n")
        self.fake_tool(directory, "Oxygen.Cooker.Inspector")
        output = self.command("""
function global:cmake { throw 'Unexpected build invocation' }
& (Join-Path $PSScriptRoot 'Examples/Content/import_models.ps1') -Record (Join-Path $PSScriptRoot 'Examples/Content/imports/model.import.json') -ToolPath (Join-Path $PSScriptRoot 'bin/Release/Oxygen.Cooker.ImportTool.cmd')
""", expect=1)
        self.assertIn("--record", output)
        self.assertNotIn("--recipe", output)
        self.assertNotIn("Unexpected build invocation", output)
        self.assertEqual(record.read_bytes(), before)

    def test_packaging_propagates_shared_tool_failure(self):
        tree = self.tree("ninja")
        self.fake_tool(tree / "bin/Release", "Oxygen.Cooker.PakTool", 29)
        self.copy_workflow("Examples/Content/pak_content.ps1")
        (self.root / "Examples/Content/.cooked/main").mkdir(parents=True)
        output = self.command("""
& (Join-Path $PSScriptRoot 'Examples/Content/pak_content.ps1')
exit $LASTEXITCODE
""", expect=29)
        self.assertIn("RUNTIME=ninja-Release", output)
        self.assertIn("ARG1=build", output)
        self.assertNotIn("Published artifacts:", output)

    def test_cooker_callers_have_no_private_launch_paths(self):
        for relative in ("Examples/Content/cook_scenes.ps1", "Examples/Content/pak_content.ps1", "Examples/Content/import_models.ps1"):
            text = (ENGINE / relative).read_text(encoding="utf-8")
            self.assertIn("Build-OxygenExecutables" if "import_models" in relative else "Resolve-OxygenExecutables", text)
            self.assertIn("Invoke-OxygenTool", text)
            self.assertNotIn("Resolve-OxygenBuildSelection", text)
            self.assertNotIn("Invoke-OxygenSelectedExecutable", text)
            self.assertNotIn("& $ToolPath", text)
            self.assertNotIn("Invoke-NativeLogged", text)

    def test_all_example_scripts_have_dependency_free_help(self):
        scripts = sorted(p for p in (ENGINE / "Examples").rglob("*") if p.is_file() and p.suffix.lower() in (".ps1", ".py") and ".cooked" not in p.parts)
        self.assertTrue(scripts)
        for source in scripts:
            if source.suffix == ".ps1":
                target = self.copy_workflow(source.relative_to(ENGINE))
                for flag in ("-Help", "-h"):
                    with self.subTest(script=source.name, flag=flag):
                        wrapper = self.root / "help-example.ps1"
                        wrapper.write_text(
                            "$ErrorActionPreference = 'Stop'\n"
                            "function global:cmake { throw 'Help must not query build trees' }\n"
                            f"& '{target}' {flag}\n",
                            encoding="utf-8",
                        )
                        result = subprocess.run([PWSH, "-NoProfile", "-NonInteractive", "-File", str(wrapper)], cwd=self.root.parent, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=15)
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        self.assertIn("-Help", result.stdout)
            else:
                for flag in ("--help", "-h"):
                    with self.subTest(script=source.name, flag=flag):
                        # Help must work without importing optional third-party modules.
                        result = subprocess.run([sys.executable, "-I", str(source), flag], cwd=self.root, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=15)
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        self.assertIn("--help", result.stdout)
        self.assertFalse((self.root / "Examples/Content/pak").exists())
        self.assertFalse((self.root / "out").exists())

    def test_relative_tool_paths_logs_and_packaging_whatif(self):
        caller = self.root / "caller"
        caller.mkdir()
        (caller / "cooked").mkdir()
        (caller / "Pak.cmd").write_bytes(b'@echo off\r\necho TOOL-RAN\r\nexit /b 0\r\n')
        self.copy_workflow("Examples/Content/pak_content.ps1")
        output = self.command("""
Push-Location (Join-Path $PSScriptRoot 'caller')
try {
    $context = Resolve-OxygenExecutables -Targets Pak -Overrides @{ Pak = './Pak.cmd' }
    Invoke-OxygenTool -Context $context -Target Pak -LogPath './tool.log' -CheckExitCode
    if (-not (Test-Path -LiteralPath './tool.log')) { throw 'Log did not use caller directory' }
    if ((Get-Content -LiteralPath './tool.log') -ne 'TOOL-RAN') { throw 'Tool did not run' }
    & (Join-Path $PSScriptRoot 'Examples/Content/pak_content.ps1') -ToolPath './Pak.cmd' -CookedRoot './cooked' -OutputDir './uncreated' -WhatIf
    if (Test-Path -LiteralPath './uncreated') { throw 'WhatIf created output' }
} finally { Pop-Location }
""")
        self.assertIn(str(caller / "tool.log"), output)
        self.assertIn("What if:", output)

    def test_migrated_tool_scripts_have_dependency_free_help(self):
        scripts = [
            "tools/cli/BuildSelection.ps1", "tools/build-tree.ps1",
            "tools/RunVsmTests.ps1", "tools/run-test-exes.ps1",
            "tools/csm/Run-ConventionalShadowBaseline.ps1",
            "tools/vortex/Run-AsyncRuntimeValidation.ps1",
            "tools/vortex/Run-DeferredCoreFrame10Capture.ps1",
            "tools/vortex/Run-VortexBasicDebugViewValidation.ps1",
            "tools/vortex/Run-VortexBasicRuntimeValidation.ps1",
            "tools/vortex/Run-VortexFeatureVariantValidation.ps1",
            "tools/vortex/Run-VortexMultiViewValidation.ps1",
            "tools/vortex/Run-VortexOffscreenValidation.ps1",
            "tools/vortex/Show-ExposureVisualCheck.ps1",
        ]
        for relative in scripts:
            target = self.copy_workflow(relative)
            for flag in ("-Help", "-h"):
                with self.subTest(script=relative, flag=flag):
                    result = subprocess.run([PWSH, "-NoProfile", "-NonInteractive", "-File", str(target), flag],
                                            cwd=self.root.parent, capture_output=True, text=True,
                                            encoding="utf-8", errors="replace", timeout=15)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn("-Help", result.stdout)
        self.assertFalse((self.root / "out").exists())


if __name__ == "__main__":
    unittest.main()
