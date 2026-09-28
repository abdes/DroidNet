from pathlib import Path
from pakgen.api import BuildOptions, build_pak, inspect_pak
from pakgen.packing.constants import PAK_FORMAT_VERSION_CURRENT


def test_binary_diff_regression(tmp_path: Path):  # noqa: N802
    """Reproduce canonical golden bytes and validate browse-index placement."""
    repo_root = Path(__file__).parent
    golden_spec = repo_root / "_golden" / "minimal_spec.json"
    if not golden_spec.exists():
        golden_spec = repo_root / "_golden" / "minimal_spec.yaml"
    golden_pak = repo_root / "_golden" / "minimal_ref.pak"
    assert golden_spec.exists(), "Golden spec missing"
    assert (
        golden_pak.exists()
    ), "Golden pak missing (run _golden/build_ref_paks.py to regenerate)"

    # Rebuild using current code
    spec_copy = tmp_path / f"spec{golden_spec.suffix}"
    spec_copy.write_bytes(golden_spec.read_bytes())
    out_pak = tmp_path / "out_minimal.pak"
    build_pak(
        BuildOptions(
            input_spec=spec_copy,
            output_path=out_pak,
            deterministic=True,
        )
    )

    golden_info = inspect_pak(golden_pak)
    new_info = inspect_pak(out_pak)

    assert golden_info["header"]["magic_ok"] == new_info["header"]["magic_ok"]
    assert (
        golden_info["header"]["content_version"]
        == new_info["header"]["content_version"]
    )
    assert new_info["header"]["version"] == PAK_FORMAT_VERSION_CURRENT
    assert golden_info["footer"]["directory"] == new_info["footer"]["directory"]
    assert golden_info.get("directory_entries", []) == new_info.get(
        "directory_entries", []
    )

    golden_bytes = golden_pak.read_bytes()
    new_bytes = out_pak.read_bytes()

    directory = new_info["footer"]["directory"]
    directory_end = directory["offset"] + directory["size"]
    assert golden_bytes == new_bytes

    browse = new_info["footer"].get("browse_index") or {"offset": 0, "size": 0}
    assert browse["size"] > 0
    bix_offset = browse["offset"]
    bix_size = browse["size"]
    assert new_bytes[bix_offset : bix_offset + 8] == b"OXPAKBIX"
    assert bix_offset == directory_end
    assert bix_offset + bix_size == new_info["footer"]["offset"]

    # Sanity: new file integrity should validate.
    assert new_info["footer"]["crc_match"]
