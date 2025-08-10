"""IO helpers for spec resource data extraction."""

from __future__ import annotations
from pathlib import Path
from typing import Any

from ..packing.constants import MAX_HEX_STRING_LENGTH
from .paths import safe_file_path

__all__ = ["safe_read_file", "read_data_from_spec"]


class DataError(RuntimeError):
    pass


def safe_read_file(path: Path, max_size: int = 100 * 1024 * 1024) -> bytes:
    if not path.exists():
        raise DataError(f"File not found: {path}")
    size = path.stat().st_size
    if size > max_size:
        raise DataError(f"File too large: {size}>{max_size}")
    return path.read_bytes()


def read_data_from_spec(
    entry: dict[str, Any], base_dir: Path, max_size: int = 100 * 1024 * 1024
) -> bytes:
    sources = [k for k in ("data_hex", "file", "data") if k in entry]
    if not sources:
        raise DataError("No data source (data_hex|file|data) provided")
    if len(sources) > 1:
        raise DataError(f"Multiple data sources: {sources}")
    src = sources[0]
    if src == "data_hex":
        raw = entry["data_hex"]
        if not isinstance(raw, str):
            raise DataError("data_hex must be string")
        h = raw.replace(" ", "").replace("\n", "")
        if len(h) > MAX_HEX_STRING_LENGTH:
            raise DataError("hex string too long")
        if len(h) % 2:
            raise DataError("hex string must have even length")
        try:
            return bytes.fromhex(h)
        except ValueError as e:
            raise DataError(f"invalid hex: {e}") from e
    if src == "file":
        p = entry["file"]
        if not isinstance(p, str):
            raise DataError("file path must be string")
        resolved = safe_file_path(base_dir, p)
        return safe_read_file(resolved, max_size)
    # src == 'data'
    d = entry["data"]
    if isinstance(d, str):
        return d.encode("utf-8")
    if isinstance(d, bytes):
        return d
    raise DataError("data must be str or bytes")
