from __future__ import annotations

"""Snapshot test for deterministic PakPlan JSON.

Generates plan for a basic spec under deterministic mode and compares against
committed golden snapshot. Acts as regression guard for layout changes.
"""
import json
from pathlib import Path
from pakgen.api import plan_dry_run
from snapshot_helper import assert_matches_snapshot


def _spec() -> dict:
    return {
        "version": 1,
        "content_version": 1,
        "textures": [
            {"name": "t0", "width": 4, "height": 4, "data": "AAA"},
        ],
        "buffers": [
            {"name": "b0", "stride": 4, "data": "BBBB"},
        ],
        "audios": [],
        "assets": [
            {"type": "material", "name": "mat0", "alignment": 16},
            {
                "type": "geometry",
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
            },
        ],
    }


def test_plan_snapshot(tmp_path: Path):  # noqa: N802
    spec_path = tmp_path / "spec.json"
    spec_path.write_text(json.dumps(_spec()), encoding="utf-8")
    _plan, d = plan_dry_run(spec_path, deterministic=True)
    assert_matches_snapshot(d, "plan_basic_deterministic.json")
