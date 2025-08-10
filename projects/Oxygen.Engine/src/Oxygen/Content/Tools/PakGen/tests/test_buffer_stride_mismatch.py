import json
from pathlib import Path
import pytest
from pakgen.api import build_pak, BuildOptions


def write_bad_spec(tmp_path: Path) -> Path:
    # stride=8 but provide 12 bytes (not multiple of 8)
    spec = {
        "version": 1,
        "buffers": [
            {
                "name": "bad",
                "stride": 8,
                "data_hex": "000102030405060708090A0B",
            },
        ],
        "textures": [],
        "audios": [],
        "assets": [],
    }
    path = tmp_path / "bad_stride.json"
    path.write_text(json.dumps(spec), encoding="utf-8")
    return path


def write_good_spec(tmp_path: Path) -> Path:
    # stride=8 with 16 bytes OK
    spec = {
        "version": 1,
        "buffers": [
            {
                "name": "good",
                "stride": 8,
                "data_hex": "000102030405060708090A0B0C0D0E0F",
            },
        ],
        "textures": [],
        "audios": [],
        "assets": [],
    }
    path = tmp_path / "good_stride.json"
    path.write_text(json.dumps(spec), encoding="utf-8")
    return path


def test_stride_mismatch_rejected(tmp_path: Path):
    bad_path = write_bad_spec(tmp_path)
    out_pak = tmp_path / "out_bad.pak"
    with pytest.raises(ValueError) as exc:
        build_pak(
            BuildOptions(
                input_spec=bad_path, output_path=out_pak, deterministic=True
            )
        )
    assert "E_STRIDE_MULT" in str(exc.value)


def test_stride_good_builds(tmp_path: Path):
    good_path = write_good_spec(tmp_path)
    out_pak = tmp_path / "out_good.pak"
    res = build_pak(
        BuildOptions(
            input_spec=good_path, output_path=out_pak, deterministic=True
        )
    )
    assert out_pak.exists() and res.bytes_written > 0
