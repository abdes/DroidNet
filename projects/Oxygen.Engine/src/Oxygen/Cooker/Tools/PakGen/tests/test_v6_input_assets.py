from __future__ import annotations

import json
import struct
from pathlib import Path

from pakgen.api import BuildOptions, build_pak, inspect_pak, plan_dry_run
from pakgen.packing.constants import ASSET_HEADER_SIZE


def _spec_with_input_assets() -> dict:
    return {
        "version": 6,
        "content_version": 1,
        "buffers": [],
        "textures": [],
        "audios": [],
        "assets": [
            {
                "type": "input_action",
                "name": "AccelerateAction",
                "asset_key": "11" * 16,
                "value_type": 2,
                "flags": 1,
            },
            {
                "type": "input_action",
                "name": "DecelerateAction",
                "asset_key": "22" * 16,
                "value_type": 2,
                "flags": 0,
            },
            {
                "type": "input_mapping_context",
                "name": "GameplayContext",
                "asset_key": "33" * 16,
                "mappings": [
                    {
                        "action": "AccelerateAction",
                        "slot_name": "Keyboard.PageUp",
                        "scale": [1.0, 1.0],
                        "bias": [0.0, 0.0],
                        "triggers": [
                            {
                                "type": "pressed",
                                "behavior": "implicit",
                                "actuation_threshold": 0.5,
                                "aux": [
                                    {
                                        "action": "DecelerateAction",
                                        "completion_states": 3,
                                        "time_to_complete_ns": 1234,
                                        "flags": 4,
                                    }
                                ],
                            }
                        ],
                    },
                    {
                        "action": "DecelerateAction",
                        "slot_name": "Keyboard.PageDown",
                        "scale": [1.0, 1.0],
                        "bias": [0.0, 0.0],
                        "triggers": [],
                    },
                ],
            },
            {
                "type": "scene",
                "name": "InputScene",
                "asset_key": "44" * 16,
                "nodes": [
                    {"name": "Root", "parent": None},
                    {"name": "Cube", "parent": 0},
                ],
            },
        ],
    }


def _write_spec(tmp_path: Path, spec: dict) -> Path:
    path = tmp_path / "spec.json"
    path.write_text(json.dumps(spec), encoding="utf-8")
    return path


def _find_directory_entries_by_type(info: dict, asset_type: int) -> list[dict]:
    return [e for e in info.get("directory_entries", []) if e["asset_type"] == asset_type]


def test_v6_input_assets_are_planned_and_written(tmp_path: Path):  # noqa: N802
    spec = _spec_with_input_assets()
    spec_path = _write_spec(tmp_path, spec)

    _plan, plan_dict = plan_dry_run(spec_path, deterministic=True)
    counts = plan_dict["statistics"]["asset_counts"]
    assert counts["input_actions"] == 2
    assert counts["input_mapping_contexts"] == 1
    assert counts["scenes"] == 1

    out_path = tmp_path / "out.pak"
    build_pak(BuildOptions(input_spec=spec_path, output_path=out_path, deterministic=True))
    info = inspect_pak(out_path)

    assert len(_find_directory_entries_by_type(info, 5)) == 2  # input_action
    assert len(_find_directory_entries_by_type(info, 6)) == 1  # input_mapping_context
    assert len(_find_directory_entries_by_type(info, 3)) == 1  # scene


def test_v6_input_mapping_layout(tmp_path: Path):  # noqa: N802
    spec = _spec_with_input_assets()
    spec_path = _write_spec(tmp_path, spec)
    out_path = tmp_path / "out_layout.pak"
    build_pak(BuildOptions(input_spec=spec_path, output_path=out_path, deterministic=True))

    info = inspect_pak(out_path)
    data = out_path.read_bytes()

    imc_entry = _find_directory_entries_by_type(info, 6)[0]

    accel_key = bytes.fromhex("11" * 16)

    # InputMappingContextAssetDesc: header, flags(4), then 4 InputDataTable entries.
    imc_desc_off = imc_entry["desc_offset"]
    imc_desc = data[imc_desc_off : imc_desc_off + 256]
    assert len(imc_desc) == 256

    base = ASSET_HEADER_SIZE + 4
    mappings_offset, mappings_count, mappings_entry_size = struct.unpack_from(
        "<QII", imc_desc, base + 0
    )
    triggers_offset, triggers_count, triggers_entry_size = struct.unpack_from(
        "<QII", imc_desc, base + 16
    )
    aux_offset, aux_count, aux_entry_size = struct.unpack_from(
        "<QII", imc_desc, base + 32
    )
    strings_offset, strings_size, strings_entry_size = struct.unpack_from(
        "<QII", imc_desc, base + 48
    )

    assert mappings_count == 2
    assert mappings_entry_size == 64
    assert triggers_count == 1
    assert triggers_entry_size == 96
    assert aux_count == 1
    assert aux_entry_size == 32
    assert strings_size > 1
    assert strings_entry_size == 1

    first_mapping_abs = imc_desc_off + mappings_offset
    first_mapping = data[first_mapping_abs : first_mapping_abs + 64]
    assert first_mapping[:16] == accel_key
    slot_name_offset = struct.unpack_from("<I", first_mapping, 16)[0]
    assert slot_name_offset > 0

    strings_abs = imc_desc_off + strings_offset
    strings_blob = data[strings_abs : strings_abs + strings_size]
    end = strings_blob.find(b"\x00", slot_name_offset)
    assert end > slot_name_offset
    assert strings_blob[slot_name_offset:end].decode("utf-8") == "Keyboard.PageUp"
