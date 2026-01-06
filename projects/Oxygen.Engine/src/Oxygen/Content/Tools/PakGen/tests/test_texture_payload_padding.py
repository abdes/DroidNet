from pathlib import Path

from pakgen.packing.planner import build_plan


def test_rgba8_1x1_payload_padded_to_256(tmp_path: Path):
    spec = {
        "version": 1,
        "content_version": 0,
        "buffers": [],
        "textures": [
            {
                "name": "Tiny",
                "texture_type": 3,
                "compression_type": 0,
                "width": 1,
                "height": 1,
                "depth": 1,
                "array_layers": 1,
                "mip_levels": 1,
                "format": 30,
                "alignment": 256,
                "data_hex": "ffffffff",
            }
        ],
        "audios": [],
        "assets": [],
    }

    build = build_plan(spec, tmp_path)

    # PakGen inserts a fallback texture at index 0.
    assert build.resources.index_map["texture"]["Tiny"] == 1
    assert build.resources.desc_fields["texture"][1]["name"] == "Tiny"
    assert len(build.resources.data_blobs["texture"][1]) == 256


def test_rgba8_2x2_payload_padded_to_512(tmp_path: Path):
    spec = {
        "version": 1,
        "content_version": 0,
        "buffers": [],
        "textures": [
            {
                "name": "Tiny2x2",
                "texture_type": 3,
                "compression_type": 0,
                "width": 2,
                "height": 2,
                "depth": 1,
                "array_layers": 1,
                "mip_levels": 1,
                "format": 30,
                "alignment": 256,
                # 4 RGBA8 texels (16 bytes) without row padding.
                "data_hex": "ff" * 16,
            }
        ],
        "audios": [],
        "assets": [],
    }

    build = build_plan(spec, tmp_path)

    assert build.resources.index_map["texture"]["Tiny2x2"] == 1
    assert build.resources.desc_fields["texture"][1]["name"] == "Tiny2x2"
    assert len(build.resources.data_blobs["texture"][1]) == 512
