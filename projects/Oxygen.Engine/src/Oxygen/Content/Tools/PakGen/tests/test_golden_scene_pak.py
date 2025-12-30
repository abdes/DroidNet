from __future__ import annotations

from pathlib import Path

from pakgen.api import inspect_pak, validate_pak


def test_golden_scene_pak_inspects_cleanly():  # noqa: N802
    golden_dir = Path(__file__).parent / "_golden"
    pak_path = golden_dir / "scene_basic.pak"
    assert pak_path.exists(), "Golden pak missing; regenerate via PakGen CLI"

    info = inspect_pak(pak_path)
    issues = validate_pak(pak_path)
    assert not issues

    assert info["header"]["magic_ok"]
    assert info["footer"]["magic_ok"]
    assert info["footer"]["crc_match"]

    entries = info.get("directory_entries", [])
    assert len(entries) == 5
    assert [e["asset_type"] for e in entries] == [1, 2, 2, 2, 3]
    # Material stays fixed-size; geometry and scene include variable payload.
    assert entries[0]["desc_size"] == 256
    assert entries[1]["desc_size"] > 256
    assert entries[2]["desc_size"] > 256
    assert entries[3]["desc_size"] > 256
    assert entries[4]["desc_size"] > 256
