import json
from pathlib import Path
import pytest
from pakgen.api import BuildOptions, build_pak, plan_dry_run


def write_spec(tmp_path: Path) -> Path:
    spec = {
        "version": 1,
        "buffers": [
            {"name": "default_empty_buffer", "path": None, "size": 0},
        ],
        "textures": [
            {
                "name": "default_texture",
                "path": None,
                "width": 1,
                "height": 1,
                "format": 1,
            },
        ],
        "audios": [],
        "assets": [
            {
                "type": "material",
                "name": "test_material",
                "key": "01234567-89ab-cdef-0123-456789abcdef",
                "shader_stage_mask": 0,
                "textures": {"base_color": "default_texture"},
            }
        ],
    }
    path = tmp_path / "empty_buffer_spec.json"
    path.write_text(json.dumps(spec), encoding="utf-8")
    return path


def test_zero_length_buffer_region_elided(tmp_path: Path):
    spec_path = write_spec(tmp_path)
    # Plan first (deterministic)
    plan, plan_dict = plan_dry_run(spec_path, deterministic=True)
    # Buffer region should either be absent or have size 0 and no blobs remaining.
    buffer_regions = [r for r in plan.regions if r.name == "buffer"]
    assert (not buffer_regions) or buffer_regions[0].size == 0
    # Build should succeed (no runtime error)
    out_pak = tmp_path / "out.pak"
    opts = BuildOptions(
        input_spec=spec_path, output_path=out_pak, deterministic=True
    )
    res = build_pak(opts)
    assert out_pak.exists() and res.bytes_written > 0
