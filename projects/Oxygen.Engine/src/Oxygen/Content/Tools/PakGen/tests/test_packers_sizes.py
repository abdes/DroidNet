from pakgen.packing.packers import (
    pack_material_asset_descriptor,
    pack_geometry_asset_descriptor,
    pack_mesh_descriptor,
    pack_submesh_descriptor,
    pack_mesh_view_descriptor,
)
from pakgen.packing.constants import (
    MATERIAL_DESC_SIZE,
    GEOMETRY_DESC_SIZE,
    MESH_DESC_SIZE,
    SUBMESH_DESC_SIZE,
    MESH_VIEW_DESC_SIZE,
)


def _header_builder(_):
    return b"\x00" * 32


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
