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
    # Accept both legacy and convenience keys.
    # - 'file': relative path to a file
    # - 'path': alias for 'file' used by some specs/tests
    sources: list[str] = []
    if "data_hex" in entry:
        sources.append("data_hex")
    # Treat null file/path as "not provided" so callers can include it as a
    # placeholder without conflicting with data_hex/data.
    if "file" in entry and entry.get("file") is not None:
        sources.append("file")
    if "path" in entry and entry.get("path") is not None:
        sources.append("path")
    if "data" in entry:
        sources.append("data")

    # Special-case: allow explicit zero-length resources without a data source.
    # This supports specs that declare 'size: 0' (and optionally 'path: null').
    if not sources:
        size_field = (
            entry.get("size") or entry.get("data_size") or entry.get("length")
        )
        if size_field in (None, 0):
            return b""
        raise DataError("No data source (data_hex|file|path|data) provided")
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
    if src in ("file", "path"):
        p = entry[src]
        if p is None:
            # Common pattern for zero-length placeholders.
            size_field = (
                entry.get("size")
                or entry.get("data_size")
                or entry.get("length")
            )
            if size_field in (None, 0):
                return b""
            raise DataError(f"{src} is null for non-zero-sized resource")
        if not isinstance(p, str):
            raise DataError(f"{src} path must be string")
        resolved = safe_file_path(base_dir, p)
        return safe_read_file(resolved, max_size)
    # src == 'data'
    d = entry["data"]
    if isinstance(d, str):
        return d.encode("utf-8")
    if isinstance(d, bytes):
        return d
    raise DataError("data must be str or bytes")
