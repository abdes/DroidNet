"""Behavioral checks for the repository's build and test entry points."""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tooling.scripts.msbuild import query_msbuild_properties, visual_studio_tool
from tooling.scripts.traversal.runner import TraversalConfig, TraversalRunner
from tooling.scripts.traversal.task_registry import TaskInvocation, TaskRegistry
from tooling.scripts.traversal.tasks.invoke_tests import invoke_tests

ROOT = Path(__file__).resolve().parents[2]


class TestFailurePropagation(unittest.TestCase):
    def run_process(self, arguments: list[str], timeout: float = 10):
        with tempfile.TemporaryDirectory() as directory:
            project = Path(directory) / "Process.Tests.csproj"
            project.touch()
            values = {
                "IsTestProject": "true", "EnableMSTestRunner": "true",
                "TargetDir": str(Path(sys.executable).parent),
                "TargetName": Path(sys.executable).stem,
            }
            invocation = TaskInvocation("Invoke-Tests", invoke_tests)
            config = TraversalConfig(
                start_location=Path(directory), tasks=[invocation],
                forwarded_arguments={"timeout": timeout}, extra_arguments=arguments,
            )
            with patch("tooling.scripts.traversal.tasks.invoke_tests.discover_target_frameworks", return_value=["net9.0"]), \
                 patch("tooling.scripts.traversal.tasks.invoke_tests.query_msbuild_properties", return_value=(values, None)):
                return TraversalRunner(TaskRegistry()).run(config)

    @unittest.skipUnless(os.name == "nt", "Windows executable runner")
    def test_nonzero_process_fails_the_traversal(self):
        result = self.run_process(["-c", "raise SystemExit(7)"])
        self.assertFalse(result.success)
        self.assertEqual(result.failures[0].exception.returncode, 7)

    @unittest.skipUnless(os.name == "nt", "Windows executable runner")
    def test_timeout_fails_the_traversal(self):
        result = self.run_process(["-c", "import time; time.sleep(10)"], 0.1)
        self.assertFalse(result.success)
        self.assertIsInstance(result.failures[0].exception, subprocess.TimeoutExpired)


@unittest.skipUnless(os.name == "nt", "Visual Studio MSBuild required")
class BuildConfiguration(unittest.TestCase):
    def evaluate(self, path: str, **kwargs):
        values, _ = query_msbuild_properties(ROOT / path, [
            "RunAnalyzersDuringBuild", "IsPackable", "GeneratePackageOnBuild",
            "OutputPath", "OutDir", "TargetFramework", "TargetFrameworks",
            "ArtifactsPivots", "PublishDir",
        ], **kwargs)
        return values

    def test_editor_is_internal_and_build_does_not_pack_or_analyze(self):
        for configuration in ("Debug", "Release"):
            values = self.evaluate("projects/Oxygen.Editor/src/Oxygen.Editor.App.csproj", configuration=configuration)
            for key in ("IsPackable", "GeneratePackageOnBuild", "RunAnalyzersDuringBuild"):
                self.assertEqual(values[key].lower(), "false", key)
            self.assertTrue(Path(values["OutputPath"]).is_relative_to(ROOT / "artifacts"))
            self.assertTrue(Path(values["PublishDir"]).is_relative_to(ROOT / "artifacts"))
            self.assertIn("win-x64", values["ArtifactsPivots"])

    def test_reusable_library_is_packable_but_does_not_pack_on_build(self):
        values = self.evaluate("projects/Storage/src/Storage.csproj")
        self.assertEqual(values["IsPackable"].lower(), "true")
        self.assertEqual(values["GeneratePackageOnBuild"].lower(), "false")
        self.assertEqual(values["TargetFrameworks"], "")

    def test_frameworks_and_rids_have_distinct_output_directories(self):
        project = "projects/Collections/src/Collections.csproj"
        first = self.evaluate(project, target_framework="net9.0")
        windows = self.evaluate(project, target_framework="net9.0-windows10.0.26100.0")
        rid = self.evaluate(project, target_framework="net9.0", runtime_identifier="win-x64")
        self.assertEqual(len({first["OutputPath"], windows["OutputPath"], rid["OutputPath"]}), 3)

    def test_interop_uses_the_repository_artifact_root(self):
        values = self.evaluate("projects/Oxygen.Editor.Interop/src/Oxygen.Editor.Interop.vcxproj")
        self.assertEqual(Path(values["OutDir"]), ROOT / "artifacts/bin/Oxygen.Editor.Interop/Debug_net9.0")

    def test_build_and_explicit_analysis_are_separate(self):
        parent = ROOT / "artifacts/build-streamlining"
        parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=parent) as directory:
            folder = Path(directory)
            project = folder / "BuildWorkflowProbe.csproj"
            project.write_text(
                '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup>'
                '<TargetFramework>net9.0</TargetFramework><EnableDefaultCompileItems>false</EnableDefaultCompileItems>'
                '</PropertyGroup><ItemGroup><Compile Include="Probe.cs" /></ItemGroup></Project>', encoding="utf-8")
            (folder / "Probe.cs").write_text(
                'namespace BuildWorkflow; public sealed class Probe { private readonly string text = "x";'
                ' public string Read() { if (text.Length > 0) return text; return string.Empty; } }', encoding="utf-8")
            command = [str(visual_studio_tool("msbuild")), str(project), "/nologo", "/m", "/nr:false", "/v:minimal",
                       f"/p:ArtifactsPath={folder / 'outputs'}"]
            normal = subprocess.run([*command, "/restore"], capture_output=True, text=True, check=True)
            self.assertNotRegex(normal.stdout, r"warning (IDE|SA|RCS|MA|CA)\d+")
            analyzed = subprocess.run([*command, "/p:RunAnalyzersDuringBuild=true"], capture_output=True, text=True, check=True)
            self.assertIn("IDE0009", analyzed.stdout)
            self.assertIn("IDE0011", analyzed.stdout)
            self.assertNotRegex(analyzed.stdout, r"\b(SA1101|SA1503|RCS1001|RCS1003)\b")


@unittest.skipUnless(os.name == "nt", "PowerShell SDK receipt generator")
class SdkReceiptInvalidation(unittest.TestCase):
    def test_binary_and_compile_inputs_invalidate_only_their_consumers(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for part in ("bin", "include", "lib"):
                (root / part).mkdir()
            for name in ("Oxygen.Engine.dll", "Oxygen.Engine.EditorInterface.dll"):
                (root / "bin" / name).write_bytes(b"binary-v1")
            header = root / "include/Example.h"
            header.write_bytes(b"header-v1")
            (root / "lib/Example.lib").write_bytes(b"library-v1")
            receipt, compile_input = root / "receipt.h", root / "compile.h"
            command = ["powershell.exe", "-NoProfile", "-File",
                       str(ROOT / "projects/Oxygen.Editor.Interop/tools/Write-NativeSdkReceipt.ps1"),
                       "-SdkRoot", str(root), "-Configuration", "Release",
                       "-OutputHeader", str(receipt), "-CompileHeader", str(compile_input)]

            def generate():
                subprocess.run(command, capture_output=True, text=True, check=True)
                return receipt.read_bytes(), compile_input.read_bytes()

            original = generate()
            times = (receipt.stat().st_mtime_ns, compile_input.stat().st_mtime_ns)
            self.assertEqual(generate(), original)
            self.assertEqual(times, (receipt.stat().st_mtime_ns, compile_input.stat().st_mtime_ns))
            (root / "bin/Oxygen.Engine.dll").write_bytes(b"binary-v2")
            binary_changed = generate()
            self.assertNotEqual(binary_changed[0], original[0])
            self.assertEqual(binary_changed[1], original[1])
            header.write_bytes(b"header-v2")
            header_changed = generate()
            self.assertEqual(header_changed[0], binary_changed[0])
            self.assertNotEqual(header_changed[1], binary_changed[1])
            (root / "lib/Example.lib").write_bytes(b"library-v2")
            self.assertNotEqual(generate()[1], header_changed[1])


@unittest.skipUnless(os.name == "nt", "Visual Studio solution generation")
class SolutionGeneration(unittest.TestCase):
    def test_native_tests_are_included_without_building(self):
        with tempfile.TemporaryDirectory() as directory:
            solution = Path(directory) / "Interop.sln"
            subprocess.run([
                "powershell.exe", "-NoProfile", "-File", str(ROOT / "tooling/GenerateSolution.ps1"),
                "-Scope", str(ROOT / "projects/Oxygen.Editor.Interop"),
                "-SolutionPath", str(solution),
            ], cwd=directory, capture_output=True, text=True, check=True)
            content = solution.read_text(encoding="utf-8-sig")
            self.assertIn("Oxygen.Editor.Interop.NativeTests.vcxproj", content)
            self.assertIn("Debug|x64", content)
            self.assertIn("Release|x64", content)
            self.assertNotIn("Release|ARM64", content)

    def test_default_solution_name_preserves_dots(self):
        # Directory discovery intentionally excludes ignored build outputs, so use
        # a short-lived source directory. TemporaryDirectory owns its cleanup.
        with tempfile.TemporaryDirectory(prefix="SolutionProbe.", dir=ROOT / "projects") as directory:
            folder = Path(directory)
            (folder / "Probe.csproj").write_text(
                '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup>'
                '<TargetFramework>net9.0</TargetFramework></PropertyGroup></Project>', encoding="utf-8")
            subprocess.run([
                "powershell.exe", "-NoProfile", "-File", str(ROOT / "tooling/GenerateSolution.ps1"),
                "-Scope", str(folder),
            ], cwd=tempfile.gettempdir(), capture_output=True, text=True, check=True)
            self.assertTrue((folder / (folder.name + ".sln")).is_file())

    def test_help_describes_options_and_examples(self):
        for module, expected in (("get_artifacts", "--framework-all"), ("traverse", "--timeout")):
            output = subprocess.check_output(
                [sys.executable, "-m", "tooling.scripts." + module, "--help"], cwd=ROOT, text=True)
            self.assertIn(expected, output)
            self.assertIn("Examples:", output)
        output = subprocess.check_output([
            "powershell.exe", "-NoProfile", "-File", str(ROOT / "tooling/GenerateSolution.ps1"), "--help",
        ], text=True)
        self.assertIn("NoLaunch", output)
        self.assertIn("EXAMPLE", output)




if __name__ == "__main__":
    unittest.main()
