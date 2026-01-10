from pathlib import Path
import struct

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

    payload = build.resources.data_blobs["texture"][1]
    assert payload[:4] == b"OTX1"

    magic, policy, flags, sub_count, total, layouts_off, data_off, _ = (
        struct.unpack_from("<IBBHIIIQ", payload, 0)
    )
    assert magic == 0x3158544F
    assert policy == 1  # D3D12
    assert flags == 0
    assert sub_count == 1
    assert total == len(payload)
    assert layouts_off == 28
    assert data_off == 512

    off_bytes, row_pitch, size_bytes = struct.unpack_from(
        "<III", payload, layouts_off
    )
    assert off_bytes == 0
    assert row_pitch == 256
    assert size_bytes == 256
    assert payload[data_off : data_off + 4] == b"\xff\xff\xff\xff"


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

    payload = build.resources.data_blobs["texture"][1]
    assert payload[:4] == b"OTX1"

    magic, policy, flags, sub_count, total, layouts_off, data_off, _ = (
        struct.unpack_from("<IBBHIIIQ", payload, 0)
    )
    assert magic == 0x3158544F
    assert policy == 1  # D3D12
    assert flags == 0
    assert sub_count == 1
    assert total == len(payload)
    assert layouts_off == 28
    assert data_off == 512

    off_bytes, row_pitch, size_bytes = struct.unpack_from(
        "<III", payload, layouts_off
    )
    assert off_bytes == 0
    assert row_pitch == 256
    assert size_bytes == 512

    # Ensure second row starts at row_pitch (not tightly packed).
    data = payload[data_off : data_off + size_bytes]
    assert data[0:8] == b"\xff" * 8
    assert data[8:row_pitch] == b"\x00" * (row_pitch - 8)
    assert data[row_pitch : row_pitch + 8] == b"\xff" * 8
