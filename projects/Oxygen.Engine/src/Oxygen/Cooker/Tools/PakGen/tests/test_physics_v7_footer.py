from pathlib import Path

from pakgen.api import BuildOptions, build_pak, inspect_pak, validate_pak


def test_v7_footer_reports_physics_region_and_table(tmp_path: Path):
    spec = {
        "version": 7,
        "content_version": 1,
        "physics": [
            {
                "name": "shape_blob",
                "format": 1,
                "data_hex": "01020304",
            }
        ],
        "assets": [
            {
                "type": "physics_material",
                "name": "pmat0",
                "asset_key": "11111111-1111-1111-1111-111111111111",
            },
            {
                "type": "collision_shape",
                "name": "cshape0",
                "asset_key": "22222222-2222-2222-2222-222222222222",
                "resource_name": "shape_blob",
                "shape_type": "box",
                "shape_params": {"half_extents": [0.5, 0.5, 0.5]},
            },
            {
                "type": "physics_scene",
                "name": "scene0.opscene",
                "asset_key": "33333333-3333-3333-3333-333333333333",
                "target_scene_key": "44444444-4444-4444-4444-444444444444",
                "target_node_count": 0,
            },
        ],
        "buffers": [],
        "textures": [],
        "audios": [],
        "scripts": [],
    }

    import json

    spec_path = tmp_path / "physics_v7.json"
    spec_path.write_text(json.dumps(spec), encoding="utf-8")
    out_pak = tmp_path / "physics_v7.pak"
    build_pak(
        BuildOptions(
            input_spec=spec_path,
            output_path=out_pak,
            deterministic=True,
        )
    )

    info = inspect_pak(out_pak)
    issues = validate_pak(out_pak)
    assert issues == []

    physics_region = info["footer"]["regions"]["physics"]
    physics_table = info["footer"]["tables"]["physics"]
    assert physics_region["size"] > 0
    # sentinel + authored entry
    assert physics_table["count"] == 2
    assert physics_table["entry_size"] == 48

    asset_types = {e["asset_type"] for e in info.get("directory_entries", [])}
    assert 7 in asset_types
    assert 8 in asset_types
    assert 9 in asset_types
