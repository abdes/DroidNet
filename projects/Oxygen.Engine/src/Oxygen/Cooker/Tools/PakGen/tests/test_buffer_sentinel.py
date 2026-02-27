import struct
import json
from pathlib import Path
from pakgen.api import build_pak, BuildOptions

def test_buffer_sentinel_descriptor_is_zeroed(tmp_path: Path):
    """Verify that buffer index 0 is reserved as an all-zero sentinel in the PAK."""
    spec = {
        "version": 7,
        "content_version": 1,
        "buffers": [
            {"name": "user_buf", "stride": 4, "data_hex": "BBBBBBBB"}
        ],
        "textures": [],
        "audios": [],
        "assets": []
    }
    spec_path = tmp_path / "spec.json"
    spec_path.write_text(json.dumps(spec))

    out_pak = tmp_path / "out.pak"
    build_pak(BuildOptions(input_spec=spec_path, output_path=out_pak))

    data = out_pak.read_bytes()
    footer = data[-256:]

    # Extract buffer table info from footer (v7 layout):
    # directory header (24)
    # + 5 resource regions (texture, buffer, audio, script, physics) => 5 * 16 = 80
    # + texture table (16)
    # => buffer table starts at 24 + 80 + 16 = 120
    # Each table record is (offset: Q, count: I, entry_size: I) = 16 bytes.
    footer_buffer_table_offset = 24 + (5 * 16) + 16
    (buffer_table_offset, buffer_table_count, buffer_entry_size) = struct.unpack_from(
        "<QII", footer, footer_buffer_table_offset
    )

    assert buffer_table_count == 2, "Should have 2 entries (sentinel + user buffer)"
    assert buffer_entry_size == 32

    # Read first entry (sentinel)
    sentinel_bytes = data[buffer_table_offset : buffer_table_offset + 32]
    # BufferResourceDesc: data_offset (Q), size_bytes (I), usage_flags (I),
    # element_stride (I), element_format (B), content_hash (Q), reserved (3B)
    unpacked_sentinel = struct.unpack("<QIII B Q 3s", sentinel_bytes)

    # Verify sentinel is all-zero
    assert unpacked_sentinel[0] == 0, "Sentinel data_offset must be 0"
    assert unpacked_sentinel[1] == 0, "Sentinel size_bytes must be 0"
    assert unpacked_sentinel[2] == 0, "Sentinel usage_flags must be 0"
    assert unpacked_sentinel[3] == 0, "Sentinel element_stride must be 0"
    assert unpacked_sentinel[4] == 0, "Sentinel element_format must be 0"
    assert unpacked_sentinel[5] == 0, "Sentinel content_hash must be 0"

    # Read second entry (user buffer)
    user_buf_bytes = data[buffer_table_offset + 32 : buffer_table_offset + 64]
    unpacked_user = struct.unpack("<QIII B Q 3s", user_buf_bytes)

    assert unpacked_user[0] != 0, "User buffer must have a non-zero offset"
    assert unpacked_user[1] == 4, "User buffer size mismatch"
    assert unpacked_user[3] == 4, "User buffer stride mismatch"
