from __future__ import annotations

import struct

from pakgen.packing.packers import pack_physics_scene_asset_descriptor_and_payload


_PHYSICS_BINDING_RIGID_BODY = 0x59485052  # 'RPHY'
_PHYSICS_BINDING_COLLIDER = 0x4C4F4350  # 'PCOL'
_PHYSICS_BINDING_CHARACTER = 0x52484350  # 'PCHR'
_PHYSICS_BINDING_JOINT = 0x544E4A50  # 'PJNT'
_PHYSICS_BINDING_VEHICLE = 0x4C485650  # 'PVHL'
_NO_RESOURCE_INDEX = 0


def _header_builder(_asset: dict[str, object]) -> bytes:
    return b"\x00" * 95


def test_physics_scene_omitted_resource_indices_default_to_no_resource_index() -> None:
    asset = {
        "type": "physics_scene",
        "target_scene_key": "00000000000000000000000000000000",
        "target_node_count": 2,
        "rigid_body_bindings": [
            {
                "node_index": 0,
                "body_type": 1,
                "motion_quality": 0,
            }
        ],
        "collider_bindings": [{"node_index": 0}],
        "character_bindings": [{"node_index": 1}],
        "joint_bindings": [{"node_index_a": 0, "node_index_b": 1}],
        "vehicle_bindings": [{"node_index": 0}],
    }

    desc, payload = pack_physics_scene_asset_descriptor_and_payload(
        asset,
        header_builder=_header_builder,
        shape_name_to_asset_key={},
        physics_material_name_to_asset_key={},
        physics_resource_name_to_index={},
    )
    blob = desc + payload

    table_count = struct.unpack_from("<I", blob, 111 + 4)[0]
    directory_offset = struct.unpack_from("<Q", blob, 111 + 8)[0]
    assert table_count == 5
    assert directory_offset == 256

    table_offsets: dict[int, int] = {}
    for i in range(table_count):
        entry_off = directory_offset + i * 20
        binding_type = struct.unpack_from("<I", blob, entry_off)[0]
        table_offset = struct.unpack_from("<Q", blob, entry_off + 4)[0]
        table_offsets[binding_type] = table_offset

    rigid_off = table_offsets[_PHYSICS_BINDING_RIGID_BODY]
    collider_off = table_offsets[_PHYSICS_BINDING_COLLIDER]
    character_off = table_offsets[_PHYSICS_BINDING_CHARACTER]
    joint_off = table_offsets[_PHYSICS_BINDING_JOINT]
    vehicle_off = table_offsets[_PHYSICS_BINDING_VEHICLE]

    rigid_shape_key = blob[rigid_off + 36 : rigid_off + 52]
    rigid_material_key = blob[rigid_off + 52 : rigid_off + 68]
    collider_shape_key = blob[collider_off + 4 : collider_off + 20]
    collider_material_key = blob[collider_off + 20 : collider_off + 36]
    character_shape_key = blob[character_off + 4 : character_off + 20]
    joint_constraint = struct.unpack_from("<I", blob, joint_off + 8)[0]
    vehicle_constraint = struct.unpack_from("<I", blob, vehicle_off + 4)[0]

    assert rigid_shape_key == b"\x00" * 16
    assert rigid_material_key == b"\x00" * 16
    assert collider_shape_key == b"\x00" * 16
    assert collider_material_key == b"\x00" * 16
    assert character_shape_key == b"\x00" * 16
    assert joint_constraint == _NO_RESOURCE_INDEX
    assert vehicle_constraint == _NO_RESOURCE_INDEX
