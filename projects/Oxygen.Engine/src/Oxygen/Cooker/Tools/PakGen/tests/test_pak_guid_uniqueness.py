from __future__ import annotations

import json
from pathlib import Path

from pakgen.api import BuildOptions, build_pak


def _read_pak_guid(path: Path) -> bytes:
    data = path.read_bytes()
    # PakHeader layout: <8sHH16s36s> so GUID is at byte offset 12.
    return data[12:28]


def _write_minimal_spec(path: Path, buffer_name: str) -> None:
    spec = {
        "version": 6,
        "content_version": 1,
        "buffers": [
            {
                "name": buffer_name,
                "usage": 0,
                "stride": 0,
                "format": 0,
                "data_hex": "",
            }
        ],
        "textures": [],
        "audios": [],
        "assets": [],
    }
    path.write_text(json.dumps(spec), encoding="utf-8")


def test_deterministic_pak_guid_unique_and_nonzero(
    tmp_path: Path,
):  # noqa: N802
    # Two different specs must not end up with the same PakHeader GUID.
    spec_a = tmp_path / "a.json"
    spec_b = tmp_path / "b.json"
    _write_minimal_spec(spec_a, "buf_a")
    _write_minimal_spec(spec_b, "buf_b")

    out_a = tmp_path / "a.pak"
    out_b = tmp_path / "b.pak"

    build_pak(
        BuildOptions(
            input_spec=spec_a,
            output_path=out_a,
            deterministic=True,
            force=True,
        )
    )
    build_pak(
        BuildOptions(
            input_spec=spec_b,
            output_path=out_b,
            deterministic=True,
            force=True,
        )
    )

    guid_a = _read_pak_guid(out_a)
    guid_b = _read_pak_guid(out_b)

    assert guid_a != b"\x00" * 16
    assert guid_b != b"\x00" * 16
    assert guid_a != guid_b


def test_deterministic_pak_guid_reproducible_for_repeated_runs(
    tmp_path: Path,
):  # noqa: N802
    spec = tmp_path / "stable.json"
    _write_minimal_spec(spec, "stable_buf")

    out_a = tmp_path / "stable_a.pak"
    out_b = tmp_path / "stable_b.pak"
    out_c = tmp_path / "stable_c.pak"

    for out in (out_a, out_b, out_c):
        build_pak(
            BuildOptions(
                input_spec=spec,
                output_path=out,
                deterministic=True,
                force=True,
            )
        )

    guid_a = _read_pak_guid(out_a)
    guid_b = _read_pak_guid(out_b)
    guid_c = _read_pak_guid(out_c)

    assert guid_a == guid_b == guid_c
    assert guid_a != b"\x00" * 16


def test_deterministic_pak_guid_is_stable_for_normalized_equivalent_specs(
    tmp_path: Path,
):  # noqa: N802
    # Same semantic content with different JSON key order/whitespace must map to
    # the same deterministic GUID because request-construction normalizes input.
    spec_a = tmp_path / "equiv_a.json"
    spec_b = tmp_path / "equiv_b.json"

    spec_a_dict = {
        "assets": [],
        "audios": [],
        "textures": [],
        "buffers": [
            {
                "usage": 0,
                "name": "same_buf",
                "stride": 0,
                "format": 0,
                "data_hex": "",
            }
        ],
        "content_version": 1,
        "version": 6,
    }
    spec_b_dict = {
        "version": 6,
        "content_version": 1,
        "buffers": [
            {
                "name": "same_buf",
                "usage": 0,
                "stride": 0,
                "format": 0,
                "data_hex": "",
            }
        ],
        "textures": [],
        "audios": [],
        "assets": [],
    }

    spec_a.write_text(
        json.dumps(spec_a_dict, indent=2),
        encoding="utf-8",
    )
    spec_b.write_text(
        json.dumps(spec_b_dict, separators=(",", ":")),
        encoding="utf-8",
    )

    out_a = tmp_path / "equiv_a.pak"
    out_b = tmp_path / "equiv_b.pak"

    build_pak(
        BuildOptions(
            input_spec=spec_a,
            output_path=out_a,
            deterministic=True,
            force=True,
        )
    )
    build_pak(
        BuildOptions(
            input_spec=spec_b,
            output_path=out_b,
            deterministic=True,
            force=True,
        )
    )

    guid_a = _read_pak_guid(out_a)
    guid_b = _read_pak_guid(out_b)

    assert guid_a == guid_b
    assert guid_a != b"\x00" * 16
