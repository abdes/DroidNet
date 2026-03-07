from pakgen.packing.packers import (
    pack_material_asset_descriptor,
    pack_geometry_asset_descriptor,
    pack_mesh_descriptor,
    pack_submesh_descriptor,
    pack_mesh_view_descriptor,
    pack_physics_material_asset_descriptor,
    pack_collision_shape_asset_descriptor,
    pack_physics_scene_asset_descriptor_and_payload,
    pack_rigid_body_binding_record,
    pack_collider_binding_record,
    pack_character_binding_record,
    pack_soft_body_binding_record,
    pack_joint_binding_record,
    pack_vehicle_binding_record,
    pack_aggregate_binding_record,
)
from pakgen.packing.constants import (
    MATERIAL_DESC_SIZE,
    GEOMETRY_DESC_SIZE,
    MESH_DESC_SIZE,
    SUBMESH_DESC_SIZE,
    MESH_VIEW_DESC_SIZE,
    ASSET_HEADER_SIZE,
    PHYSICS_MATERIAL_ASSET_DESC_SIZE,
    COLLISION_SHAPE_ASSET_DESC_SIZE,
    PHYSICS_SCENE_DESC_SIZE,
    RIGID_BODY_BINDING_RECORD_SIZE,
    COLLIDER_BINDING_RECORD_SIZE,
    CHARACTER_BINDING_RECORD_SIZE,
    SOFT_BODY_BINDING_RECORD_SIZE,
    JOINT_BINDING_RECORD_SIZE,
    VEHICLE_BINDING_RECORD_SIZE,
    AGGREGATE_BINDING_RECORD_SIZE,
)


def _header_builder(_):
    return b"\x00" * ASSET_HEADER_SIZE


def _shader_refs_builder(_):
    return b""


def _lods_builder(_):
    return b""


def test_material_descriptor_size():
    desc = pack_material_asset_descriptor(
        {},
        {},
        header_builder=_header_builder,
        shader_refs_builder=_shader_refs_builder,
    )
    assert len(desc) == MATERIAL_DESC_SIZE


def test_geometry_descriptor_size():
    desc = pack_geometry_asset_descriptor(
        {}, header_builder=_header_builder, lods_builder=_lods_builder
    )
    assert len(desc) == GEOMETRY_DESC_SIZE


def test_mesh_descriptor_size():
    # Provide minimal resource_index_map and name packer
    resource_index_map = {"buffer": {}}
    desc = pack_mesh_descriptor(
        {"name": "lod0", "vertex_buffer": "", "submeshes": []},
        resource_index_map,
        lambda n, sz: (n.encode() + b"\x00" * sz)[:sz],
    )
    assert len(desc) == MESH_DESC_SIZE


def test_submesh_descriptor_size():
    # Provide simple material asset with 16-byte key
    simple_assets = [{"name": "mat0", "key": b"\x00" * 16}]
    desc = pack_submesh_descriptor(
        {"name": "sm0", "material": "mat0", "mesh_views": []},
        simple_assets,
        lambda n, sz: (n.encode() + b"\x00" * sz)[:sz],
    )
    assert len(desc) == SUBMESH_DESC_SIZE


def test_mesh_view_descriptor_size():
    desc = pack_mesh_view_descriptor(
        {
            "first_index": 0,
            "index_count": 0,
            "first_vertex": 0,
            "vertex_count": 0,
        }
    )
    assert len(desc) == MESH_VIEW_DESC_SIZE


def test_physics_material_descriptor_size():
    desc = pack_physics_material_asset_descriptor(
        {},
        header_builder=_header_builder,
    )
    assert len(desc) == PHYSICS_MATERIAL_ASSET_DESC_SIZE


def test_collision_shape_descriptor_size():
    desc = pack_collision_shape_asset_descriptor(
        {},
        resource_asset_key=b"\x00" * 16,
        header_builder=_header_builder,
    )
    assert len(desc) == COLLISION_SHAPE_ASSET_DESC_SIZE


def test_physics_scene_descriptor_size():
    desc, payload = pack_physics_scene_asset_descriptor_and_payload(
        {
            "target_scene_key": "00000000-0000-0000-0000-000000000000",
            "target_node_count": 0,
        },
        header_builder=_header_builder,
    )
    assert len(desc) == PHYSICS_SCENE_DESC_SIZE
    assert payload == b""


def test_physics_binding_record_sizes():
    rigid = pack_rigid_body_binding_record(
        {},
        shape_asset_key=b"\x00" * 16,
        material_asset_key=b"\x00" * 16,
        node_count=1,
    )
    collider = pack_collider_binding_record(
        {},
        shape_asset_key=b"\x00" * 16,
        material_asset_key=b"\x00" * 16,
        node_count=1,
    )
    character = pack_character_binding_record(
        {},
        shape_asset_key=b"\x00" * 16,
        node_count=1,
    )
    soft = pack_soft_body_binding_record(
        {}, topology_asset_key=b"\x00" * 16, node_count=1
    )
    joint = pack_joint_binding_record(
        {}, constraint_asset_key=b"\x00" * 16, node_count=1
    )
    vehicle = pack_vehicle_binding_record(
        {}, constraint_asset_key=b"\x00" * 16, node_count=1
    )
    aggregate = pack_aggregate_binding_record({}, node_count=1)
    assert len(rigid) == RIGID_BODY_BINDING_RECORD_SIZE
    assert len(collider) == COLLIDER_BINDING_RECORD_SIZE
    assert len(character) == CHARACTER_BINDING_RECORD_SIZE
    assert len(soft) == SOFT_BODY_BINDING_RECORD_SIZE
    assert len(joint) == JOINT_BINDING_RECORD_SIZE
    assert len(vehicle) == VEHICLE_BINDING_RECORD_SIZE
    assert len(aggregate) == AGGREGATE_BINDING_RECORD_SIZE
