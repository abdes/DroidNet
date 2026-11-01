"""Selection and launcher regressions; no engine build, import or launch."""

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ENGINE = Path(__file__).resolve().parents[3]
PWSH = shutil.which("pwsh") or "pwsh"
SHOWCASE = "Examples/Showcase"
RENDERSCENE = "Examples/RenderScene"
TOOLS = RENDERSCENE + "/Tools"


@unittest.skipUnless(
    shutil.which("pwsh") and shutil.which("cmake"), "PowerShell and CMake required"
)
class BuildSelectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="oxygen selection ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        folder = self.root / "tools/cli"
        folder.mkdir(parents=True)
        for name in (
            "BuildSelection.ps1",
            "oxy-targets.ps1",
            "oxybuild.ps1",
            "oxyrun.ps1",
        ):
            shutil.copyfile(ENGINE / "tools/cli" / name, folder / name)
        self.data = {
            "version": 9,
            "configurePresets": [],
            "buildPresets": [],
            "testPresets": [],
        }

    def tree(self, variant, configs=("Debug", "Release"), exists=True, disabled=False):
        root = self.root / "out" / ("build-" + variant)
        if exists:
            root.mkdir(parents=True)
        configure = "conan-" + variant + "-default"
        self.data["configurePresets"].append(
            {
                "name": configure,
                "generator": "Ninja Multi-Config"
                if variant.endswith("ninja")
                else "Visual Studio 18 2026",
                "binaryDir": "${sourceDir}/out/build-" + variant,
                "cacheVariables": {
                    "OXYGEN_WITH_ASAN": "ON" if "asan" in variant else "OFF",
                    "OXYGEN_WITH_TRACY": "ON" if "tracy" in variant else "OFF",
                },
            }
        )
        for config in configs:
            preset = {
                "name": "conan-" + variant + "-" + config.lower(),
                "configurePreset": configure,
                "configuration": config,
            }
            if disabled:
                preset["condition"] = False
            self.data["buildPresets"].append(preset)
            self.data["testPresets"].append(
                {
                    **preset,
                    "environment": {"OXYGEN_SELECTED_RUNTIME": variant + "-" + config},
                }
            )
        return root

    def save(self):
        (self.root / "CMakePresets.json").write_text(
            json.dumps(self.data), encoding="utf-8"
        )

    def command(self, body, expect=0):
        self.save()
        path = self.root / "check.ps1"
        path.write_text(
            "$ErrorActionPreference='Stop'\nSet-StrictMode -Version Latest\n"
            ". (Join-Path $PSScriptRoot 'tools/cli/BuildSelection.ps1')\n" + body,
            encoding="utf-8",
        )
        result = subprocess.run(
            [PWSH, "-NoProfile", "-File", str(path)],
            cwd=self.root.parent,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
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
try {
    Resolve-OxygenBuildSelection -Sanitized -Config Release
    throw 'Accepted conflict'
}
catch { if ($_.Exception.Message -notlike 'No available preset*') { throw } }
try { Resolve-OxygenBuildSelection -BuildTree absent; throw 'Switched explicit tree' }
catch { if ($_.Exception.Message -notlike 'No available preset*') { throw } }
""")
        self.assertEqual(
            json.loads(output),
            [
                "conan-ninja-release",
                "conan-ninja-debug",
                "conan-tracy-ninja-release",
                "conan-vs-debug",
                "conan-asan-ninja-debug",
            ],
        )

    @unittest.skipUnless(os.name == "nt", "Windows build host policy")
    def test_windows_compiler_host_policy(self):
        self.command(r"""
Assert-OxygenWindowsBuildTools -Compiler (
    'C:\VS\VC\Tools\MSVC\14.51\bin\Hostx64\x64\cl.exe'
)
foreach ($compiler in @(
    'C:\VS\VC\Tools\MSVC\14.51\bin\Hostx86\x64\cl.exe',
    'C:\VS\VC\Tools\MSVC\14.51\bin\Hostx64\x86\cl.exe'
)) {
    try {
        Assert-OxygenWindowsBuildTools -Compiler $compiler
        throw 'Accepted forbidden compiler'
    }
    catch { if ($_.Exception.Message -notlike 'Oxygen requires Hostx64*') { throw } }
}
""")

    @unittest.skipUnless(os.name == "nt", "Windows build host policy")
    def test_bare_compiler_resolves_through_path(self):
        fake = self.root / "fake-cl" / "bin" / "Hostx64" / "x64"
        fake.mkdir(parents=True)
        (fake / "cl.exe").write_bytes(b"")
        output = self.command("""
$env:PATH = (Join-Path $PSScriptRoot 'fake-cl/bin/Hostx64/x64') +
    [IO.Path]::PathSeparator + $env:PATH
Assert-OxygenWindowsBuildTools -Compiler 'cl'
Assert-OxygenWindowsBuildTools -Compiler 'cl.exe'
'RESOLVED'
""")
        self.assertIn("RESOLVED", output)

    @unittest.skipUnless(os.name == "nt", "Windows build host policy")
    def test_bare_compiler_resolving_to_hostx86_is_rejected(self):
        fake = self.root / "fake-cl" / "bin" / "Hostx86" / "x64"
        fake.mkdir(parents=True)
        (fake / "cl.exe").write_bytes(b"")
        self.command("""
$env:PATH = (Join-Path $PSScriptRoot 'fake-cl/bin/Hostx86/x64') +
    [IO.Path]::PathSeparator + $env:PATH
try {
    Assert-OxygenWindowsBuildTools -Compiler 'cl'
    throw 'Accepted forbidden compiler'
}
catch { if ($_.Exception.Message -notlike 'Oxygen requires Hostx64*') { throw } }
""")

    @unittest.skipUnless(os.name == "nt", "Windows build host policy")
    def test_cached_hostx86_is_rejected_before_build_invocation(self):
        build = self.tree("ninja")
        (build / "CMakeCache.txt").write_text(
            "CMAKE_GENERATOR:INTERNAL=Ninja Multi-Config\n"
            "CMAKE_CXX_COMPILER:FILEPATH="
            "C:/VS/VC/Tools/MSVC/14.51/bin/Hostx86/x64/cl.exe\n",
            encoding="utf-8",
        )
        self.command("""
$selection = Resolve-OxygenBuildSelection
function global:cmake { throw 'Must reject compiler before building' }
try {
    Invoke-OxygenBuild -Targets oxygen-cooker-importtool -Selection $selection
    throw 'Accepted forbidden compiler'
}
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
                    result = subprocess.run(
                        [PWSH, "-NoProfile", "-File", str(path)],
                        cwd=self.root.parent,
                        capture_output=True,
                        text=True,
                        encoding="utf-8",
                        errors="replace",
                    )
                    self.assertEqual(
                        result.returncode, 0, result.stdout + result.stderr
                    )
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
    ConvertTo-Json -InputObject @($args) -Compress |
        Add-Content (Join-Path $PSScriptRoot 'calls.jsonl')
    $global:LASTEXITCODE = 0
}
$null = Invoke-BuildForTarget probe $selection
$null = Invoke-BuildForTarget probe $selection
""")
        calls = [
            json.loads(line)
            for line in (self.root / "calls.jsonl").read_text().splitlines()
        ]
        self.assertEqual(
            calls,
            [["--build", "--preset", "conan-ninja-release", "--target", "probe"]] * 2,
        )

    def test_ambiguous_target_cancels_on_empty_input_or_eof(self):
        root = self.tree("ninja")
        reply = root / ".cmake/api/v1/reply"
        reply.mkdir(parents=True)
        targets = []
        for name in ("Probe.One", "Probe.Two"):
            filename = f"target-{name}.json"
            (reply / filename).write_text(json.dumps({"name": name}), encoding="utf-8")
            targets.append({"name": name, "jsonFile": filename})
        (reply / "model.json").write_text(
            json.dumps({"configurations": [{"name": "Debug", "targets": targets}]}),
            encoding="utf-8",
        )
        (reply / "index-001.json").write_text(
            json.dumps(
                {
                    "objects": [
                        {
                            "kind": "codemodel",
                            "version": {"major": 2},
                            "jsonFile": "model.json",
                        }
                    ]
                }
            ),
            encoding="utf-8",
        )
        for response in ("$null", "''"):
            with self.subTest(response=response):
                self.command(
                    """
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
""".replace("RESPONSE", response)
                )

    def test_unconfigured_tree_requires_explicit_setup(self):
        self.tree("ninja")
        output = self.command("""
. (Join-Path $PSScriptRoot 'tools/cli/oxy-targets.ps1')
$selection = Resolve-OxygenBuildSelection -BuildTree build-ninja -Config Release
function global:cmake { throw 'Unconfigured tree must not invoke CMake' }
try {
    $null = Invoke-BuildForTarget probe $selection
    throw 'Accepted unconfigured tree'
}
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
            "cmake_minimum_required(VERSION 4.2)\nproject(Regeneration NONE)\n"
            'file(WRITE "${CMAKE_BINARY_DIR}/configured.txt" "before")\n'
            'add_custom_target(probe COMMAND "${CMAKE_COMMAND}" -E true)\n'
        )
        cmakelists.write_text(project, encoding="utf-8")
        configured = subprocess.run(
            ["cmake", "-S", str(self.root), "-B", str(build), "-G", "Ninja"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(
            configured.returncode, 0, configured.stdout + configured.stderr
        )
        cmakelists.write_text(project.replace('"before"', '"after"'), encoding="utf-8")
        # Make ordering deterministic even on filesystems with coarse timestamps.
        os.utime(build / "build.ninja", (1, 1))
        self.command("""
. (Join-Path $PSScriptRoot 'tools/cli/oxy-targets.ps1')
$nativeCmake = (Get-Command cmake -CommandType Application |
    Select-Object -First 1).Source
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
        for root, config, files in (
            (tracy, "Release", ["Importer.exe"]),
            (asan, "Debug", ["Importer.exe", "Inspector.exe"]),
        ):
            folder = root / "bin" / config
            folder.mkdir(parents=True)
            for name in files:
                (folder / name).touch()
        output = self.command("""
$requiredExes = 'Importer.exe','Inspector.exe'
@(
  (Resolve-OxygenBuildSelection).BuildPreset
  (Resolve-OxygenBuildSelection -RequiredExecutables $requiredExes).BuildPreset
) | ConvertTo-Json -Compress
$mixedArgs = @{
    Preset = 'conan-tracy-ninja-release'
    RequiredExecutables = $requiredExes
}
try { Resolve-OxygenBuildSelection @mixedArgs; throw 'Mixed tool families' }
catch { if ($_.Exception.Message -notlike 'No available preset*') { throw } }
""")
        self.assertEqual(
            json.loads(output), ["conan-tracy-ninja-release", "conan-asan-vs-debug"]
        )

    def test_configuration_precedes_instrumentation_and_generator(self):
        self.tree("ninja", ("Debug",))
        self.tree("tracy-vs", ("Release",))
        self.assertEqual(
            self.command("(Resolve-OxygenBuildSelection).BuildPreset").strip(),
            "conan-tracy-vs-release",
        )

    def artifact(self, root, configs, available):
        (root / "CMakeCache.txt").write_text(
            "CMAKE_BUILD_TYPE:STRING=Release\n", encoding="utf-8"
        )
        reply = root / ".cmake/api/v1/reply"
        reply.mkdir(parents=True)
        model = {"configurations": []}
        for config in configs:
            target_file = f"target-App-{config}.json"
            model["configurations"].append(
                {"name": config, "targets": [{"name": "App", "jsonFile": target_file}]}
            )
            artifact = root / "bin" / config / "app.cmd"
            if config in available:
                artifact.parent.mkdir(parents=True)
                artifact.write_bytes(
                    b"@echo off\r\n"
                    b"echo RUNTIME=%OXYGEN_SELECTED_RUNTIME%\r\n"
                    b"echo ARG=%~1\r\n"
                    b"exit /b 23\r\n"
                )
            (reply / target_file).write_text(
                json.dumps(
                    {
                        "name": "App",
                        "type": "EXECUTABLE",
                        "artifacts": [{"path": artifact.relative_to(root).as_posix()}],
                    }
                ),
                encoding="utf-8",
            )
        (reply / "model.json").write_text(json.dumps(model), encoding="utf-8")
        (reply / "index-002.json").write_text(
            json.dumps(
                {
                    "objects": [
                        {
                            "kind": "codemodel",
                            "version": {"major": 2},
                            "jsonFile": "model.json",
                        }
                    ]
                }
            ),
            encoding="utf-8",
        )
        # A stale target file must never authorize the wrong configuration.
        (reply / "target-App-Release-stale.json").write_text(
            json.dumps(
                {"type": "EXECUTABLE", "artifacts": [{"path": "bin/Debug/app.cmd"}]}
            ),
            encoding="utf-8",
        )

    def test_no_build_runs_available_configuration_and_restores_environment(self):
        root = self.tree("ninja")
        self.artifact(root, ("Debug", "Release"), ("Debug",))
        output = self.command("""
$env:OXYGEN_SELECTED_RUNTIME = 'parent'
$beforeLocation = $PWD.Path
$oxyrun = Join-Path $PSScriptRoot 'tools/cli/oxyrun.ps1'
& $oxyrun App -NoBuild -- 'argument with spaces'
if ($LASTEXITCODE -ne 23) { throw "Lost exit code: $LASTEXITCODE" }
if ($env:OXYGEN_SELECTED_RUNTIME -ne 'parent') {
    throw 'Leaked child runtime environment'
}
if ($PWD.Path -ne $beforeLocation) { throw 'Leaked working directory' }
exit 0
""")
        self.assertIn("conan-ninja-debug", output)
        self.assertIn("RUNTIME=ninja-Debug", output)
        self.assertIn("ARG=argument with spaces", output)

    def test_argument_separator_preserves_child_help(self):
        root = self.tree("ninja", ("Release",))
        self.artifact(root, ("Release",), ("Release",))
        output = self.command(
            """
& (Join-Path $PSScriptRoot 'tools/cli/oxyrun.ps1') App -NoBuild -- -h
exit $LASTEXITCODE
""",
            expect=23,
        )
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
        self.assertEqual(
            before, sorted(p.relative_to(root) for p in root.rglob("*") if p.is_file())
        )

    def test_explicit_missing_artifact_does_not_run_debug(self):
        root = self.tree("ninja")
        self.artifact(root, ("Debug", "Release"), ("Debug",))
        result = self.command(
            """
& (Join-Path $PSScriptRoot 'tools/cli/oxyrun.ps1') App -NoBuild -Config Release
exit $LASTEXITCODE
""",
            expect=1,
        )
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
        self.assertEqual(
            Path(output.strip()), root / "bin/Release/Oxygen.Examples.Scene.exe"
        )
        (root / "bin/Release/Oxygen-Examples-Scene.exe").touch()
        output = self.command("""
$choice = Resolve-OxygenBuildSelection -Preset conan-vs-release
$artifact = Get-OxygenExecutableArtifact $choice.BuildRoot oxygen-examples-scene Release
if ($null -ne $artifact) { throw 'Ambiguous output selected' }
""")
        self.assertEqual(output, "")

    def test_build_failure_preserves_exit_code_and_never_runs(self):
        root = self.tree("ninja")
        self.artifact(root, ("Debug", "Release"), ("Release",))
        output = self.command(
            """
$global:NativeCMake = (Get-Command cmake -CommandType Application |
    Select-Object -First 1).Source
function global:cmake {
    if ($args -contains '--list-presets=build') { & $global:NativeCMake @args; return }
    if ($args -contains '--build') {
        Write-Host 'SIMULATED BUILD FAILURE'; $global:LASTEXITCODE = 19
    }
    else { Write-Output 'SIMULATED CONFIGURE OUTPUT'; $global:LASTEXITCODE = 0 }
}
& (Join-Path $PSScriptRoot 'tools/cli/oxyrun.ps1') App
exit $LASTEXITCODE
""",
            expect=19,
        )
        self.assertIn("SIMULATED BUILD FAILURE", output)
        self.assertNotIn("RUNTIME=", output)

    def test_macros_inheritance_and_environment(self):
        self.tree("ninja", ("Release",))
        leaf = self.data["configurePresets"][0]
        leaf["binaryDir"] = "${fileDir}/out/build-ninja"
        leaf["inherits"] = "base"
        self.data["configurePresets"].append(
            {
                "name": "base",
                "hidden": True,
                "environment": {"OXYGEN_TEST_PREFIX": "${sourceDir}/runtime"},
            }
        )
        self.data["testPresets"][0]["environment"]["OXYGEN_TEST_EXPANDED"] = (
            "$env{OXYGEN_TEST_PREFIX}/bin"
        )
        output = self.command(
            "(Resolve-OxygenBuildSelection).RuntimeEnvironment | "
            "ConvertTo-Json -Compress"
        )
        self.assertEqual(
            json.loads(output)["OXYGEN_TEST_EXPANDED"].replace("\\", "/"),
            str(self.root).replace("\\", "/") + "/runtime/bin",
        )

    def test_explicit_custom_cache_and_foreign_checkout(self):
        self.tree("ninja")
        custom = self.root / "out/custom"
        custom.mkdir()
        (custom / "CMakeCache.txt").write_text(
            f"CMAKE_HOME_DIRECTORY:INTERNAL={self.root}\n"
            "CMAKE_CONFIGURATION_TYPES:STRING=Debug;Release\n"
            "CMAKE_GENERATOR:INTERNAL=Ninja Multi-Config\n",
            encoding="utf-8",
        )
        output = self.command(
            "(Resolve-OxygenBuildSelection -BuildTree './out/custom').Config"
        )
        self.assertEqual(output.strip(), "Release")
        (custom / "CMakeCache.txt").write_text(
            "CMAKE_HOME_DIRECTORY:INTERNAL=C:/another-checkout\nCMAKE_BUILD_TYPE:STRING=Debug\n",
            encoding="utf-8",
        )
        self.command("Resolve-OxygenBuildSelection -BuildTree custom", expect=1)

    def fake_tool(self, folder, name, exit_code=0):
        folder.mkdir(parents=True, exist_ok=True)
        path = folder / (name + ".cmd")
        path.write_bytes(
            (
                "@echo off\r\necho RUNTIME=%OXYGEN_SELECTED_RUNTIME%\r\n"
                "echo ARG1=%~1\r\necho ARG2=%~2\r\necho ARG3=%~3\r\necho ARG4=%~4\r\n"
                "echo ARG5=%~5\r\necho ARG6=%~6\r\necho ARGS=%*\r\n"
                f"exit /b {exit_code}\r\n"
            ).encode()
        )
        return path

    def copy_workflow(self, name):
        source = ENGINE / name
        target = self.root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        return target

    @property
    def rs(self):
        return self.root / RENDERSCENE

    PROJECT_ID = "018f8f8f-1234-7abc-8def-0123456789ab"

    def project(self, *scripts):
        """Lays out a minimal examples project and the RenderScene tools named.

        Returns the project folder (Showcase); RenderScene is `self.rs`.
        """
        for script in scripts:
            self.copy_workflow(f"{TOOLS}/{script}")
        self.copy_workflow(f"{TOOLS}/internal/ExamplesTools.ps1")
        self.copy_workflow(f"{SHOWCASE}/Tools/examples.schema.json")
        folder = self.root / SHOWCASE
        (folder / "Content").mkdir(parents=True, exist_ok=True)
        (folder / "Project.oxy").write_text(
            json.dumps(
                {
                    "Id": self.PROJECT_ID,
                    "Name": "Fixture",
                    "SchemaVersion": 2,
                    "Mounts": [
                        {"Name": "Showcase", "Kind": "Source", "Path": "Content"}
                    ],
                }
            ),
            encoding="utf-8",
        )
        (folder / "Examples.json").write_text(
            json.dumps(
                {
                    "version": 1,
                    "examples": [
                        {
                            "name": "Fixture",
                            "description": "A fixture example.",
                            "scenes": ["Main"],
                            "tags": ["fixture"],
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )
        return folder

    def engine_pak(self, tree):
        """Records the engine content pak the build tree produces."""
        pak = self.root / "bin/Oxygen/Release/engine.pak"
        pak.parent.mkdir(parents=True, exist_ok=True)
        pak.write_bytes(b"")
        (tree / "engine-content-pak-Release.txt").write_text(str(pak), encoding="utf-8")
        return pak

    def release_cache(self, tree):
        (tree / "CMakeCache.txt").write_text(
            "CMAKE_BUILD_TYPE:STRING=Release\n", encoding="utf-8"
        )

    def cooked(self, folder):
        """Marks RenderScene's cooked root as holding a cook."""
        content = self.rs / ".cooked/Showcase"
        content.mkdir(parents=True, exist_ok=True)
        (content / "container.index.bin").write_bytes(b"")
        return content

    RECORDING_CMAKE = """
$global:realCmake = (Get-Command cmake -CommandType Application |
    Select-Object -First 1).Source
function global:cmake {
    if ($args[0] -ne '--build') { return & $global:realCmake @args }
    Write-Host "BUILD $($args -join ' ')"
    $global:LASTEXITCODE = 0
}
"""

    def test_cook_workflow_builds_importer_then_uses_shared_resolution(self):
        tree = self.tree("ninja")
        self.release_cache(tree)
        self.fake_tool(tree / "bin/Release", "Oxygen.Cooker.ImportTool")
        folder = self.project("cook.ps1")
        engine_pak = self.engine_pak(tree)
        manifest = folder / "Manifests/Fixture.import-manifest.json"
        output = self.command(
            self.RECORDING_CMAKE
            + """
& (Join-Path $PSScriptRoot 'Examples/RenderScene/Tools/cook.ps1') Fixture -NoTUI
exit $LASTEXITCODE
"""
        )
        # The selected tree builds the importer first, so a stale one is
        # rebuilt before it cooks anything.
        self.assertIn(
            "BUILD --build --preset conan-ninja-release "
            "--target oxygen-cooker-importtool oxygen-engine-content",
            output,
        )
        self.assertIn("conan-ninja-release", output)
        self.assertIn("RUNTIME=ninja-Release", output)
        # The cooked root is a global option, so it precedes the subcommand.
        self.assertIn("ARG1=--no-tui", output)
        self.assertIn("ARG2=--cooked-root", output)
        self.assertIn(f"ARG3={self.rs / '.cooked/Showcase'}", output)
        self.assertIn("ARG4=batch", output)
        self.assertIn("ARG5=--manifest", output)
        self.assertIn(f"ARG6={manifest}", output)
        self.assertIn("--cooked-context-root", output)
        self.assertIn(str(engine_pak), output)

    def test_cook_runs_one_batch_for_all_selected_examples(self):
        tree = self.tree("ninja")
        self.release_cache(tree)
        self.fake_tool(tree / "bin/Release", "Oxygen.Cooker.ImportTool")
        folder = self.project("cook.ps1")
        self.engine_pak(tree)
        catalog = folder / "Examples.json"
        data = json.loads(catalog.read_text(encoding="utf-8"))
        data["examples"].append({**data["examples"][0], "name": "Second"})
        catalog.write_text(json.dumps(data), encoding="utf-8")
        output = self.command(
            self.RECORDING_CMAKE
            + """
& (Join-Path $PSScriptRoot 'Examples/RenderScene/Tools/cook.ps1') -Tag fixture
exit $LASTEXITCODE
"""
        )
        self.assertEqual(output.count("RUNTIME="), 1)
        self.assertIn(str(folder / "Manifests/Fixture.import-manifest.json"), output)
        self.assertIn(str(folder / "Manifests/Second.import-manifest.json"), output)

    def test_cook_propagates_importer_failure(self):
        tree = self.tree("ninja")
        self.release_cache(tree)
        self.fake_tool(tree / "bin/Release", "Oxygen.Cooker.ImportTool", 31)
        self.project("cook.ps1")
        self.engine_pak(tree)
        output = self.command(
            self.RECORDING_CMAKE
            + """
& (Join-Path $PSScriptRoot 'Examples/RenderScene/Tools/cook.ps1') Fixture
exit $LASTEXITCODE
""",
            expect=31,
        )
        self.assertIn("FAILED", output)

    def check_fixture(self, exit_code=0):
        """A project whose one example has its scene and manifest, and an
        importer that answers `exit_code`. Returns the -Check script and the
        manifest."""
        folder = self.project("cook.ps1")
        scene = folder / "Content/Scenes/Fixture/Main.scene.json"
        scene.parent.mkdir(parents=True)
        scene.write_text("{}", encoding="utf-8")
        manifest = folder / "Manifests/Fixture.import-manifest.json"
        manifest.parent.mkdir()
        manifest.write_text(json.dumps({"jobs": []}), encoding="utf-8")
        tool = self.fake_tool(self.root / "importer", "ImportTool", exit_code)
        script = (
            "& (Join-Path $PSScriptRoot 'Examples/RenderScene/Tools/cook.ps1') "
            f"-Check -ToolPath '{tool}'\nexit $LASTEXITCODE\n"
        )
        return script, manifest

    def test_cook_check_analyzes_every_manifest_with_the_importer(self):
        script, manifest = self.check_fixture()
        output = self.command(script)
        # The importer's source analysis is the check: the script has no rule
        # of its own about what a manifest holds.
        self.assertIn("ARG1=--no-tui", output)
        self.assertIn("ARG2=analyze-sources", output)
        self.assertIn("ARG3=--manifest", output)
        self.assertIn(f"ARG4={manifest}", output)
        self.assertIn("ARG5=--report", output)
        self.assertIn("consistent", output)

    def test_cook_check_fails_when_the_analysis_refuses_an_example(self):
        script, _ = self.check_fixture(exit_code=1)
        output = self.command(script, expect=1)
        self.assertIn("the source analysis failed for Fixture", output)

    def test_cook_check_refuses_a_missing_scene_before_analyzing(self):
        script, _ = self.check_fixture()
        (self.root / SHOWCASE / "Content/Scenes/Fixture/Main.scene.json").unlink()
        output = self.command(script, expect=1)
        self.assertIn("Fixture: no scene", output)
        self.assertNotIn("analyze-sources", output)

    def test_cook_check_refuses_a_manifest_without_an_example(self):
        script, manifest = self.check_fixture()
        manifest.with_name("Stray.import-manifest.json").write_text(
            json.dumps({"jobs": []}), encoding="utf-8"
        )
        output = self.command(script, expect=1)
        self.assertIn("Stray.import-manifest.json has no example", output)

    def test_cook_rejects_unknown_example_before_building(self):
        self.tree("ninja")
        self.project("cook.ps1")
        output = self.command(
            """
function global:cmake { throw 'Unknown example must not build' }
try {
    & (Join-Path $PSScriptRoot 'Examples/RenderScene/Tools/cook.ps1') Absent
    throw 'Accepted unknown example'
}
catch { if ($_.Exception.Message -notlike "No example named 'Absent'*") { throw } }
& (Join-Path $PSScriptRoot 'Examples/RenderScene/Tools/cook.ps1') -List
"""
        )
        self.assertIn("Fixture", output)

    def test_explicit_tool_and_companion_still_use_shared_invocation(self):
        tool = self.fake_tool(self.root / "explicit tools", "CustomImporter", 17)
        inspector = self.fake_tool(tool.parent, "Oxygen.Cooker.Inspector")
        output = self.command("""
function global:cmake { throw 'Explicit tool must not query presets' }
$toolTargets = 'oxygen-cooker-importtool','oxygen-cooker-inspector'
$tools = Resolve-OxygenExecutables -Targets $toolTargets -Overrides @{
  'oxygen-cooker-importtool' = (
    Join-Path $PSScriptRoot 'explicit tools/CustomImporter.cmd'
  )
}
$tools.Paths['oxygen-cooker-inspector']
$toolArgs = @('batch','argument with spaces')
Invoke-OxygenTool -Context $tools -Target oxygen-cooker-importtool -Arguments $toolArgs
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
$toolTargets = 'oxygen-cooker-importtool','oxygen-cooker-inspector'
$tools = Resolve-OxygenExecutables -Targets $toolTargets
$tools.Selection.BuildPreset
$tools.Paths.Values | Sort-Object
""")
        self.assertIn("conan-ninja-debug", output)
        self.assertNotIn("bin\\Release", output)

    def test_build_tools_creates_missing_release_executables_before_resolution(self):
        tree = self.tree("ninja")
        self.release_cache(tree)
        self.fake_tool(tree / "bin/Debug", "Oxygen.Cooker.ImportTool")
        self.fake_tool(tree / "bin/Debug", "Oxygen.Cooker.Inspector")
        output = self.command("""
$global:realCmake = (Get-Command cmake -CommandType Application |
    Select-Object -First 1).Source
function global:cmake {
    if ($args[0] -eq '--build') {
        $expectedArgs = '--build --preset conan-ninja-release ' +
            '--target oxygen-cooker-importtool oxygen-cooker-inspector'
        if (($args -join ' ') -ne $expectedArgs) {
            throw 'Wrong build/configuration'
        }
        $directory = Join-Path $PSScriptRoot 'out/build-ninja/bin/Release'
        [IO.Directory]::CreateDirectory($directory) | Out-Null
        foreach ($name in @('ImportTool','Inspector')) {
            $cmdPath = Join-Path $directory "Oxygen.Cooker.$name.cmd"
            [IO.File]::WriteAllText($cmdPath, "@echo off`r`necho FRESH_RELEASE`r`n")
        }
        $env:OXYGEN_TEST_BUILD_ENV = 'temporary'
        $global:LASTEXITCODE = 0
        return
    }
    & $global:realCmake @args
}
$toolTargets = 'oxygen-cooker-importtool','oxygen-cooker-inspector'
$tools = Build-OxygenExecutables -Targets $toolTargets
if ($tools.Selection.Config -ne 'Release') { throw 'Fell back to Debug' }
if (Test-Path Env:OXYGEN_TEST_BUILD_ENV) { throw 'Leaked build environment' }
Invoke-OxygenTool -Context $tools -Target oxygen-cooker-importtool
""")
        self.assertIn("FRESH_RELEASE", output)

    def test_packaging_propagates_shared_tool_failure(self):
        tree = self.tree("ninja")
        self.release_cache(tree)
        self.fake_tool(tree / "bin/Release", "Oxygen.Cooker.PakTool", 29)
        folder = self.project("pak.ps1")
        cooked = self.cooked(folder)
        engine_pak = self.engine_pak(tree)
        output = self.command(
            self.RECORDING_CMAKE
            + """
& (Join-Path $PSScriptRoot 'Examples/RenderScene/Tools/pak.ps1')
exit $LASTEXITCODE
""",
            expect=29,
        )
        self.assertIn(
            "BUILD --build --preset conan-ninja-release "
            "--target oxygen-cooker-paktool oxygen-engine-content",
            output,
        )
        self.assertIn("RUNTIME=ninja-Release", output)
        self.assertIn("ARG1=build", output)
        self.assertIn("ARG2=--loose-source", output)
        self.assertIn(f"ARG3={cooked}", output)
        self.assertIn("ARG4=--dependency-pak", output)
        self.assertIn(f"ARG5={engine_pak}", output)
        self.assertNotIn("Published:", output)

    def test_packaging_passes_project_root_mounts_and_source_key(self):
        tree = self.tree("ninja")
        self.release_cache(tree)
        self.fake_tool(tree / "bin/Release", "Oxygen.Cooker.PakTool")
        folder = self.project("pak.ps1")
        self.cooked(folder)
        self.engine_pak(tree)
        output = self.command(
            self.RECORDING_CMAKE
            + """
& (Join-Path $PSScriptRoot 'Examples/RenderScene/Tools/pak.ps1')
exit $LASTEXITCODE
"""
        )
        self.assertIn(f'--root "{folder}"', output)
        self.assertIn(f'--mount "Showcase={folder / "Content"}"', output)
        self.assertIn(f"--source-key {self.PROJECT_ID}", output)
        self.assertIn(str(self.rs / ".build/pak/examples.pak"), output)

    def test_packaging_requires_a_cook(self):
        tree = self.tree("ninja")
        self.release_cache(tree)
        self.project("pak.ps1")
        output = self.command(
            """
function global:cmake { throw 'Nothing cooked must not build' }
& (Join-Path $PSScriptRoot 'Examples/RenderScene/Tools/pak.ps1')
""",
            expect=1,
        )
        self.assertNotIn("Nothing cooked must not build", output)

    def play_fixture(self, exit_code=0, cooked=True):
        folder = self.project("play.ps1")
        self.fake_tool(self.root / "player", "Oxygen.Player", exit_code)
        if cooked:
            scene = self.cooked(folder) / "Scenes/Fixture/Main.oscene"
            scene.parent.mkdir(parents=True)
            scene.write_bytes(b"")
        return folder

    def test_play_launches_cooked_scene_with_project_mounts_and_exit_code(self):
        folder = self.play_fixture(exit_code=7)
        output = self.command(
            """
function global:cmake { throw 'Explicit player must not query presets' }
$play = Join-Path $PSScriptRoot 'Examples/RenderScene/Tools/play.ps1'
$player = Join-Path $PSScriptRoot 'player/Oxygen.Player.cmd'
& $play Fixture -PlayerPath $player -w 1280x720
exit $LASTEXITCODE
""",
            expect=7,
        )
        self.assertIn("Scene: /Showcase/Scenes/Fixture/Main.oscene", output)
        self.assertIn("ARG1=--launch", output)
        self.assertIn("ARG3=-w", output)
        self.assertIn("ARG4=1280x720", output)
        found = re.search(r"Launch file: (.+)", output)
        self.assertIsNotNone(found)
        launch_file = Path(found.group(1).strip()) if found else Path()
        self.addCleanup(launch_file.unlink, missing_ok=True)
        launch = json.loads(launch_file.read_text(encoding="utf-8"))
        self.assertEqual(Path(launch["mounts"][0]), self.rs / ".cooked/Showcase")
        self.assertEqual(Path(launch["source_mounts"]["Showcase"]), folder / "Content")
        self.assertEqual(launch["scene"], "/Showcase/Scenes/Fixture/Main.oscene")

    def test_play_refuses_uncooked_scene_without_launching(self):
        self.play_fixture(cooked=False)
        output = self.command(
            """
$play = Join-Path $PSScriptRoot 'Examples/RenderScene/Tools/play.ps1'
$player = Join-Path $PSScriptRoot 'player/Oxygen.Player.cmd'
& $play Fixture -PlayerPath $player
exit $LASTEXITCODE
""",
            expect=1,
        )
        self.assertNotIn("RUNTIME=", output)

    def test_play_list_names_examples_and_scenes(self):
        self.play_fixture(cooked=False)
        output = self.command(
            """
function global:cmake { throw 'List must not query presets' }
& (Join-Path $PSScriptRoot 'Examples/RenderScene/Tools/play.ps1') -List
"""
        )
        self.assertRegex(output, r"Fixture\s+Main")

    def test_example_tools_have_no_private_launch_paths(self):
        forbidden = (
            "Resolve-OxygenBuildSelection",
            "Invoke-OxygenSelectedExecutable",
            "& $ToolPath",
            "Invoke-NativeLogged",
        )
        helper = (ENGINE / TOOLS / "internal/ExamplesTools.ps1").read_text(
            encoding="utf-8"
        )
        # Source checkouts build their tools so they are never stale.
        self.assertIn("Build-OxygenExecutables", helper)
        self.assertIn("Resolve-OxygenExecutables", helper)
        for text in [helper] + [
            (ENGINE / TOOLS / script).read_text(encoding="utf-8")
            for script in ("cook.ps1", "pak.ps1", "play.ps1")
        ]:
            for name in forbidden:
                self.assertNotIn(name, text)
        for script in ("cook.ps1", "pak.ps1", "play.ps1"):
            text = (ENGINE / TOOLS / script).read_text(encoding="utf-8")
            self.assertIn("Select-ExamplesTools", text, script)
            self.assertIn("Invoke-OxygenTool", text, script)

    def test_all_example_scripts_have_dependency_free_help(self):
        scripts = sorted(
            p
            for p in (ENGINE / "Examples").rglob("*")
            if p.is_file()
            and p.suffix.lower() in (".ps1", ".py")
            and ".cooked" not in p.parts
            # The shared helper is dot-sourced by the tools, never run.
            and p.name != "ExamplesTools.ps1"
            # Generators run once and take no options; only argparse scripts
            # have a help to check (running a generator with -h would run it).
            and (p.suffix.lower() == ".ps1" or "argparse" in p.read_text("utf-8"))
        )
        self.assertTrue(scripts)
        for source in scripts:
            if source.suffix == ".ps1":
                target = self.copy_workflow(source.relative_to(ENGINE))
                for flag in ("-Help", "-h"):
                    with self.subTest(script=source.name, flag=flag):
                        wrapper = self.root / "help-example.ps1"
                        wrapper.write_text(
                            "$ErrorActionPreference = 'Stop'\n"
                            "function global:cmake "
                            "{ throw 'Help must not query build trees' }\n"
                            f"& '{target}' {flag}\n",
                            encoding="utf-8",
                        )
                        result = subprocess.run(
                            [
                                PWSH,
                                "-NoProfile",
                                "-NonInteractive",
                                "-File",
                                str(wrapper),
                            ],
                            cwd=self.root.parent,
                            capture_output=True,
                            text=True,
                            encoding="utf-8",
                            errors="replace",
                            timeout=15,
                        )
                        self.assertEqual(
                            result.returncode, 0, result.stdout + result.stderr
                        )
                        self.assertIn("-Help", result.stdout)
            else:
                for flag in ("--help", "-h"):
                    with self.subTest(script=source.name, flag=flag):
                        # Help must work without importing optional third-party modules.
                        result = subprocess.run(
                            [sys.executable, "-I", str(source), flag],
                            cwd=self.root,
                            capture_output=True,
                            text=True,
                            encoding="utf-8",
                            errors="replace",
                            timeout=15,
                        )
                        self.assertEqual(
                            result.returncode, 0, result.stdout + result.stderr
                        )
                        self.assertIn("--help", result.stdout)
        for produced in (".cooked", ".build"):
            self.assertFalse((self.root / RENDERSCENE / produced).exists())
        self.assertFalse((self.root / "out").exists())

    def test_relative_tool_paths_logs_and_packaging_whatif(self):
        caller = self.root / "caller"
        caller.mkdir()
        (caller / "engine.pak").write_bytes(b"")
        (caller / "Pak.cmd").write_bytes(b"@echo off\r\necho TOOL-RAN\r\nexit /b 0\r\n")
        folder = self.project("pak.ps1")
        self.cooked(folder)
        output = self.command("""
Push-Location (Join-Path $PSScriptRoot 'caller')
try {
    $context = Resolve-OxygenExecutables -Targets Pak -Overrides @{ Pak = './Pak.cmd' }
    Invoke-OxygenTool -Context $context -Target Pak -LogPath './tool.log' -CheckExitCode
    if (-not (Test-Path -LiteralPath './tool.log')) {
        throw 'Log did not use caller directory'
    }
    if ((Get-Content -LiteralPath './tool.log') -ne 'TOOL-RAN') {
        throw 'Tool did not run'
    }
    $pak = Join-Path $PSScriptRoot 'Examples/RenderScene/Tools/pak.ps1'
    & $pak -ToolPath './Pak.cmd' -EnginePak './engine.pak' -WhatIf
} finally { Pop-Location }
""")
        self.assertIn(str(caller / "tool.log"), output)
        self.assertIn("What if:", output)
        self.assertNotIn("Published:", output)
        self.assertFalse((self.rs / ".build").exists())

    def test_migrated_tool_scripts_have_dependency_free_help(self):
        scripts = [
            "tools/cli/BuildSelection.ps1",
            "tools/build-tree.ps1",
            "tools/RunVsmTests.ps1",
            "tools/run-test-exes.ps1",
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
                    result = subprocess.run(
                        [
                            PWSH,
                            "-NoProfile",
                            "-NonInteractive",
                            "-File",
                            str(target),
                            flag,
                        ],
                        cwd=self.root.parent,
                        capture_output=True,
                        text=True,
                        encoding="utf-8",
                        errors="replace",
                        timeout=15,
                    )
                    self.assertEqual(
                        result.returncode, 0, result.stdout + result.stderr
                    )
                    self.assertIn("-Help", result.stdout)
        self.assertFalse((self.root / "out").exists())


if __name__ == "__main__":
    unittest.main()
