"""Scene v5 node layout and canonical flag-mask producer contract."""

import struct
from pathlib import Path

import pytest

from pakgen.api import inspect_pak
from pakgen.packing.constants import ASSET_HEADER_SIZE, SCENE_DESC_SIZE
from pakgen.packing.errors import PakError
from pakgen.packing.packers import (
    _pack_node_record,
    pack_asset_header,
    pack_scene_asset_descriptor_and_payload,
)
from pakgen.spec.generators import expand_node
from pakgen.spec.validator import run_validation_pipeline


def _spec(node):
    return {
        "source_identity": "01a0a760-49ab-7304-88b2-98c0bc413938",
        "version": 7,
        "assets": [{"type": "scene", "name": "Flags", "nodes": [node]}],
    }


def _pack_scene(scene):
    return pack_scene_asset_descriptor_and_payload(
        scene, header_builder=pack_asset_header, geometry_name_to_key={}
    )


def test_node_record_places_inheritance_before_transform():
    node = {
        "node_id": "11" * 16,
        "flags": 0x32,
        "inherited_flags": 0x0D,
        "translation": [2.0, 3.0, 4.0],
        "rotation": [0.0, 0.0, 0.6, 0.8],
        "scale": [5.0, 6.0, 7.0],
    }
    data = _pack_node_record(node, index=0, name_offset=9, node_count=1)
    assert len(data) == 72
    assert data[:16] == bytes.fromhex("11" * 16)
    assert struct.unpack_from("<4I", data, 16) == (9, 0, 0x32, 0x0D)
    assert struct.unpack_from("<3f", data, 32) == (2.0, 3.0, 4.0)
    assert struct.unpack_from("<4f", data, 44) == pytest.approx((0, 0, 0.6, 0.8))
    assert struct.unpack_from("<3f", data, 60) == (5.0, 6.0, 7.0)


@pytest.mark.parametrize("flags", [None, 0, 1, 4, 8, 0x3F])
def test_existing_numeric_values_remain_local_for_root_and_child(flags):
    nodes = [{"name": "Root"}, {"name": "Child", "parent": 0}]
    if flags is not None:
        for node in nodes:
            node["flags"] = flags
    scene = {"type": "scene", "name": "Flags", "nodes": nodes}
    descriptor, payload, _ = _pack_scene(scene)
    assert descriptor[65] == 5
    assert struct.unpack_from("<QII", descriptor, ASSET_HEADER_SIZE) == (
        SCENE_DESC_SIZE,
        2,
        72,
    )
    strings_offset, _ = struct.unpack_from("<II", descriptor, ASSET_HEADER_SIZE + 16)
    assert strings_offset == SCENE_DESC_SIZE + 2 * 72
    for index in range(2):
        assert struct.unpack_from("<II", payload, index * 72 + 24) == (flags or 0, 0)


@pytest.mark.parametrize("flags,inherited", [(0, 1), (0, 4), (0, 8), (0x32, 0x0D)])
def test_valid_inheritance_is_accepted_by_spec_and_direct_packer(flags, inherited):
    node = {"name": "Root", "flags": flags, "inherited_flags": inherited}
    assert not run_validation_pipeline(_spec(node))
    data = _pack_node_record(node, index=0, name_offset=0, node_count=1)
    assert struct.unpack_from("<II", data, 24) == (flags, inherited)


@pytest.mark.parametrize(
    "field,value,code",
    [
        ("flags", True, "E_TYPE"),
        ("flags", "1", "E_TYPE"),
        ("flags", 1.0, "E_TYPE"),
        ("flags", None, "E_TYPE"),
        ("flags", -1, "E_RANGE"),
        ("flags", 1 << 32, "E_RANGE"),
        ("flags", 1 << 6, "E_SCENE_FLAGS"),
        ("inherited_flags", False, "E_TYPE"),
        ("inherited_flags", "1", "E_TYPE"),
        ("inherited_flags", 1.0, "E_TYPE"),
        ("inherited_flags", None, "E_TYPE"),
        ("inherited_flags", -1, "E_RANGE"),
        ("inherited_flags", 1 << 32, "E_RANGE"),
        ("inherited_flags", 1 << 6, "E_SCENE_FLAGS"),
        ("inherited_flags", 1 << 1, "E_SCENE_FLAGS"),
        ("inherited_flags", 1 << 4, "E_SCENE_FLAGS"),
        ("inherited_flags", 1 << 5, "E_SCENE_FLAGS"),
    ],
)
def test_invalid_masks_fail_before_packing(field, value, code):
    node = {"name": "Root", field: value}
    errors = run_validation_pipeline(_spec(node))
    assert code in {error.code for error in errors}
    with pytest.raises(PakError) as caught:
        _pack_node_record(node, index=0, name_offset=0, node_count=1)
    assert caught.value.code == code


@pytest.mark.parametrize("bit", [1, 4, 8])
def test_inherited_bits_cannot_also_store_local_true(bit):
    node = {"name": "Root", "flags": bit, "inherited_flags": bit}
    assert "E_SCENE_FLAGS" in {
        error.code for error in run_validation_pipeline(_spec(node))
    }
    with pytest.raises(PakError, match="must not overlap"):
        _pack_node_record(node, index=0, name_offset=0, node_count=1)


@pytest.mark.parametrize("version", [None, 1, 2, 3, 4, 6, "5", 5.0, True])
def test_only_current_scene_descriptor_version_is_emitted(version):
    spec = _spec({"name": "Root"})
    scene = spec["assets"][0]
    scene["version"] = version
    assert "E_VERSION" in {error.code for error in run_validation_pipeline(spec)}
    with pytest.raises(PakError, match="Scene asset version 5 is required"):
        _pack_scene(scene)


@pytest.mark.parametrize(
    "layout,params",
    [
        ("grid", {"count": [2, 1, 1]}),
        ("linear", {"count": 2}),
        ("circle", {"count": 2}),
        ("scatter", {"count": 2, "seed": 7}),
    ],
)
def test_generated_nodes_keep_explicit_local_and_inherited_masks(layout, params):
    nodes = expand_node(
        {
            "name": "Generated",
            "parent": 0,
            "flags": 0x32,
            "inherited_flags": 0x0D,
            "generate": {"layout": layout, layout: params},
        },
        1,
        "scene.nodes[1]",
    )
    assert len(nodes) == 2
    for node in nodes:
        assert node["flags"] == 0x32
        assert node["inherited_flags"] == 0x0D


@pytest.mark.parametrize(
    "name",
    [
        "scene_basic.pak",
        "scripting_scene_ref.pak",
        "input_scene_ref.pak",
        "scene_with_physics_sidecar_ref.pak",
    ],
)
def test_maintained_scene_fixtures_use_v5_local_masks(name):
    path = Path(__file__).parent / "_golden" / name
    data = path.read_bytes()
    entries = [
        entry
        for entry in inspect_pak(path)["directory_entries"]
        if entry["asset_type"] == 3
    ]
    assert entries
    for entry in entries:
        descriptor = entry["desc_offset"]
        assert data[descriptor + 65] == 5
        offset, count, stride = struct.unpack_from(
            "<QII", data, descriptor + ASSET_HEADER_SIZE
        )
        assert count > 0
        assert stride == 72
        for index in range(count):
            flags, inherited = struct.unpack_from(
                "<II", data, descriptor + offset + index * stride + 24
            )
            assert flags & ~0x3F == 0
            assert inherited == 0
