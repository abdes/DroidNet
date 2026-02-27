from __future__ import annotations

import json
import struct
from pathlib import Path

from pakgen.api import BuildOptions, build_pak, inspect_pak, plan_dry_run


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
                "input_context_bindings": [
                    {
                        "node_index": 1,
                        "context": "GameplayContext",
                        "priority": 100,
                        "activate_on_load": True,
                    },
                    {
                        "node_index": 0,
                        "context": "GameplayContext",
                        "priority": 50,
                        "flags": 2,
                    },
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


def test_v6_input_mapping_and_scene_inpt_layout(tmp_path: Path):  # noqa: N802
    spec = _spec_with_input_assets()
    spec_path = _write_spec(tmp_path, spec)
    out_path = tmp_path / "out_layout.pak"
    build_pak(BuildOptions(input_spec=spec_path, output_path=out_path, deterministic=True))

    info = inspect_pak(out_path)
    data = out_path.read_bytes()

    imc_entry = _find_directory_entries_by_type(info, 6)[0]
    scene_entry = _find_directory_entries_by_type(info, 3)[0]

    accel_key = bytes.fromhex("11" * 16)
    context_key = bytes.fromhex("33" * 16)
    inpt_component_type = 0x54504E49  # 'INPT'

    # InputMappingContextAssetDesc: header(95), flags(4), then 4 InputDataTable entries.
    imc_desc_off = imc_entry["desc_offset"]
    imc_desc = data[imc_desc_off : imc_desc_off + 256]
    assert len(imc_desc) == 256

    mappings_offset, mappings_count, mappings_entry_size = struct.unpack_from(
        "<QII", imc_desc, 99
    )
    triggers_offset, triggers_count, triggers_entry_size = struct.unpack_from(
        "<QII", imc_desc, 115
    )
    aux_offset, aux_count, aux_entry_size = struct.unpack_from("<QII", imc_desc, 131)
    strings_offset, strings_size, strings_entry_size = struct.unpack_from(
        "<QII", imc_desc, 147
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

    # SceneAssetDesc: header(95), nodes table(16), strings table(8), component dir off(8), count(4).
    scene_desc_off = scene_entry["desc_offset"]
    scene_desc = data[scene_desc_off : scene_desc_off + 256]
    component_dir_offset = struct.unpack_from("<Q", scene_desc, 119)[0]
    component_dir_count = struct.unpack_from("<I", scene_desc, 127)[0]
    assert component_dir_count > 0

    inpt_table_offset = None
    inpt_table_count = 0
    inpt_table_entry_size = 0
    for i in range(component_dir_count):
        entry_off = scene_desc_off + component_dir_offset + i * 20
        component_type = struct.unpack_from("<I", data, entry_off)[0]
        table_offset, table_count, table_entry_size = struct.unpack_from(
            "<QII", data, entry_off + 4
        )
        if component_type == inpt_component_type:
            inpt_table_offset = table_offset
            inpt_table_count = table_count
            inpt_table_entry_size = table_entry_size
            break

    assert inpt_table_offset is not None
    assert inpt_table_count == 2
    assert inpt_table_entry_size == 32

    record0 = data[
        scene_desc_off + inpt_table_offset : scene_desc_off + inpt_table_offset + 32
    ]
    record1 = data[
        scene_desc_off
        + inpt_table_offset
        + 32 : scene_desc_off
        + inpt_table_offset
        + 64
    ]

    node0, key0, priority0, flags0, _reserved0 = struct.unpack("<I16siII", record0)
    node1, key1, priority1, flags1, _reserved1 = struct.unpack("<I16siII", record1)

    # Sorted by (node_index, priority): node 0 first, then node 1.
    assert node0 == 0
    assert key0 == context_key
    assert priority0 == 50
    assert flags0 == 2

    assert node1 == 1
    assert key1 == context_key
    assert priority1 == 100
    assert (flags1 & 0x1) == 0x1  # activate_on_load
