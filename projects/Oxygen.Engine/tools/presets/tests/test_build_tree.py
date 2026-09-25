"""Exercise CLI mode boundaries without provisioning or compiling the engine."""

import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ENGINE = Path(__file__).resolve().parents[3]
PWSH = shutil.which("pwsh")


@unittest.skipUnless(PWSH, "PowerShell 7 required")
class BuildTreeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="oxygen build tree ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for relative in ("tools/build-tree.ps1", "tools/cli/BuildSelection.ps1", ".vscode/prepare_clangd.py"):
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ENGINE / relative, target)
        self.build = self.root / "out/build-ninja"
        self.build.mkdir(parents=True)
        (self.build / "keep.obj").write_bytes(b"existing compiled object")
        self.sdk = self.root / "out/install/Debug"
        self.sdk.mkdir(parents=True)
        (self.sdk / "keep.dll").write_bytes(b"existing SDK binary")
        (self.root / "CMakePresets.json").write_text(json.dumps({
            "version": 9,
            "configurePresets": [{"name": "oxygen-ninja-default", "generator": "Ninja Multi-Config",
                                  "binaryDir": "${sourceDir}/out/build-ninja"}],
        }), encoding="utf-8")

    def run_script(self, body, expected=0):
        script = self.root / "invoke.ps1"
        script.write_text("$ErrorActionPreference = 'Stop'\n" + body, encoding="utf-8")
        result = subprocess.run([PWSH, "-NoProfile", "-NonInteractive", "-File", str(script)],
                                cwd=self.root.parent, capture_output=True, text=True,
                                encoding="utf-8", errors="replace")
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def mock_tools(self, generator="Ninja Multi-Config", fail=""):
        return f"$global:Generator = '{generator}'\n$global:FailTool = '{fail}'\n" + """
$global:Calls = Join-Path $PSScriptRoot 'calls.jsonl'
function global:conan { throw 'Configure must not invoke Conan' }
function global:Remove-Item { throw 'Configure must not clean output' }
function global:cmake {
    @{tool='cmake'; args=@($args); cwd=$PWD.Path} | ConvertTo-Json -Compress | Add-Content $global:Calls
    $export = if ($args -contains '-DCMAKE_EXPORT_COMPILE_COMMANDS=OFF') { 'OFF' } else { 'ON' }
    Set-Content 'out/build-ninja/CMakeCache.txt' "CMAKE_GENERATOR:INTERNAL=$global:Generator`nCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=$export"
    $global:LASTEXITCODE = if ($global:FailTool -eq 'cmake') { 37 } else { 0 }
}
function global:python {
    @{tool='python'; args=@($args); cwd=$PWD.Path} | ConvertTo-Json -Compress | Add-Content $global:Calls
    $global:LASTEXITCODE = if ($global:FailTool -eq 'python') { 38 } else { 0 }
}
function global:python3 { python @args }
"""

    def calls(self):
        path = self.root / "calls.jsonl"
        return [json.loads(line) for line in path.read_text(encoding="utf-8-sig").splitlines()] if path.exists() else []

    def test_repeated_configure_never_runs_conan_or_cleans(self):
        self.run_script(self.mock_tools() + """
& "$PSScriptRoot/tools/build-tree.ps1" configure oxygen-ninja-default -Define 'OXYGEN_BUILD_TESTS=OFF','CUSTOM:STRING=a b;c'
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& "$PSScriptRoot/tools/build-tree.ps1" configure oxygen-ninja-default
exit $LASTEXITCODE
""")
        calls = self.calls()
        self.assertEqual([c["tool"] for c in calls], ["cmake", "python", "cmake", "python"])
        self.assertEqual(calls[0]["args"], ["--preset", "oxygen-ninja-default",
                                         "-DOXYGEN_BUILD_TESTS=OFF", "-DCUSTOM:STRING=a b;c"])
        self.assertEqual(calls[2]["args"], ["--preset", "oxygen-ninja-default"])
        self.assertTrue(all(Path(c["cwd"]) == self.root for c in calls))
        self.assertEqual((self.build / "keep.obj").read_bytes(), b"existing compiled object")
        self.assertEqual((self.sdk / "keep.dll").read_bytes(), b"existing SDK binary")

    def test_configure_failure_propagates_without_preparation(self):
        self.run_script(self.mock_tools(fail="cmake") + """
& "$PSScriptRoot/tools/build-tree.ps1" configure oxygen-ninja-default
exit $LASTEXITCODE
""", expected=37)
        self.assertEqual([c["tool"] for c in self.calls()], ["cmake"])

    def test_preparation_failure_propagates(self):
        result = self.run_script(self.mock_tools(fail="python") + """
& "$PSScriptRoot/tools/build-tree.ps1" configure oxygen-ninja-default
exit $LASTEXITCODE
""", expected=38)
        self.assertNotIn("=== Success ===", result.stdout)

    def test_visual_studio_does_not_prepare_clangd(self):
        self.run_script(self.mock_tools(generator="Visual Studio 18 2026") + """
& "$PSScriptRoot/tools/build-tree.ps1" configure oxygen-ninja-default
exit $LASTEXITCODE
""")
        self.assertEqual([c["tool"] for c in self.calls()], ["cmake"])

    def test_explicitly_disabled_export_is_not_read_as_stale_database(self):
        result = self.run_script(self.mock_tools() + """
& "$PSScriptRoot/tools/build-tree.ps1" configure oxygen-ninja-default -Define 'CMAKE_EXPORT_COMPILE_COMMANDS=OFF'
exit $LASTEXITCODE
""")
        self.assertEqual([c["tool"] for c in self.calls()], ["cmake"])
        self.assertIn("not refreshed", result.stdout)

    def test_invalid_options_and_missing_inputs_fail_before_tools(self):
        for arguments in (
            "configure oxygen-ninja-default -Clean",
            "configure oxygen-ninja-default -Generator Ninja",
            "configure oxygen-ninja-default -WithTracy",
            "configure oxygen-ninja-default -DependencyBuild missing",
            "generate named-profile -Define 'A=1'",
            "configure oxygen-ninja-default -Define '-B=outside'",
            "configure unknown-preset",
            "configure",
            "generate",
            "generate missing/profile.ini -Clean",
        ):
            with self.subTest(arguments=arguments):
                result = self.run_script(self.mock_tools() + f'& "$PSScriptRoot/tools/build-tree.ps1" {arguments}\nexit $LASTEXITCODE\n', expected=1)
                self.assertNotIn("=== Success ===", result.stdout)
                self.assertEqual(self.calls(), [])
                self.assertTrue((self.sdk / "keep.dll").is_file())

    def test_named_profile_and_dependency_policy_are_passed_to_conan(self):
        self.run_script(self.mock_tools() + """
function global:conan {
    @{tool='conan'; args=@($args); cwd=$PWD.Path} | ConvertTo-Json -Compress | Add-Content $global:Calls
    $global:LASTEXITCODE = 0
    if ($args[0] -eq 'profile') { '{"host":{"conf":{"user.oxygen:sanitizer":"none"}}}' }
}
& "$PSScriptRoot/tools/build-tree.ps1" generate named-profile -Generator Ninja -DependencyBuild never
exit $LASTEXITCODE
""")
        calls = [c for c in self.calls() if c["tool"] == "conan"]
        self.assertEqual(len(calls), 4)
        self.assertTrue(all("--profile:host=named-profile" in c["args"] for c in calls))
        self.assertTrue(all("--build=never" in c["args"] for c in calls[1:]))
        self.assertTrue((self.build / "keep.obj").is_file())
        self.assertTrue((self.sdk / "keep.dll").is_file())


if __name__ == "__main__":
    unittest.main()
