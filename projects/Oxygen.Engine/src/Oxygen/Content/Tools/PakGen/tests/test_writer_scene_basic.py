import json
from pathlib import Path
import struct

from pakgen.api import BuildOptions, build_pak


def test_build_pak_with_scene_asset(tmp_path: Path):
    spec = {
        "version": 1,
        "content_version": 7,
        "buffers": [],
        "textures": [],
        "audios": [],
        "assets": [
            {
                "type": "material",
                "name": "MatA",
                "asset_key": "11" * 16,
                "alignment": 1,
            },
            {
                "type": "geometry",
                "name": "GeoA",
                "asset_key": "22" * 16,
                "lods": [
                    {
                        "vertex_count": 3,
                        "index_count": 3,
                        "mesh_type": 0,
                        "submeshes": [
                            {
                                "material": "MatA",
                                "bounding_box_min": [0.0, 0.0, 0.0],
                                "bounding_box_max": [1.0, 1.0, 1.0],
                                "mesh_views": [
                                    {
                                        "first_index": 0,
                                        "index_count": 3,
                                        "first_vertex": 0,
                                        "vertex_count": 3,
                                    }
                                ],
                            }
                        ],
                    }
                ],
            },
            {
                "type": "scene",
                "name": "SceneA",
                "asset_key": "33" * 16,
                "nodes": [
                    {"name": "Root", "parent": None},
                    {"name": "Child", "parent": 0},
                ],
                "renderables": [
                    {"node_index": 1, "geometry": "GeoA", "visible": True}
                ],
            },
        ],
    }

    spec_path = tmp_path / "spec.json"
    spec_path.write_text(json.dumps(spec), encoding="utf-8")
    out_path = tmp_path / "out.pak"

    res = build_pak(BuildOptions(input_spec=spec_path, output_path=out_path))
    data = out_path.read_bytes()
    assert res.bytes_written == len(data)

    # Footer: directory metadata at start
    footer = data[-256:]
    directory_offset, directory_size, asset_count = struct.unpack_from(
        "<QQQ", footer, 0
    )
    assert asset_count == 3
    assert directory_offset != 0
    assert directory_size == 3 * 64

    # Directory entry format: AssetKey(16) + asset_type(u8) + entry_off(u64)
    # + desc_off(u64) + desc_size(u32) + reserved(27)
    entries = data[directory_offset : directory_offset + directory_size]
    seen_scene = False
    for i in range(asset_count):
        e = entries[i * 64 : (i + 1) * 64]
        asset_type = e[16]
        desc_size = struct.unpack_from("<I", e, 16 + 1 + 8 + 8)[0]
        if asset_type == 3:
            seen_scene = True
            # Scene descriptor must be at least 256 bytes (fixed header) plus payload
            assert desc_size >= 256
    assert seen_scene
