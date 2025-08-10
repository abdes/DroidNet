import json
from pathlib import Path
from pakgen.api import plan_dry_run, BuildOptions, build_pak


def write_spec(tmp_path: Path) -> Path:
    spec = {
        "version": 1,
        "buffers": [
            {"name": "empty_a", "path": None, "size": 0},
            {
                "name": "vertex_data",
                "path": None,
                "size": 12,
                "data_hex": "000000000100000002000000",
            },
            {"name": "empty_b", "path": None, "size": 0},
        ],
        "textures": [],
        "audios": [],
        "assets": [],
    }
    path = tmp_path / "mixed_buffers.json"
    path.write_text(json.dumps(spec), encoding="utf-8")
    return path


def test_mixed_zero_and_nonzero_buffers(tmp_path: Path):
    spec_path = write_spec(tmp_path)
    plan, _ = plan_dry_run(spec_path, deterministic=True)
    # Buffer region should exist (non-zero) because there is a non-empty blob.
    buf_regions = [r for r in plan.regions if r.name == "buffer"]
    assert buf_regions and buf_regions[0].size > 0
    # Build should succeed without errors.
    out_pak = tmp_path / "out.pak"
    res = build_pak(
        BuildOptions(
            input_spec=spec_path, output_path=out_pak, deterministic=True
        )
    )
    assert out_pak.exists() and res.bytes_written > 0
