import binascii
from pathlib import Path
from pakgen.api import build_pak, BuildOptions


def test_build_minimal_pak(tmp_path: Path):
    spec = {
        "version": 1,
        "content_version": 7,
        "buffers": [],
        "textures": [],
        "audios": [],
        "assets": [
            {
                "type": "material",
                "name": "MatA",
                "asset_key": "11" * 16,
                "alignment": 1,
            },
            {
                "type": "geometry",
                "name": "GeoA",
                "lods": [
                    {
                        "vertex_count": 3,
                        "index_count": 3,
                        "mesh_type": 0,
                        "submeshes": [
                            {
                                "material": "MatA",
                                "mesh_views": [
                                    {
                                        "first_index": 0,
                                        "index_count": 3,
                                        "first_vertex": 0,
                                        "vertex_count": 3,
                                    }
                                ],
                            }
                        ],
                    }
                ],
            },
        ],
    }
    spec_path = tmp_path / "spec.json"
    spec_path.write_text(__import__("json").dumps(spec))
    out_path = tmp_path / "out.pak"
    res = build_pak(BuildOptions(input_spec=spec_path, output_path=out_path))
    data = out_path.read_bytes()
    assert res.bytes_written == len(data)
    assert len(data) > 256
    footer = data[-256:]
    directory_offset = int.from_bytes(footer[0:8], "little")
    directory_size = int.from_bytes(footer[8:16], "little")
    asset_count = int.from_bytes(footer[16:24], "little")
    assert asset_count == 2
    assert directory_offset != 0
    assert directory_size >= 64
    crc_field_offset = len(data) - 12
    import zlib

    expected_crc = (
        zlib.crc32(data[:crc_field_offset] + data[crc_field_offset + 4 :])
        & 0xFFFFFFFF
    )
    stored_crc = int.from_bytes(data[-12:-8], "little")
    assert stored_crc == expected_crc
