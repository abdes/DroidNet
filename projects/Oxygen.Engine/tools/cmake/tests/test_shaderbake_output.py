# pyright: strict
"""Reporting checks against the built ShaderBake CLI and real DXC."""

import os
import re
import shutil
import unittest

from cmake_testing import ENGINE, CommandTestCase, output, skip_unless

EXECUTABLE = os.environ.get("OXYGEN_SHADERBAKE_EXE", "")
PROGRESS = r"\[\d+/\d+\]"
# One catalog entry with a single entry point and no permutations.
LEAF = "Vortex/Stages/Translucency/ForwardMesh_VS.hlsl"


@skip_unless(EXECUTABLE, "Set OXYGEN_SHADERBAKE_EXE to the built tool")
class ShaderBakeOutputTests(CommandTestCase):
    temp_prefix = "oxygen shader output "

    def setUp(self) -> None:
        super().setUp()
        self.shaders = self.root / "shaders"
        shutil.copytree(ENGINE / "src/Oxygen/Graphics/Direct3D12/Shaders", self.shaders)
        self.archive = self.root / "shaders.bin"

    def bake(self, *extra: str, success: bool = True) -> str:
        result = self.run_command(
            [
                EXECUTABLE,
                "update",
                "--workspace-root",
                self.root,
                "--shader-root",
                self.shaders,
                "--oxygen-include-root",
                ENGINE / "src/Oxygen",
                "--build-root",
                self.root / "cache",
                "--out",
                self.archive,
                "--mode",
                "production",
                *extra,
            ],
            cwd=ENGINE,
            success=success,
            timeout=180,
        )
        return output(result)

    def append_to_leaf(self, text: str) -> None:
        with (self.shaders / LEAF).open("a", encoding="utf-8") as stream:
            stream.write(f"\n{text}\n")

    def test_quiet_update_verbose_diagnostics_and_actual_work(self) -> None:
        initial = self.bake()
        self.assertRegex(initial, PROGRESS)
        self.assertIn("Wrote ", initial)
        for detail in ("[dirty:", "Reflection:", "expanded_requests="):
            self.assertNotIn(detail, initial)
        original = self.archive.read_bytes()
        timestamp = self.archive.stat().st_mtime_ns

        self.assertEqual(self.bake().strip(), "")
        verbose = self.bake("--verbose")
        for detail in ("[clean]", "skipping repack", "expanded_requests="):
            self.assertIn(detail, verbose)
        self.assertEqual(self.archive.read_bytes(), original)
        self.assertEqual(self.archive.stat().st_mtime_ns, timestamp)

        self.append_to_leaf("// Reporting test: invalidate one real compilation request.")
        changed = self.bake()
        self.assertEqual(len(re.findall(PROGRESS, changed)), 1, changed)
        self.assertIn("ForwardMesh_VS.hlsl", changed)
        self.assertIn("Wrote ", changed)
        self.assertNotIn("[clean]", changed)
        self.assertEqual(self.bake().strip(), "")

        self.append_to_leaf("#warning shaderbake_warning_probe")
        warning = self.bake()
        self.assertIn("shaderbake_warning_probe", warning)
        self.assertIn("warning", warning.lower())

        self.archive.unlink()
        repack = self.bake()
        self.assertNotRegex(repack, PROGRESS)
        self.assertIn("Wrote ", repack)

        self.append_to_leaf("#error shaderbake_failure_probe")
        failure = self.bake(success=False)
        self.assertIn("shaderbake_failure_probe", failure)
        self.assertIn("Failed to compile", failure)


if __name__ == "__main__":
    unittest.main()
