from __future__ import annotations

from pathlib import Path

from pakgen.api import BuildOptions, build_pak


def _read_pak_guid(path: Path) -> bytes:
    data = path.read_bytes()
    # PakHeader layout: <8sHH16s36s> so GUID is at byte offset 12.
    return data[12:28]


def test_deterministic_pak_guid_unique_and_nonzero(
    tmp_path: Path,
):  # noqa: N802
    # Two different specs must not end up with the same PakHeader GUID.
    spec_a = tmp_path / "a.json"
    spec_b = tmp_path / "b.json"

    # Minimal valid specs (same version/content_version, different buffer name).
    spec_a.write_text(
        "{"
        '"version":1,'
        '"content_version":1,'
        '"buffers":[{"name":"buf_a","usage":0,"stride":0,"format":0,"data_hex":""}],'
        '"textures":[],"audios":[],"assets":[]'
        "}",
        encoding="utf-8",
    )
    spec_b.write_text(
        "{"
        '"version":1,'
        '"content_version":1,'
        '"buffers":[{"name":"buf_b","usage":0,"stride":0,"format":0,"data_hex":""}],'
        '"textures":[],"audios":[],"assets":[]'
        "}",
        encoding="utf-8",
    )

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
