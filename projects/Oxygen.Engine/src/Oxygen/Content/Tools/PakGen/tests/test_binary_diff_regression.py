from pathlib import Path
import hashlib
import json
from pakgen.api import BuildOptions, build_pak


def _read_bytes(path: Path) -> bytes:
    return path.read_bytes()


def _hexdigest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def test_binary_diff_regression(tmp_path: Path):  # noqa: N802
    """Build minimal spec and compare bytes to committed golden PAK.

    Ensures writer refactor maintains bitwise stability per Phase 6 exit criteria.
    On mismatch prints first differing offset and short context window plus hashes.
    """
    repo_root = Path(__file__).parent
    golden_spec = repo_root / "_golden" / "minimal_spec.json"
    golden_pak = repo_root / "_golden" / "minimal_ref.pak"
    assert golden_spec.exists(), "Golden spec missing"
    assert (
        golden_pak.exists()
    ), "Golden pak missing (run build_minimal_ref.py to regenerate intentionally)"

    # Rebuild using current code
    spec_copy = tmp_path / "spec.json"
    spec_copy.write_bytes(golden_spec.read_bytes())
    out_pak = tmp_path / "out_minimal.pak"
    build_pak(BuildOptions(input_spec=spec_copy, output_path=out_pak))

    golden_bytes = _read_bytes(golden_pak)
    new_bytes = _read_bytes(out_pak)

    if golden_bytes != new_bytes:
        # Find first diff
        limit = min(len(golden_bytes), len(new_bytes))
        diff_index = None
        for i in range(limit):
            if golden_bytes[i] != new_bytes[i]:
                diff_index = i
                break
        if diff_index is None and len(golden_bytes) != len(new_bytes):
            diff_index = limit
        ctx_start = max(0, (diff_index or 0) - 16)
        ctx_end = min(limit, (diff_index or 0) + 16)
        golden_slice = golden_bytes[ctx_start:ctx_end].hex()
        new_slice = new_bytes[ctx_start:ctx_end].hex()
        msg = (
            "Binary diff regression: first difference at offset {}\n"
            "Golden SHA256: {}\nNew    SHA256: {}\n"
            "Golden slice[{}:{}]: {}\nNew    slice[{}:{}]: {}".format(
                diff_index,
                _hexdigest(golden_bytes),
                _hexdigest(new_bytes),
                ctx_start,
                ctx_end,
                golden_slice,
                ctx_start,
                ctx_end,
                new_slice,
            )
        )
        raise AssertionError(msg)

    # Hash equality assertion for clarity
    assert _hexdigest(golden_bytes) == _hexdigest(new_bytes)
