from __future__ import annotations

"""Planner layout & empty spec tests (ported from legacy planner tests).

Covers:
- Basic resource + asset counts
- Descriptor ordering & alignment assumptions
- Directory/footer ordering invariants
- CRC field offset sanity
- Empty spec behavior
"""

from pathlib import Path
import json

from pakgen.api import build_pak, BuildOptions, inspect_pak
from pakgen.spec.models import (
    PakSpec,
    TextureResource,
    BufferResource,
    MaterialAsset,
    GeometryAsset,
    GeometryLod,
    Submesh,
    MeshView,
)
from pakgen.packing.constants import (
    DATA_ALIGNMENT,
    TABLE_ALIGNMENT,
    FOOTER_SIZE,
    DIRECTORY_ENTRY_SIZE,
)


def _aligned(value: int, alignment: int) -> bool:
    return (value % alignment) == 0


def _build_basic_spec(tmp_path: Path) -> Path:
    # Build an in-memory spec (mirror legacy test_planner_basic_layout scenario)
    spec = {
        "version": 1,
        "content_version": 7,
        "textures": [
            {
                "name": "albedo_tex",
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
            {"name": "vb0", "stride": 4, "data": "BBBB"},
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
                        "vertex_buffer": "vb0",
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
    spec_path = tmp_path / "planner_basic.json"
    spec_path.write_text(json.dumps(spec), encoding="utf-8")
    out_path = tmp_path / "planner_basic.pak"
    build_pak(
        BuildOptions(input_spec=spec_path, output_path=out_path, force=True)
    )
    return out_path


def test_planner_basic_layout(tmp_path: Path):  # noqa: N802
    pak_path = _build_basic_spec(tmp_path)
    info = inspect_pak(pak_path)
    footer = info["footer"]

    # Basic counts
    assets = info.get("directory_entries", [])
    material_entries = [e for e in assets if e["asset_type"] == 1]
    geometry_entries = [e for e in assets if e["asset_type"] == 2]
    assert len(material_entries) == 1
    assert len(geometry_entries) == 1
    assert footer["directory"]["asset_count"] == 2

    # Resource regions & tables alignment
    for rname, region in footer["regions"].items():
        if region["offset"]:
            assert _aligned(
                region["offset"], DATA_ALIGNMENT
            ), f"{rname} region not aligned"
    for tname, table in footer["tables"].items():
        if table["offset"]:
            assert _aligned(
                table["offset"], TABLE_ALIGNMENT
            ), f"{tname} table not aligned"

    # Directory & footer ordering
    directory_offset = footer["directory"]["offset"]
    directory_size = footer["directory"]["size"]
    assert _aligned(directory_offset, TABLE_ALIGNMENT)
    footer_offset = footer["offset"]
    directory_end = directory_offset + directory_size
    browse = footer.get("browse_index") or {"offset": 0, "size": 0}
    if browse.get("size", 0):
        assert browse["offset"] == directory_end
        assert footer_offset == browse["offset"] + browse["size"]
    else:
        assert footer_offset == directory_end
    assert info["file_size"] == footer_offset + FOOTER_SIZE

    # CRC32 location sanity (last 12 bytes has crc32 + magic)
    assert (footer_offset + FOOTER_SIZE - 12) == (info["file_size"] - 12)

    # Descriptor ordering: material before geometry by descriptor offset
    mat_desc_off = material_entries[0]["desc_offset"]
    geo_desc_off = geometry_entries[0]["desc_offset"]
    assert mat_desc_off < geo_desc_off
    assert _aligned(mat_desc_off, 16)


def test_planner_empty_spec(tmp_path: Path):  # noqa: N802
    spec = {
        "version": 1,
        "content_version": 0,
        "buffers": [],
        "textures": [],
        "audios": [],
        "assets": [],
    }
    spec_path = tmp_path / "empty.json"
    spec_path.write_text(json.dumps(spec), encoding="utf-8")
    out_path = tmp_path / "empty.pak"
    build_pak(
        BuildOptions(input_spec=spec_path, output_path=out_path, force=True)
    )
    info = inspect_pak(out_path)
    footer = info["footer"]
    # No assets => directory omitted (offset/size zero)
    assert footer["directory"]["asset_count"] == 0
    assert footer["directory"]["offset"] == 0
    assert footer["directory"]["size"] == 0
    # Footer located at file_size - FOOTER_SIZE
    assert footer["offset"] == info["file_size"] - FOOTER_SIZE
    assert info["file_size"] == footer["offset"] + FOOTER_SIZE
