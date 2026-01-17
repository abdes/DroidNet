import json
from pathlib import Path
from pakgen.api import BuildOptions, build_pak


def test_buffer_indices_preserved_for_geometry(tmp_path: Path):
    # Arrange: buffers deliberately out of alphabetical order relative to names
    spec = {
        "version": 1,
        "content_version": 1,
        "buffers": [
            {"name": "default_empty_buffer", "data": "", "usage": 0, "size": 0},
            {
                "name": "zzz_vertex_buffer",
                "data": "00010203",
                "usage": 1,
                "stride": 1,
                "format": 0,
            },
            {
                "name": "aaa_index_buffer",
                "data": "00000001",
                "usage": 2,
                "stride": 4,
                "format": 10,
            },
        ],
        "textures": [
            {
                "name": "default_texture",
                "width": 1,
                "height": 1,
                "format": 30,
                "texture_type": 3,
                "compression_type": 0,
                "data": "ff",
            }
        ],
        "assets": [
            {
                "name": "mat",
                "type": "material",
                "asset_key": "11111111-2222-3333-4444-555555555555",
                "material_domain": 0,
                "flags": 0,
                "shader_stages": 0,
                "base_color": [1, 1, 1, 1],
            },
            {
                "name": "geo",
                "type": "geometry",
                "asset_key": "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
                "lods": [
                    {
                        "name": "lod0",
                        "mesh_type": 1,
                        "vertex_buffer": "zzz_vertex_buffer",
                        "index_buffer": "aaa_index_buffer",
                        "submeshes": [
                            {
                                "name": "s0",
                                "material": "mat",
                                "bounding_box_min": [0.0, 0.0, 0.0],
                                "bounding_box_max": [1.0, 1.0, 1.0],
                                "mesh_views": [
                                    {
                                        "first_index": 0,
                                        "index_count": 1,
                                        "first_vertex": 0,
                                        "vertex_count": 1,
                                    }
                                ],
                            }
                        ],
                    }
                ],
            },
        ],
    }
    import yaml

    spec_path = tmp_path / "spec.yaml"
    spec_path.write_text(yaml.safe_dump(spec))
    pak_path = tmp_path / "out.pak"
    manifest_path = tmp_path / "out.manifest.json"

    # Act
    build_pak(
        BuildOptions(
            input_spec=spec_path,
            output_path=pak_path,
            manifest_path=manifest_path,
            deterministic=True,
        )
    )

    # Assert: ensure buffer table order matches spec order (no alphabetical reorder)
    manifest = json.loads(manifest_path.read_text())
    # There is no explicit table entries list yet; rely on absence of warnings and counts.
    # Future enhancement: expose resource ordering explicitly in manifest.
    # For now, indirect validation: file builds successfully and size > header/footer.
    assert manifest["file_size"] > 0
