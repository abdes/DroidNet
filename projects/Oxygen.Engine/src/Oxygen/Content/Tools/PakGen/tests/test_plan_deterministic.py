from __future__ import annotations

"""Deterministic ordering tests.

Verifies that enabling deterministic flag reorders assets/resources by name while
producing same final file size for given spec.
"""

from pathlib import Path
import json

from pakgen.api import plan_dry_run


def _spec_unsorted() -> dict:
    return {
        "version": 1,
        "content_version": 1,
        "textures": [
            {
                "name": "tex_b",
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
                "name": "tex_a",
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
        "buffers": [],
        "audios": [],
        "assets": [
            {"type": "material", "name": "mat_z"},
            {"type": "material", "name": "mat_a"},
        ],
    }


def test_deterministic_asset_ordering(tmp_path: Path):  # noqa: N802
    spec_path = tmp_path / "unsorted.json"
    spec_path.write_text(json.dumps(_spec_unsorted()), encoding="utf-8")
    plan_nd, d_nd = plan_dry_run(spec_path, deterministic=False)
    plan_d, d_d = plan_dry_run(spec_path, deterministic=True)

    names_nd = [a["name"] for a in d_nd["assets"]]
    names_d = [a["name"] for a in d_d["assets"]]
    assert names_nd == ["mat_z", "mat_a"]  # original insertion
    assert names_d == ["mat_a", "mat_z"]  # sorted
    assert d_d["file_size"] == d_nd["file_size"]
