import json
import struct
from pathlib import Path

from pakgen.api import BuildOptions, build_pak


def _read_directory_entries(data: bytes) -> list[dict[str, int]]:
    footer = data[-256:]
    directory_offset, directory_size, asset_count = struct.unpack_from(
        "<QQQ", footer, 0
    )
    entries: list[dict[str, int]] = []
    for i in range(asset_count):
        off = directory_offset + i * 64
        entry = data[off : off + 64]
        asset_type = struct.unpack_from("<B", entry, 16)[0]
        _entry_offset, desc_offset, desc_size = struct.unpack_from(
            "<QQI", entry, 17
        )
        entries.append(
            {
                "asset_type": int(asset_type),
                "desc_offset": int(desc_offset),
                "desc_size": int(desc_size),
            }
        )
    return entries


def test_procedural_mesh_params_blob_is_emitted(tmp_path: Path):
    # 6-byte blob to verify procedural params are physically emitted.
    params_hex = "11223344aabb"
    params_blob = bytes.fromhex(params_hex)

    spec = {
        "version": 7,
        "content_version": 1,
        "buffers": [],
        "textures": [],
        "audios": [],
        "scripts": [],
        "assets": [
            {
                "type": "material",
                "name": "MatProc",
                "asset_key": "11" * 16,
            },
            {
                "type": "geometry",
                "name": "GeoProc",
                "asset_key": "22" * 16,
                "lods": [
                    {
                        "name": "Cube/ParamCube",
                        "mesh_type": 2,
                        "procedural_params": {
                            "data_hex": params_hex,
                        },
                        "submeshes": [
                            {
                                "name": "main",
                                "material": "MatProc",
                                "bounding_box_min": [-0.5, -0.5, -0.5],
                                "bounding_box_max": [0.5, 0.5, 0.5],
                                "mesh_views": [
                                    {
                                        "first_index": 0,
                                        "index_count": 36,
                                        "first_vertex": 0,
                                        "vertex_count": 24,
                                    }
                                ],
                            }
                        ],
                    }
                ],
            },
        ],
    }

    spec_path = tmp_path / "procedural_params_spec.json"
    spec_path.write_text(json.dumps(spec), encoding="utf-8")
    out_pak = tmp_path / "procedural_params.pak"

    build_pak(
        BuildOptions(
            input_spec=spec_path,
            output_path=out_pak,
            deterministic=True,
        )
    )

    data = out_pak.read_bytes()
    entries = _read_directory_entries(data)
    geometry_entries = [e for e in entries if e["asset_type"] == 2]
    assert len(geometry_entries) == 1

    geom_entry = geometry_entries[0]
    desc_offset = geom_entry["desc_offset"]
    desc_size = geom_entry["desc_size"]
    geom_blob = data[desc_offset : desc_offset + desc_size]
    assert len(geom_blob) == desc_size

    # Geometry descriptor is 256 bytes, then MeshDesc starts.
    mesh_desc_off = 256
    # MeshDesc info block starts at byte 73:
    # name(64) + mesh_type(1) + submesh_count(4) + mesh_view_count(4)
    params_size_off = mesh_desc_off + 73
    packed_params_size = struct.unpack_from("<I", geom_blob, params_size_off)[0]
    assert packed_params_size == len(params_blob)

    # Procedural param blob is written immediately after MeshDesc.
    params_blob_off = mesh_desc_off + 145
    assert geom_blob[params_blob_off : params_blob_off + len(params_blob)] == params_blob
