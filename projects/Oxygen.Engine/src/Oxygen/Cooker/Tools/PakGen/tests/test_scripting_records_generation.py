from __future__ import annotations

import struct
from pathlib import Path

from pakgen.api import BuildOptions, build_pak, inspect_pak
from pakgen.packing.constants import ASSET_HEADER_SIZE


def _read_script_slot_records(
    pak_path: Path, table_offset: int, count: int, entry_size: int
) -> list[tuple[bytes, int, int, int, int]]:
    data = pak_path.read_bytes()
    out: list[tuple[bytes, int, int, int, int]] = []
    for i in range(count):
        start = table_offset + i * entry_size
        rec = data[start : start + entry_size]
        key = rec[:16]
        params_off, params_count, exec_order, flags = struct.unpack_from(
            "<QIiI", rec, 16
        )
        out.append((key, params_off, params_count, exec_order, flags))
    return out


def _read_script_resource_descs(
    pak_path: Path, table_offset: int, count: int, entry_size: int
) -> list[tuple[int, int, int, int, int, int]]:
    data = pak_path.read_bytes()
    out: list[tuple[int, int, int, int, int, int]] = []
    for i in range(count):
        start = table_offset + i * entry_size
        rec = data[start : start + entry_size]
        data_offset, size_bytes, language, encoding, compression, content_hash = (
            struct.unpack_from("<QIBBBQ", rec, 0)
        )
        out.append(
            (data_offset, size_bytes, language, encoding, compression, content_hash)
        )
    return out


def _read_script_asset_desc(
    pak_path: Path, desc_offset: int
) -> tuple[str, int, int, int, str]:
    data = pak_path.read_bytes()
    desc = data[desc_offset : desc_offset + 256]
    name = desc[1:65].split(b"\x00", 1)[0].decode("utf-8", errors="strict")
    payload_offset = ASSET_HEADER_SIZE
    bytecode_index, source_index, flags = struct.unpack_from(
        "<III", desc, payload_offset
    )
    external = desc[payload_offset + 12 : payload_offset + 132].split(
        b"\x00", 1
    )[0].decode("utf-8", errors="strict")
    return (name, bytecode_index, source_index, flags, external)


def test_scripting_records_generation(tmp_path: Path):  # noqa: N802
    golden_spec = Path(__file__).parent / "_golden" / "scripting_scene_spec.yaml"
    out_pak = tmp_path / "scripting_scene.pak"
    build_pak(
        BuildOptions(
            input_spec=golden_spec,
            output_path=out_pak,
            deterministic=True,
        )
    )

    info = inspect_pak(out_pak)
    footer = info["footer"]
    script_region = footer["regions"]["script"]
    script_table = footer["tables"]["script"]
    slot_table = footer["tables"]["script_slot"]
    assert script_region["size"] > 0
    assert script_table["count"] == 3
    assert script_table["entry_size"] == 32
    assert slot_table["count"] == 3
    assert slot_table["entry_size"] == 128

    dir_entries = info.get("directory_entries", [])
    script_assets = [e for e in dir_entries if e["asset_type"] == 4]
    assert len(script_assets) == 2

    slots = _read_script_slot_records(
        out_pak,
        table_offset=slot_table["offset"],
        count=slot_table["count"],
        entry_size=slot_table["entry_size"],
    )
    # First slot has 3 params and non-zero offset.
    assert slots[0][2] == 3
    assert slots[0][1] > 0
    assert slots[0][3] == -10
    # Second slot has 4 params and non-zero offset.
    assert slots[1][2] == 4
    assert slots[1][1] > 0
    assert slots[1][3] == 0
    # Third slot has no params and zero offset.
    assert slots[2][2] == 0
    assert slots[2][1] == 0
    assert slots[2][3] == 20

    descs = _read_script_resource_descs(
        out_pak,
        table_offset=script_table["offset"],
        count=script_table["count"],
        entry_size=script_table["entry_size"],
    )
    # index 0 sentinel
    assert descs[0] == (0, 0, 0, 0, 0, 0)
    # real descriptors must have non-zero offsets/sizes/hashes
    for data_offset, size_bytes, _lang, encoding, _comp, content_hash in descs[1:]:
        assert data_offset > 0
        assert size_bytes > 0
        assert content_hash > 0
        assert encoding in (0, 1)

    by_name = {}
    for e in script_assets:
        name, bytecode_index, source_index, flags, external = _read_script_asset_desc(
            out_pak, e["desc_offset"]
        )
        by_name[name] = (bytecode_index, source_index, flags, external)

    assert set(by_name.keys()) == {"MoveScript", "AiScript"}
    move = by_name["MoveScript"]
    ai = by_name["AiScript"]
    assert move[0] > 0 and move[1] > 0
    assert ai[0] == 0 and ai[1] == 0
    # kAllowExternalSource
    assert (move[2] & 0x1) == 0x0
    assert (ai[2] & 0x1) == 0x1
    assert move[3] == ""
    assert ai[3] == "scripts/game/ai.luau"
