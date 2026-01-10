from __future__ import annotations

"""Padding & variable extra size tests.

Ensures:
- total padding == sum(section paddings
- geometry variable_extra_size accounted in asset plan
- file_size decomposition matches: footer.offset + FOOTER_SIZE
"""

from pathlib import Path
import json

from pakgen.api import plan_dry_run
from pakgen.packing.constants import FOOTER_SIZE


def _spec_with_padding() -> dict:
    # Intentionally add two textures to force region + table sizing and an aligned material.
    return {
        "version": 1,
        "content_version": 2,
        "textures": [
            {
                "name": "t_a",
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
            {
                "name": "t_b",
                "texture_type": 3,
                "compression_type": 0,
                "width": 8,
                "height": 8,
                "depth": 1,
                "array_layers": 1,
                "mip_levels": 1,
                "format": 30,
                "alignment": 256,
                "data_hex": "ff" * (8 * 8 * 4),
            },
        ],
        "buffers": [
            {"name": "buf_z", "stride": 4, "data": "CCCC"},
        ],
        "audios": [],
        "assets": [
            {"type": "material", "name": "mat_z", "alignment": 32},
            {
                "type": "geometry",
                "name": "geo_z",
                "lods": [
                    {
                        "name": "lod0",
                        "vertex_buffer": "buf_z",
                        "index_buffer": None,
                        "submeshes": [
                            {
                                "name": "s0",
                                "material": "mat_z",
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
            },
        ],
    }


def test_padding_accounting(tmp_path: Path):  # noqa: N802
    spec_path = tmp_path / "pad.json"
    spec_path.write_text(json.dumps(_spec_with_padding()), encoding="utf-8")
    pak_plan, plan_dict = plan_dry_run(spec_path)

    # Padding total matches sum
    pad = plan_dict["padding"]
    assert pad["total"] == sum(pad["by_section"].values())

    # Geometry variable extra present and >0
    geom_assets = [a for a in plan_dict["assets"] if a["type"] == "geometry"]
    assert geom_assets and geom_assets[0]["variable_extra_size"] > 0

    # File size consistent with footer offset
    assert plan_dict["footer"]["offset"] + FOOTER_SIZE == plan_dict["file_size"]
