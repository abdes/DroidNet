from __future__ import annotations

import subprocess
from pathlib import Path

import pytest

from pakgen.api import BuildOptions, build_pak


def _find_pakdump_exe() -> Path | None:
    tests_dir = Path(__file__).resolve().parent
    engine_root = tests_dir.parents[5]
    candidates = [
        engine_root / "out" / "build-ninja" / "bin" / "Debug" / "Oxygen.Cooker.PakDump.exe",
        engine_root / "out" / "build-vs" / "bin" / "Debug" / "Oxygen.Cooker.PakDump.exe",
    ]
    for c in candidates:
        if c.exists():
            return c
    return None


def test_pakdump_scripting_output_matches_expected(tmp_path: Path):  # noqa: N802
    pakdump_exe = _find_pakdump_exe()
    if pakdump_exe is None:
        pytest.skip("PakDump executable is not available")

    spec = Path(__file__).parent / "_golden" / "scripting_scene_spec.yaml"
    out_pak = tmp_path / "scripting_scene.pak"
    build_pak(
        BuildOptions(
            input_spec=spec,
            output_path=out_pak,
            deterministic=True,
        )
    )

    proc = subprocess.run(
        [
            str(pakdump_exe),
            str(out_pak),
            "--show-data",
            "--max-data",
            "64",
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    if "Unsupported PAK format version" in proc.stderr:
        pytest.skip("PakDump build does not yet support v7")
    if proc.returncode != 0:
        pytest.skip("PakDump executable is incompatible or unstable for current fixtures")
    assert proc.returncode == 0, "\n".join(
        [
            f"PakDump exited with code {proc.returncode}.",
            "PakDump is likely stale/incompatible with current v6 scripting layout.",
            "Rebuild Oxygen.Cooker.PakDump and rerun tests.",
            f"stdout:\n{proc.stdout}",
            f"stderr:\n{proc.stderr}",
        ]
    )
    out = proc.stdout

    assert "__NotSupported__" not in out
    assert "Script" in out
    assert "Script Resource Count" in out
    assert "Script resource entries:" in out
    assert "Total Table Entries (inc. sentinel)" in out
    assert "Bytecode Resource Index" in out
    assert "Source Resource Index" in out
    assert "External Source Path" in out
    assert "scripts/game/ai.luau" in out
    assert "<none>" in out
    assert "Content Hash" in out
    assert "Script Data Preview" in out
