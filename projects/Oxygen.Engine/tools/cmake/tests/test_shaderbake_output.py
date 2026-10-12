# pyright: strict
"""Reporting checks against the built ShaderBake CLI and real DXC."""

import os
import re
import shutil
import struct
import unittest
from pathlib import Path

from cmake_testing import ENGINE, CommandTestCase, output, skip_unless

EXECUTABLE = os.environ.get("OXYGEN_SHADERBAKE_EXE", "")
PROGRESS = r"\[\d+/\d+\]"
# One catalog entry with a single entry point and no permutations.
LEAF = "Vortex/Stages/Translucency/ForwardMesh_VS.hlsl"


def container_parts(blob: bytes) -> dict[bytes, bytes]:
    """Return the parts of a DXBC container, keyed by FourCC."""
    if blob[:4] != b"DXBC":
        raise ValueError("not a DXBC container")
    (count,) = struct.unpack_from("<I", blob, 28)
    parts: dict[bytes, bytes] = {}
    for index in range(count):
        (offset,) = struct.unpack_from("<I", blob, 32 + 4 * index)
        (size,) = struct.unpack_from("<I", blob, offset + 4)
        parts[blob[offset : offset + 4]] = blob[offset + 8 : offset + 8 + size]
    return parts


def recorded_pdb_name(dxil: Path) -> str:
    """Return the PDB name a DXIL records in its ILDN part."""
    ildn = container_parts(dxil.read_bytes())[b"ILDN"]
    (length,) = struct.unpack_from("<H", ildn, 2)
    return ildn[4 : 4 + length].decode("utf-8")


@skip_unless(EXECUTABLE, "Set OXYGEN_SHADERBAKE_EXE to the built tool")
class ShaderBakeOutputTests(CommandTestCase):
    temp_prefix = "oxygen shader output "

    def setUp(self) -> None:
        super().setUp()
        # Mirror the repository layout: ShaderBake tracks every include
        # relative to the workspace, so the include root must live inside it.
        self.include_root = self.root / "src/Oxygen"
        shutil.copytree(
            ENGINE / "src/Oxygen",
            self.include_root,
            ignore=lambda directory, names: [
                name
                for name in names
                if not (Path(directory) / name).is_dir()
                and Path(name).suffix not in (".hlsl", ".hlsli")
            ],
        )
        self.shaders = self.include_root / "Graphics/Direct3D12/Shaders"
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
                self.include_root,
                "--build-root",
                self.root / "cache",
                "--out",
                self.archive,
                "--config",
                "release",
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

    def test_debug_info_follows_the_profile(self) -> None:
        dxil_dir = self.root / "dxil" / LEAF
        pdb_dir = self.root / "pdb" / LEAF

        self.bake("--debug-info", "external")
        (dxil,) = list(dxil_dir.glob("*.dxil"))
        (pdb,) = list(pdb_dir.glob("*.pdb"))
        # Debuggers and crash analyzers find the sidecar by the recorded name.
        self.assertEqual(recorded_pdb_name(dxil), pdb.name)
        self.assertNotIn(b"ILDB", container_parts(dxil.read_bytes()))

        self.bake()
        self.assertEqual(list(pdb_dir.glob("*.pdb")), [])
        (dxil,) = list(dxil_dir.glob("*.dxil"))
        self.assertNotIn(b"ILDN", container_parts(dxil.read_bytes()))

        self.bake("--debug-info", "embedded")
        self.assertEqual(list(pdb_dir.glob("*.pdb")), [])
        (dxil,) = list(dxil_dir.glob("*.dxil"))
        self.assertIn(b"ILDB", container_parts(dxil.read_bytes()))

    def test_build_requires_a_config(self) -> None:
        result = self.run_command(
            [
                EXECUTABLE,
                "update",
                "--workspace-root",
                self.root,
                "--build-root",
                self.root / "cache",
                "--out",
                self.archive,
            ],
            cwd=ENGINE,
            success=False,
            timeout=60,
        )
        self.assertIn("--config is required", output(result))


if __name__ == "__main__":
    unittest.main()
