"""Opt-in reporting checks against the built ShaderBake CLI and real DXC."""

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ENGINE = Path(__file__).resolve().parents[3]
EXECUTABLE = os.environ.get("OXYGEN_SHADERBAKE_EXE")


@unittest.skipUnless(EXECUTABLE, "Set OXYGEN_SHADERBAKE_EXE to the built tool")
class ShaderBakeOutputTests(unittest.TestCase):
    def test_quiet_update_verbose_diagnostics_and_actual_work(self):
        with tempfile.TemporaryDirectory(prefix="oxygen shader output ") as directory:
            workspace = Path(directory)
            shaders = workspace / "shaders"
            shutil.copytree(ENGINE / "src/Oxygen/Graphics/Direct3D12/Shaders", shaders)
            archive = workspace / "shaders.bin"
            arguments = [
                "update", "--workspace-root", str(workspace),
                "--shader-root", str(shaders),
                "--oxygen-include-root", str(ENGINE / "src/Oxygen"),
                "--build-root", str(workspace / "cache"),
                "--out", str(archive), "--mode", "production",
            ]

            def run(*extra, success=True):
                result = subprocess.run(
                    [EXECUTABLE, *arguments, *extra], cwd=ENGINE,
                    capture_output=True, text=True, encoding="utf-8",
                    errors="replace", timeout=180,
                )
                output = result.stdout + result.stderr
                if success:
                    self.assertEqual(result.returncode, 0, output)
                else:
                    self.assertNotEqual(result.returncode, 0, output)
                return output

            initial = run()
            self.assertRegex(initial, r"\[\d+/\d+\]")
            self.assertIn("Wrote ", initial)
            self.assertNotIn("[dirty:", initial)
            self.assertNotIn("Reflection:", initial)
            self.assertNotIn("expanded_requests=", initial)
            original = archive.read_bytes()
            timestamp = archive.stat().st_mtime_ns

            self.assertEqual(run().strip(), "")
            verbose = run("--verbose")
            self.assertIn("[clean]", verbose)
            self.assertIn("skipping repack", verbose)
            self.assertIn("expanded_requests=", verbose)
            self.assertEqual(archive.read_bytes(), original)
            self.assertEqual(archive.stat().st_mtime_ns, timestamp)

            # This catalog entry has one entry point and no permutations.
            leaf = shaders / "Vortex/Stages/Translucency/ForwardMesh_VS.hlsl"
            with leaf.open("a", encoding="utf-8") as stream:
                stream.write("\n// Reporting test: invalidate one real compilation request.\n")
            changed = run()
            self.assertEqual(len(re.findall(r"\[\d+/\d+\]", changed)), 1, changed)
            self.assertIn("ForwardMesh_VS.hlsl", changed)
            self.assertIn("Wrote ", changed)
            self.assertNotIn("[clean]", changed)
            self.assertEqual(run().strip(), "")

            with leaf.open("a", encoding="utf-8") as stream:
                stream.write("\n#warning shaderbake_warning_probe\n")
            warning = run()
            self.assertIn("shaderbake_warning_probe", warning)
            self.assertIn("warning", warning.lower())

            archive.unlink()
            repack = run()
            self.assertNotRegex(repack, r"\[\d+/\d+\]")
            self.assertIn("Wrote ", repack)

            with leaf.open("a", encoding="utf-8") as stream:
                stream.write("\n#error shaderbake_failure_probe\n")
            failure = run(success=False)
            self.assertIn("shaderbake_failure_probe", failure)
            self.assertIn("Failed to compile", failure)


if __name__ == "__main__":
    unittest.main()
