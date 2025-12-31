from pathlib import Path
import hashlib
import json
from pakgen.api import BuildOptions, build_pak, inspect_pak


def _read_bytes(path: Path) -> bytes:
    return path.read_bytes()


def _hexdigest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def test_binary_diff_regression(tmp_path: Path):  # noqa: N802
    """Build minimal spec and validate structural compatibility.

    The PAK format now includes an optional embedded browse index blob (OXPAKBIX)
    inserted between the directory and footer. That intentionally changes the
    byte layout vs historical golden files.

    This test keeps a strong regression signal by asserting:
    - Byte stability for all content *before* the directory end.
    - Directory entries remain identical.
    - New output contains a valid browse index referenced by the footer.
    """
    repo_root = Path(__file__).parent
    golden_spec = repo_root / "_golden" / "minimal_spec.json"
    if not golden_spec.exists():
        golden_spec = repo_root / "_golden" / "minimal_spec.yaml"
    golden_pak = repo_root / "_golden" / "minimal_ref.pak"
    assert golden_spec.exists(), "Golden spec missing"
    assert (
        golden_pak.exists()
    ), "Golden pak missing (run build_minimal_ref.py to regenerate intentionally)"

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

    assert golden_info["header"] == new_info["header"]
    assert golden_info["footer"]["directory"] == new_info["footer"]["directory"]
    assert golden_info.get("directory_entries", []) == new_info.get(
        "directory_entries", []
    )

    golden_bytes = _read_bytes(golden_pak)
    new_bytes = _read_bytes(out_pak)

    directory = new_info["footer"]["directory"]
    directory_end = directory["offset"] + directory["size"]
    assert golden_bytes[:directory_end] == new_bytes[:directory_end]

    browse = new_info["footer"].get("browse_index") or {"offset": 0, "size": 0}
    assert browse["size"] > 0
    bix_offset = browse["offset"]
    bix_size = browse["size"]
    assert new_bytes[bix_offset : bix_offset + 8] == b"OXPAKBIX"
    assert bix_offset == directory_end
    assert bix_offset + bix_size == new_info["footer"]["offset"]

    # Sanity: new file integrity should validate.
    assert new_info["footer"]["crc_match"]
