import json
from pathlib import Path
from pakgen.api import build_pak, BuildOptions
from pakgen.diff import diff_spec_vs_pak_deep


def _write_spec(path: Path, spec: dict):
    path.write_text(json.dumps(spec))


def test_diff_material_base_color_change(tmp_path: Path):
    # Arrange: build pak with one material
    spec = {
        "version": 1,
        "content_version": 1,
        "buffers": [],
        "textures": [],
        "audios": [],
        "assets": [
            {
                "type": "material",
                "name": "MatA",
                "asset_key": "22" * 16,
                "base_color": [0.1, 0.2, 0.3, 1.0],
            }
        ],
    }
    spec_path = tmp_path / "spec.json"
    _write_spec(spec_path, spec)
    out_path = tmp_path / "out.pak"
    build_pak(BuildOptions(input_spec=spec_path, output_path=out_path))

    # Act: modify expected base color and diff
    modified_spec = dict(spec)
    modified_spec["assets"] = [
        {
            "type": "material",
            "name": "MatA",
            "asset_key": "22" * 16,
            "base_color": [0.9, 0.8, 0.7, 1.0],
        }
    ]
    diff_res = diff_spec_vs_pak_deep(modified_spec, out_path)

    # Assert
    mat_diffs = diff_res["materials"]
    assert any(d.get("field") == "base_color" for d in mat_diffs)


def test_diff_geometry_removed_submesh(tmp_path: Path):
    # Arrange: geometry with one LOD two submeshes
    spec = {
        "version": 1,
        "content_version": 1,
        "buffers": [],
        "textures": [],
        "audios": [],
        "assets": [
            {"type": "material", "name": "MatA", "asset_key": "33" * 16},
            {"type": "material", "name": "MatB", "asset_key": "44" * 16},
            {
                "type": "geometry",
                "name": "GeoA",
                "lods": [
                    {
                        "mesh_type": 0,
                        "submeshes": [
                            {
                                "material": "MatA",
                                "mesh_views": [
                                    {
                                        "first_index": 0,
                                        "index_count": 3,
                                        "first_vertex": 0,
                                        "vertex_count": 3,
                                    }
                                ],
                            },
                            {
                                "material": "MatB",
                                "mesh_views": [
                                    {
                                        "first_index": 0,
                                        "index_count": 3,
                                        "first_vertex": 0,
                                        "vertex_count": 3,
                                    }
                                ],
                            },
                        ],
                    }
                ],
            },
        ],
    }
    spec_path = tmp_path / "spec.json"
    _write_spec(spec_path, spec)
    out_path = tmp_path / "out.pak"
    build_pak(BuildOptions(input_spec=spec_path, output_path=out_path))

    # Act: expected spec lists only one submesh now
    modified_spec = dict(spec)
    modified_spec["assets"] = [
        {"type": "material", "name": "MatA", "asset_key": "33" * 16},
        {"type": "material", "name": "MatB", "asset_key": "44" * 16},
        {
            "type": "geometry",
            "name": "GeoA",
            "lods": [
                {
                    "mesh_type": 0,
                    "submeshes": [
                        {
                            "material": "MatA",
                            "mesh_views": [
                                {
                                    "first_index": 0,
                                    "index_count": 3,
                                    "first_vertex": 0,
                                    "vertex_count": 3,
                                }
                            ],
                        },
                    ],
                }
            ],
        },
    ]
    diff_res = diff_spec_vs_pak_deep(modified_spec, out_path)
    geo_diffs = diff_res["geometries"]
    assert any(d.get("issue") == "removed_submesh" for d in geo_diffs)


def test_diff_geometry_lod_count(tmp_path: Path):
    # Arrange: geometry with one LOD
    spec = {
        "version": 1,
        "content_version": 1,
        "buffers": [],
        "textures": [],
        "audios": [],
        "assets": [
            {"type": "material", "name": "MatA", "asset_key": "55" * 16},
            {
                "type": "geometry",
                "name": "GeoA",
                "lods": [
                    {
                        "mesh_type": 0,
                        "submeshes": [
                            {
                                "material": "MatA",
                                "mesh_views": [
                                    {
                                        "first_index": 0,
                                        "index_count": 3,
                                        "first_vertex": 0,
                                        "vertex_count": 3,
                                    }
                                ],
                            },
                        ],
                    }
                ],
            },
        ],
    }
    spec_path = tmp_path / "spec.json"
    _write_spec(spec_path, spec)
    out_path = tmp_path / "out.pak"
    build_pak(BuildOptions(input_spec=spec_path, output_path=out_path))

    # Act: expected spec has two LODs now
    modified_spec = dict(spec)
    modified_spec["assets"] = [
        {"type": "material", "name": "MatA", "asset_key": "55" * 16},
        {
            "type": "geometry",
            "name": "GeoA",
            "lods": [
                {"mesh_type": 0, "submeshes": []},
                {"mesh_type": 0, "submeshes": []},
            ],
        },
    ]
    diff_res = diff_spec_vs_pak_deep(modified_spec, out_path)
    geo_diffs = diff_res["geometries"]
    assert any(d.get("issue") == "lod_count" for d in geo_diffs)
