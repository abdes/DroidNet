import struct

import pytest

from pakgen.packing.constants import ASSET_HEADER_SIZE, COLLISION_SHAPE_ASSET_DESC_SIZE
from pakgen.packing.packers import pack_collision_shape_asset_descriptor


def _header_builder(_):
    return b"\x00" * ASSET_HEADER_SIZE


def _pack(asset, *, resource_index=17):
    desc = pack_collision_shape_asset_descriptor(
        asset,
        resource_index=resource_index,
        header_builder=_header_builder,
    )
    assert len(desc) >= COLLISION_SHAPE_ASSET_DESC_SIZE
    return desc


def _u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def _u64(buf, off):
    return struct.unpack_from("<Q", buf, off)[0]


def _f32(buf, off):
    return struct.unpack_from("<f", buf, off)[0]


def _bytes(buf, off, size):
    return bytes(buf[off : off + size])


@pytest.mark.parametrize(
    ("shape_name", "expected"),
    [
        ("invalid", 0),
        ("sphere", 1),
        ("capsule", 2),
        ("box", 3),
        ("cylinder", 4),
        ("cone", 5),
        ("convex_hull", 6),
        ("triangle_mesh", 7),
        ("height_field", 8),
        ("plane", 9),
        ("world_boundary", 10),
        ("compound", 11),
    ],
)
def test_shape_type_string_mapping(shape_name, expected):
    desc = _pack({"shape_type": shape_name})
    assert desc[ASSET_HEADER_SIZE] == expected


def test_shape_params_union_offsets_are_stable():
    shape_params_offset = ASSET_HEADER_SIZE + 1 + 12 + 16 + 12 + 4 + 8 + 8 + 16

    sphere = _pack({"shape_type": "sphere", "shape_params": {"radius": 1.25}})
    assert _f32(sphere, shape_params_offset + 0) == pytest.approx(1.25)

    capsule = _pack(
        {
            "shape_type": "capsule",
            "shape_params": {"radius": 0.5, "half_height": 3.0},
        }
    )
    assert _f32(capsule, shape_params_offset + 0) == pytest.approx(0.5)
    assert _f32(capsule, shape_params_offset + 4) == pytest.approx(3.0)

    cylinder = _pack(
        {
            "shape_type": "cylinder",
            "shape_params": {"radius": 2.5, "half_height": 4.0},
        }
    )
    assert _f32(cylinder, shape_params_offset + 0) == pytest.approx(2.5)
    assert _f32(cylinder, shape_params_offset + 4) == pytest.approx(4.0)

    cone = _pack(
        {
            "shape_type": "cone",
            "shape_params": {"radius": 1.25, "half_height": 6.5},
        }
    )
    assert _f32(cone, shape_params_offset + 0) == pytest.approx(1.25)
    assert _f32(cone, shape_params_offset + 4) == pytest.approx(6.5)

    box = _pack({"shape_type": "box", "shape_params": {"half_extents": [1, 2, 3]}})
    assert _f32(box, shape_params_offset + 0) == pytest.approx(1.0)
    assert _f32(box, shape_params_offset + 4) == pytest.approx(2.0)
    assert _f32(box, shape_params_offset + 8) == pytest.approx(3.0)

    plane = _pack(
        {
            "shape_type": "plane",
            "shape_params": {"normal": [0, 1, 0], "distance": 9.5},
        }
    )
    assert _f32(plane, shape_params_offset + 0) == pytest.approx(0.0)
    assert _f32(plane, shape_params_offset + 4) == pytest.approx(1.0)
    assert _f32(plane, shape_params_offset + 8) == pytest.approx(0.0)
    assert _f32(plane, shape_params_offset + 12) == pytest.approx(9.5)

    world_boundary = _pack(
        {
            "shape_type": "world_boundary",
            "shape_params": {
                "boundary_mode": "aabb_clamp",
                "limits_min": [-10, -20, -30],
                "limits_max": [10, 20, 30],
            },
        }
    )
    assert _u32(world_boundary, shape_params_offset + 0) == 1
    assert _f32(world_boundary, shape_params_offset + 4) == pytest.approx(-10.0)
    assert _f32(world_boundary, shape_params_offset + 8) == pytest.approx(-20.0)
    assert _f32(world_boundary, shape_params_offset + 12) == pytest.approx(-30.0)
    assert _f32(world_boundary, shape_params_offset + 16) == pytest.approx(10.0)
    assert _f32(world_boundary, shape_params_offset + 20) == pytest.approx(20.0)
    assert _f32(world_boundary, shape_params_offset + 24) == pytest.approx(30.0)


def test_common_shape_fields_offsets():
    material_asset_key = "00112233445566778899aabbccddeeff"

    desc = _pack(
        {
            "shape_type": "box",
            "local_position": [1.0, 2.0, 3.0],
            "local_rotation": [0.1, 0.2, 0.3, 0.4],
            "local_scale": [4.0, 5.0, 6.0],
            "is_sensor": True,
            "collision_own_layer": 0x0123456789ABCDEF,
            "collision_target_layers": 0xFEDCBA9876543210,
            "material_asset_key": material_asset_key,
            "shape_params": {"half_extents": [7.0, 8.0, 9.0]},
        }
    )
    base = ASSET_HEADER_SIZE
    assert _f32(desc, base + 1) == pytest.approx(1.0)
    assert _f32(desc, base + 5) == pytest.approx(2.0)
    assert _f32(desc, base + 9) == pytest.approx(3.0)
    assert _f32(desc, base + 13) == pytest.approx(0.1)
    assert _f32(desc, base + 17) == pytest.approx(0.2)
    assert _f32(desc, base + 21) == pytest.approx(0.3)
    assert _f32(desc, base + 25) == pytest.approx(0.4)
    assert _f32(desc, base + 29) == pytest.approx(4.0)
    assert _f32(desc, base + 33) == pytest.approx(5.0)
    assert _f32(desc, base + 37) == pytest.approx(6.0)
    assert _u32(desc, base + 41) == 1
    assert _u64(desc, base + 45) == 0x0123456789ABCDEF
    assert _u64(desc, base + 53) == 0xFEDCBA9876543210
    assert _bytes(desc, base + 61, 16) == bytes.fromhex(material_asset_key)
    assert _f32(desc, base + 77) == pytest.approx(7.0)
    assert _f32(desc, base + 81) == pytest.approx(8.0)
    assert _f32(desc, base + 85) == pytest.approx(9.0)


def test_cooked_shape_ref_defaults_match_shape_type():
    shape_params_offset = ASSET_HEADER_SIZE + 1 + 12 + 16 + 12 + 4 + 8 + 8 + 16
    cooked_ref_offset = shape_params_offset + 80

    # Non-payload-backed shape defaults to invalid cooked ref.
    box = _pack({"shape_type": "box"}, resource_index=99)
    assert _u32(box, cooked_ref_offset + 0) == 0
    assert box[cooked_ref_offset + 4] == 0

    # Payload-backed shape defaults to provided resource index and payload type.
    cone = _pack({"shape_type": "cone"}, resource_index=99)
    assert _u32(cone, cooked_ref_offset + 0) == 99
    assert cone[cooked_ref_offset + 4] == 1

    hull = _pack({"shape_type": "convex_hull"}, resource_index=99)
    assert _u32(hull, cooked_ref_offset + 0) == 99
    assert hull[cooked_ref_offset + 4] == 1

    mesh = _pack({"shape_type": "triangle_mesh"}, resource_index=99)
    assert _u32(mesh, cooked_ref_offset + 0) == 99
    assert mesh[cooked_ref_offset + 4] == 2

    height = _pack({"shape_type": "height_field"}, resource_index=99)
    assert _u32(height, cooked_ref_offset + 0) == 99
    assert height[cooked_ref_offset + 4] == 3

    compound = _pack({"shape_type": "compound"}, resource_index=99)
    assert _u32(compound, cooked_ref_offset + 0) == 0
    assert compound[cooked_ref_offset + 4] == 0


@pytest.mark.parametrize(
    ("shape_name", "expected_index", "expected_payload_type"),
    [
        ("invalid", 0, 0),
        ("sphere", 0, 0),
        ("capsule", 0, 0),
        ("box", 0, 0),
        ("cylinder", 0, 0),
        ("cone", 55, 1),
        ("convex_hull", 55, 1),
        ("triangle_mesh", 55, 2),
        ("height_field", 55, 3),
        ("plane", 0, 0),
        ("world_boundary", 0, 0),
        ("compound", 0, 0),
    ],
)
def test_all_shape_types_default_cooked_shape_ref_contract(
    shape_name, expected_index, expected_payload_type
):
    shape_params_offset = ASSET_HEADER_SIZE + 1 + 12 + 16 + 12 + 4 + 8 + 8 + 16
    cooked_ref_offset = shape_params_offset + 80
    desc = _pack({"shape_type": shape_name}, resource_index=55)
    assert _u32(desc, cooked_ref_offset + 0) == expected_index
    assert desc[cooked_ref_offset + 4] == expected_payload_type


def test_cooked_shape_ref_allows_explicit_override():
    shape_params_offset = ASSET_HEADER_SIZE + 1 + 12 + 16 + 12 + 4 + 8 + 8 + 16
    cooked_ref_offset = shape_params_offset + 80
    desc = _pack(
        {
            "shape_type": "triangle_mesh",
            "cooked_shape_ref": {"resource_index": 1234, "payload_type": "mesh"},
        },
        resource_index=99,
    )
    assert _u32(desc, cooked_ref_offset + 0) == 1234
    assert desc[cooked_ref_offset + 4] == 2


def test_legacy_shape_category_is_not_used_as_shape_type():
    desc = _pack({"shape_category": 3})
    assert desc[ASSET_HEADER_SIZE] == 0


def test_compound_children_are_serialized_as_trailing_array():
    desc = _pack(
        {
            "shape_type": "compound",
            "children": [
                {
                    "shape_type": "sphere",
                    "radius": 0.5,
                    "local_position": [1.0, 2.0, 3.0],
                    "local_rotation": [0.0, 0.0, 0.0, 1.0],
                    "local_scale": [1.0, 1.0, 1.0],
                },
                {
                    "shape_type": "convex_hull",
                    "payload_ref": "00112233445566778899aabbccddeeff",
                    "local_position": [4.0, 5.0, 6.0],
                    "local_rotation": [0.0, 0.0, 0.0, 1.0],
                    "local_scale": [0.9, 0.9, 0.9],
                },
            ],
        }
    )
    assert len(desc) == COLLISION_SHAPE_ASSET_DESC_SIZE + 2 * 128

    shape_params_offset = ASSET_HEADER_SIZE + 1 + 12 + 16 + 12 + 4 + 8 + 8 + 16
    child_count = _u32(desc, shape_params_offset + 0)
    child_byte_offset = _u32(desc, shape_params_offset + 4)
    assert child_count == 2
    assert child_byte_offset == COLLISION_SHAPE_ASSET_DESC_SIZE

    child0 = desc[child_byte_offset : child_byte_offset + 128]
    child1 = desc[child_byte_offset + 128 : child_byte_offset + 256]

    assert _u32(child0, 0) == 1  # sphere
    assert _f32(child0, 4) == pytest.approx(0.5)
    assert _f32(child0, 68) == pytest.approx(1.0)
    assert _f32(child0, 72) == pytest.approx(2.0)
    assert _f32(child0, 76) == pytest.approx(3.0)
    assert _bytes(child0, 108, 16) == b"\x00" * 16

    assert _u32(child1, 0) == 6  # convex_hull
    assert _f32(child1, 68) == pytest.approx(4.0)
    assert _f32(child1, 72) == pytest.approx(5.0)
    assert _f32(child1, 76) == pytest.approx(6.0)
    assert _bytes(child1, 108, 16) == bytes.fromhex(
        "00112233445566778899aabbccddeeff"
    )
