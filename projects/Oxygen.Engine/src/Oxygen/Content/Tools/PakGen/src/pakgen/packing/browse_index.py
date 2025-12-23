"""PAK embedded browse index (OXPAKBIX) helpers.

The browse index maps canonical virtual paths to AssetKeys for editor/tooling
use. PakGen derives virtual paths from asset names as `"/" + name`.

This module is shared by both the planner (size calculation) and the writer
(byte emission) to guarantee the produced payload matches the plan.
"""

from __future__ import annotations

from dataclasses import dataclass
import struct
from typing import Iterable, List, Tuple


_BIX_MAGIC = b"OXPAKBIX"
_BIX_VERSION = 1


@dataclass(frozen=True, slots=True)
class BrowseIndexEntrySpec:
    asset_key: bytes
    virtual_path: str


def _validate_virtual_path(virtual_path: str) -> None:
    if not virtual_path:
        raise ValueError("Virtual path must not be empty")
    if "\\" in virtual_path:
        raise ValueError("Virtual path must use '/' as the separator")
    if not virtual_path.startswith("/"):
        raise ValueError("Virtual path must start with '/'")
    if virtual_path != "/" and virtual_path.endswith("/"):
        raise ValueError("Virtual path must not end with '/' (except the root)")
    if "//" in virtual_path:
        raise ValueError("Virtual path must not contain '//'")

    # Segment rules: forbid '.' and '..' path segments.
    pos = 0
    while pos <= len(virtual_path):
        next_sep = virtual_path.find("/", pos)
        end = len(virtual_path) if next_sep == -1 else next_sep
        segment = virtual_path[pos:end]
        if segment == ".":
            raise ValueError("Virtual path must not contain '.'")
        if segment == "..":
            raise ValueError("Virtual path must not contain '..'")
        if next_sep == -1:
            break
        pos = next_sep + 1


def derive_virtual_path_from_asset_name(name: str) -> str:
    if not isinstance(name, str) or not name:
        raise ValueError("Asset name is required to derive a virtual path")
    # PakGen's simplest convention: one asset name per virtual path.
    virtual_path = "/" + name
    _validate_virtual_path(virtual_path)
    return virtual_path


def build_browse_index_payload(
    entries: Iterable[BrowseIndexEntrySpec],
) -> Tuple[bytes, int, int]:
    """Build an OXPAKBIX payload.

    Returns (payload_bytes, entry_count, string_table_size).
    """

    normalized: List[Tuple[bytes, bytes]] = []
    seen_paths: set[str] = set()

    for e in entries:
        if len(e.asset_key) != 16:
            raise ValueError(
                f"Asset key must be 16 bytes (got {len(e.asset_key)})"
            )
        _validate_virtual_path(e.virtual_path)
        if e.virtual_path in seen_paths:
            raise ValueError(f"Duplicate virtual path: {e.virtual_path}")
        seen_paths.add(e.virtual_path)
        normalized.append((e.asset_key, e.virtual_path.encode("utf-8")))

    string_table = b""
    entry_records = b""

    for asset_key, path_bytes in normalized:
        offset = len(string_table)
        length = len(path_bytes)
        if offset > 0xFFFFFFFF or length > 0xFFFFFFFF:
            raise ValueError("Browse index string table exceeds 4GiB")
        string_table += path_bytes
        entry_records += asset_key + struct.pack("<II", offset, length)

    entry_count = len(normalized)
    string_table_size = len(string_table)

    header = struct.pack(
        "<8sIIII",
        _BIX_MAGIC,
        _BIX_VERSION,
        entry_count,
        string_table_size,
        0,
    )

    payload = header + entry_records + string_table
    return payload, entry_count, string_table_size
