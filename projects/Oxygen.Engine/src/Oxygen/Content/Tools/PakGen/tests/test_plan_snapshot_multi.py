from __future__ import annotations

"""Multi-resource deterministic plan snapshot test."""
import json
from pathlib import Path
from pakgen.api import plan_dry_run
from snapshot_helper import assert_matches_snapshot


def _spec_multi() -> dict:
    return {
        "version": 1,
        "content_version": 2,
        "textures": [
            {"name": "tex_a", "width": 4, "height": 4, "data": "A"},
            {"name": "tex_b", "width": 8, "height": 8, "data": "BBBBBBBB"},
        ],
        "buffers": [
            {"name": "buf_a", "stride": 4, "data": "CCCCCCCCCCCC"},
            {"name": "buf_b", "stride": 8, "data": "DDDDDDDDDDDDDDDD"},
        ],
        "audios": [],
        "assets": [
            {"type": "material", "name": "mat_a", "alignment": 16},
            {"type": "material", "name": "mat_b", "alignment": 32},
            {
                "type": "geometry",
                "name": "geo_a",
                "lods": [
                    {
                        "name": "lod0",
                        "vertex_buffer": "buf_a",
                        "index_buffer": None,
                        "submeshes": [
                            {
                                "name": "sm0",
                                "material": "mat_a",
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
            {"type": "geometry", "name": "geo_b", "lods": []},
        ],
    }


def test_plan_snapshot_multi(tmp_path: Path):  # noqa: N802
    spec_path = tmp_path / "spec_multi.json"
    spec_path.write_text(json.dumps(_spec_multi()), encoding="utf-8")
    _plan, d = plan_dry_run(spec_path, deterministic=True)
    assert_matches_snapshot(d, "plan_multi_deterministic.json")
