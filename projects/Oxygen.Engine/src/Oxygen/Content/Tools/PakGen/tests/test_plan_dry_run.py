from __future__ import annotations

"""Dry-run planning parity tests.

Ensures compute_pak_plan (dry run) produces offsets matching actual written file
for simple spec cases (materials + geometries + resources). This is an initial
smoke test; future phases will expand coverage & edge cases.
"""

from pathlib import Path
import json

from pakgen.api import BuildOptions, build_pak, plan_dry_run, inspect_pak
from pakgen.packing.constants import FOOTER_SIZE, DIRECTORY_ENTRY_SIZE


def _basic_spec_dict() -> dict:
    return {
        "version": 1,
        "content_version": 1,
        "textures": [
            {
                "name": "t0",
                "texture_type": 3,
                "compression_type": 0,
                "width": 4,
                "height": 4,
                "depth": 1,
                "array_layers": 1,
                "mip_levels": 1,
                "format": 30,
                "alignment": 256,
                "data_hex": "ff" * (4 * 4 * 4),
            },
        ],
        "buffers": [
            {"name": "b0", "stride": 4, "data": "BBBB"},
        ],
        "audios": [],
        "materials": [
            {"name": "mat0", "alignment": 16},
        ],
        "geometries": [
            {
                "name": "geo0",
                "lods": [
                    {
                        "name": "lod0",
                        "vertex_buffer": "b0",
                        "index_buffer": None,
                        "submeshes": [
                            {
                                "name": "sm0",
                                "material": "mat0",
                                "mesh_views": [
                                    {
                                        "first_index": 0,
                                        "index_count": 0,
                                        "first_vertex": 0,
                                        "vertex_count": 0,
                                    }
                                ],
                            }
                        ],
                    }
                ],
            }
        ],
    }


def test_plan_offsets_match_written(tmp_path: Path):  # noqa: N802
    spec_path = tmp_path / "spec.json"
    spec_path.write_text(json.dumps(_basic_spec_dict()), encoding="utf-8")

    # Dry run plan
    pak_plan, plan_dict = plan_dry_run(spec_path)

    # Build actual pak
    out_path = tmp_path / "out.pak"
    build_pak(BuildOptions(input_spec=spec_path, output_path=out_path))
    info = inspect_pak(out_path)
    footer = info["footer"]

    # Compare directory & footer offsets / file_size
    if plan_dict["directory"]["offset"]:
        assert plan_dict["directory"]["offset"] == footer["directory"]["offset"]
        assert plan_dict["directory"]["size"] == footer["directory"]["size"]
    assert plan_dict["footer"]["offset"] == footer["offset"]
    assert plan_dict["file_size"] == info["file_size"]

    # Asset count parity
    assert (
        plan_dict["directory"]["asset_count"]
        == footer["directory"]["asset_count"]
    )


def test_plan_empty_spec(tmp_path: Path):  # noqa: N802
    empty = {
        "version": 1,
        "content_version": 0,
        "textures": [],
        "buffers": [],
        "audios": [],
        "materials": [],
        "geometries": [],
    }
    spec_path = tmp_path / "empty.json"
    spec_path.write_text(json.dumps(empty), encoding="utf-8")
    pak_plan, plan_dict = plan_dry_run(spec_path)
    assert plan_dict["directory"]["asset_count"] == 0
    assert plan_dict["directory"]["offset"] == 0
    assert plan_dict["footer"]["offset"] == plan_dict["file_size"] - FOOTER_SIZE
