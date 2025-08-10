from pathlib import Path
import json
import struct

from pakgen.api import build_pak, BuildOptions, inspect_pak
from pakgen.spec.validator import run_binary_validation


def _write_spec(tmp: Path, name: str, spec: dict) -> Path:
    p = tmp / f"{name}.json"
    p.write_text(json.dumps(spec), encoding="utf-8")
    return p


def _tamper_descriptor_size(pak_path: Path):
    data = bytearray(pak_path.read_bytes())
    # Locate directory (last 256 footer; read directory offset/size at start of footer)
    footer_off = len(data) - 256
    directory_offset, directory_size, asset_count = struct.unpack_from(
        "<QQQ", data, footer_off
    )
    if asset_count == 0:
        return
    # First directory entry desc_size is at offset: key(16) + asset_type(1)+entry_offset(8)+desc_offset(8) = 33 bytes into entry
    first_entry_off = directory_offset
    desc_size_off = first_entry_off + 16 + 1 + 8 + 8
    # Force desc_size to tiny (e.g., 8) to trigger E_BIN_DESC
    struct.pack_into("<I", data, desc_size_off, 8)
    pak_path.write_bytes(data)


def test_binary_validation_descriptor(tmp_path: Path):  # noqa: N802
    spec = {
        "version": 1,
        "assets": [{"type": "material", "name": "m0"}],
        "textures": [],
        "buffers": [],
        "audios": [],
    }
    sp = _write_spec(tmp_path, "spec", spec)
    out = tmp_path / "test.pak"
    build_pak(BuildOptions(input_spec=sp, output_path=out, force=True))
    info = inspect_pak(out)
    # Should be clean initially
    errs_clean = run_binary_validation(spec, info)
    assert not any(e.code.startswith("E_BIN") for e in errs_clean)
    # Tamper file
    _tamper_descriptor_size(out)
    info_tampered = inspect_pak(out)
    errs = run_binary_validation(spec, info_tampered)
    codes = {e.code for e in errs}
    assert "E_BIN_DESC" in codes
