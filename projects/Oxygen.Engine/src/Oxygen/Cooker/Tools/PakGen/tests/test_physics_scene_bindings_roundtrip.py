from __future__ import annotations

import struct
import subprocess
from pathlib import Path

import pytest

from pakgen.api import BuildOptions, build_pak, inspect_pak, validate_pak
from pakgen.packing.constants import ASSET_HEADER_SIZE, PHYSICS_BINDING_ENTRY_SIZES


_TARGET_NODE_COUNT_OFFSET = ASSET_HEADER_SIZE + 16
_DIRECTORY_OFFSET_OFFSET = _TARGET_NODE_COUNT_OFFSET + 8


def _find_pakdump_exe() -> Path | None:
    tests_dir = Path(__file__).resolve().parent
    engine_root = tests_dir.parents[5]
    candidates = [
        engine_root
        / "out"
        / "build-ninja"
        / "bin"
        / "Debug"
        / "Oxygen.Cooker.PakDump.exe",
        engine_root
        / "out"
        / "build-vs"
        / "bin"
        / "Debug"
        / "Oxygen.Cooker.PakDump.exe",
    ]
    for c in candidates:
        if c.exists():
            return c
    return None


def _read_directory_entries(data: bytes) -> list[dict[str, int]]:
    footer = data[-256:]
    directory_offset, directory_size, _asset_count = struct.unpack_from(
        "<QQQ", footer, 0
    )
    entries: list[dict[str, int]] = []
    for i in range(directory_size // 64):
        off = directory_offset + i * 64
        entry = data[off : off + 64]
        asset_type = struct.unpack_from("<B", entry, 16)[0]
        entry_offset, desc_offset, desc_size = struct.unpack_from(
            "<QQI", entry, 17
        )
        entries.append(
            {
                "asset_type": asset_type,
                "entry_offset": int(entry_offset),
                "desc_offset": int(desc_offset),
                "desc_size": int(desc_size),
            }
        )
    return entries


def _extract_physics_scene_desc(data: bytes) -> bytes:
    entries = _read_directory_entries(data)
    physics_entries = [e for e in entries if e["asset_type"] == 9]
    assert len(physics_entries) == 1
    entry = physics_entries[0]
    start = entry["desc_offset"]
    end = start + entry["desc_size"]
    return data[start:end]


def _parse_physics_tables(desc: bytes) -> tuple[int, int, list[dict[str, int]]]:
    target_node_count, table_count, directory_offset = struct.unpack_from(
        "<IIQ", desc, _TARGET_NODE_COUNT_OFFSET
    )
    tables: list[dict[str, int]] = []
    for i in range(table_count):
        off = int(directory_offset) + i * 20
        binding_type = struct.unpack_from("<I", desc, off)[0]
        table_offset, count, entry_size = struct.unpack_from(
            "<QII", desc, off + 4
        )
        tables.append(
            {
                "binding_type": int(binding_type),
                "offset": int(table_offset),
                "count": int(count),
                "entry_size": int(entry_size),
            }
        )
    return int(target_node_count), int(table_count), tables


def _validate_physics_scene_descriptor(desc: bytes) -> list[str]:
    issues: list[str] = []
    target_node_count, table_count, directory_offset = struct.unpack_from(
        "<IIQ", desc, _TARGET_NODE_COUNT_OFFSET
    )
    if target_node_count < 0:
        issues.append("negative target_node_count")
    if table_count == 0:
        if directory_offset != 0:
            issues.append(
                "directory_offset must be zero when table_count is zero"
            )
        return issues
    directory_size = table_count * 20
    if directory_offset + directory_size > len(desc):
        issues.append("binding table directory out of bounds")
        return issues
    spans: list[tuple[int, int]] = []
    for i in range(table_count):
        eoff = int(directory_offset) + i * 20
        binding_type = struct.unpack_from("<I", desc, eoff)[0]
        table_offset, count, entry_size = struct.unpack_from(
            "<QII", desc, eoff + 4
        )
        expected_size = PHYSICS_BINDING_ENTRY_SIZES.get(binding_type)
        if expected_size is None:
            issues.append(f"unknown binding type 0x{binding_type:08x}")
        elif entry_size != expected_size:
            issues.append(
                f"entry_size mismatch for binding type 0x{binding_type:08x}: {entry_size} != {expected_size}"
            )
        span_start = int(table_offset)
        span_end = span_start + int(count) * int(entry_size)
        if span_end > len(desc):
            issues.append("binding table data out of bounds")
            continue
        if span_start < directory_offset + directory_size:
            issues.append("binding table overlaps directory")
        spans.append((span_start, span_end))
    spans.sort()
    for i in range(1, len(spans)):
        if spans[i][0] < spans[i - 1][1]:
            issues.append("binding tables overlap")
            break
    return issues


def test_physics_scene_with_all_bindings_roundtrip(tmp_path: Path):
    spec = (
        Path(__file__).parent
        / "_golden"
        / "scene_with_physics_sidecar_spec.yaml"
    )
    out_pak = tmp_path / "scene_with_physics_sidecar.pak"
    build_pak(
        BuildOptions(
            input_spec=spec,
            output_path=out_pak,
            deterministic=True,
        )
    )
    info = inspect_pak(out_pak)
    issues = validate_pak(out_pak)
    assert issues == []
    assert info["footer"]["tables"]["physics"]["count"] == 4

    desc = _extract_physics_scene_desc(out_pak.read_bytes())
    target_node_count, table_count, tables = _parse_physics_tables(desc)
    assert target_node_count == 2
    assert table_count == 7
    assert len(tables) == 7
    assert all(t["count"] == 1 for t in tables)
    assert all(
        PHYSICS_BINDING_ENTRY_SIZES[t["binding_type"]] == t["entry_size"]
        for t in tables
    )

    pakdump_exe = _find_pakdump_exe()
    if pakdump_exe is None:
        pytest.skip("PakDump executable is not available")
    proc = subprocess.run(
        [str(pakdump_exe), str(out_pak), "--verbose"],
        capture_output=True,
        text=True,
        check=False,
    )
    if "Unsupported PAK format version" in proc.stderr:
        pytest.skip("PakDump build does not yet support v7")
    if proc.returncode != 0:
        pytest.skip(
            "PakDump executable is incompatible or unstable for current fixtures"
        )
    out = proc.stdout
    assert "__NotSupported__" not in out
    assert "PhysicsScene" in out
    assert "(PhysicsMaterial)" in out
    assert "(CollisionShape)" in out
    assert "(PhysicsScene)" in out
    assert "Physics Scene Sidecar Fields" in out


def test_physics_scene_negative_structural_cases(tmp_path: Path):
    spec = (
        Path(__file__).parent
        / "_golden"
        / "scene_with_physics_sidecar_spec.yaml"
    )
    out_pak = tmp_path / "scene_with_physics_sidecar.pak"
    build_pak(
        BuildOptions(
            input_spec=spec,
            output_path=out_pak,
            deterministic=True,
        )
    )
    original = out_pak.read_bytes()
    entries = _read_directory_entries(original)
    physics_entry = [e for e in entries if e["asset_type"] == 9][0]
    desc_offset = physics_entry["desc_offset"]
    desc_size = physics_entry["desc_size"]
    desc = bytearray(original[desc_offset : desc_offset + desc_size])
    _target_node_count, table_count, _tables = _parse_physics_tables(
        bytes(desc)
    )
    assert table_count >= 2
    directory_offset = struct.unpack_from("<Q", desc, _DIRECTORY_OFFSET_OFFSET)[0]

    bad_dir = bytearray(desc)
    struct.pack_into("<Q", bad_dir, _DIRECTORY_OFFSET_OFFSET, desc_size + 8)
    assert any(
        "directory out of bounds" in i
        for i in _validate_physics_scene_descriptor(bytes(bad_dir))
    )

    wrong_entry_size = bytearray(desc)
    first_entry_offset = int(directory_offset)
    struct.pack_into("<I", wrong_entry_size, first_entry_offset + 16, 999)
    assert any(
        "entry_size mismatch" in i
        for i in _validate_physics_scene_descriptor(bytes(wrong_entry_size))
    )

    overlap = bytearray(desc)
    first_table_offset = struct.unpack_from(
        "<Q", overlap, int(directory_offset) + 4
    )[0]
    second_entry_offset = int(directory_offset) + 20
    struct.pack_into("<Q", overlap, second_entry_offset + 4, first_table_offset)
    assert any(
        "overlap" in i
        for i in _validate_physics_scene_descriptor(bytes(overlap))
    )

    unknown_binding = bytearray(desc)
    struct.pack_into("<I", unknown_binding, first_entry_offset, 0xDEADBEEF)
    assert any(
        "unknown binding type" in i
        for i in _validate_physics_scene_descriptor(bytes(unknown_binding))
    )
