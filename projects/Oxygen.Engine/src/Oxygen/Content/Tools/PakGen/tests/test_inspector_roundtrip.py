from __future__ import annotations

"""Inspector roundtrip tests (ported from legacy inspector basic).

Builds two specs (empty & simple) via API then inspects and validates.
"""
from pathlib import Path
import json

from pakgen.api import build_pak, BuildOptions, inspect_pak, validate_pak


def _write_spec(tmp: Path, name: str, spec: dict) -> Path:
    p = tmp / f"{name}.json"
    p.write_text(json.dumps(spec), encoding="utf-8")
    return p


def _build(tmp: Path, spec_name: str, spec: dict) -> Path:
    sp = _write_spec(tmp, spec_name, spec)
    out = tmp / f"{spec_name}.pak"
    build_pak(BuildOptions(input_spec=sp, output_path=out, force=True))
    return out


EMPTY_SPEC = {
    "version": 1,
    "content_version": 0,
    "buffers": [],
    "textures": [],
    "audios": [],
    "assets": [],
}

SIMPLE_SPEC = {
    "version": 1,
    "content_version": 2,
    "buffers": [
        {"name": "vb0", "stride": 4, "data": "BBBB"},
    ],
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
            # 4x4 RGBA8 texels without row padding (will be expanded).
            "data_hex": "ff" * (4 * 4 * 4),
        },
    ],
    "audios": [],
    "assets": [
        {"type": "material", "name": "matA"},
        {
            "type": "geometry",
            "name": "geoA",
            "lods": [
                {
                    "name": "lod0",
                    "vertex_buffer": "vb0",
                    "submeshes": [
                        {
                            "name": "SM_A",
                            "material": "matA",
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


def test_inspector_empty_and_simple(tmp_path: Path):  # noqa: N802
    empty_pak = _build(tmp_path, "empty_spec", EMPTY_SPEC)
    simple_pak = _build(tmp_path, "simple_spec", SIMPLE_SPEC)

    empty_info = inspect_pak(empty_pak)
    simple_info = inspect_pak(simple_pak)

    empty_issues = validate_pak(empty_pak)
    simple_issues = validate_pak(simple_pak)

    assert not empty_issues
    assert not simple_issues

    assert empty_info["header"]["magic_ok"]
    assert simple_info["header"]["magic_ok"]

    assert empty_info["footer"]["directory"]["asset_count"] == 0
    assert simple_info["footer"]["directory"]["asset_count"] == 2

    assert empty_info["footer"]["crc_match"]
    assert simple_info["footer"]["crc_match"]

    types = [e["asset_type"] for e in simple_info.get("directory_entries", [])]
    assert types == [1, 2]

    sizes = [e["desc_size"] for e in simple_info.get("directory_entries", [])]
    assert all(s > 0 for s in sizes)
