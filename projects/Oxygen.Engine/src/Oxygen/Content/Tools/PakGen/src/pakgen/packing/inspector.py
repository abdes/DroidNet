"""Binary PAK inspection utilities (refactored from legacy inspector.py).

Public functions:
- inspect_pak(path) -> dict
- validate_pak(info) -> list[str]

Design changes:
- Uses central constants/errors modules.
- No CLI symbols; CLI handled in top-level cli.py.
- Structured Region/Table kept as dataclasses with slots.
"""

from __future__ import annotations

from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Any, Dict, List, Tuple
import struct
import zlib

from .constants import (
    MAGIC,
    FOOTER_MAGIC,
    MATERIAL_DESC_SIZE,
    GEOMETRY_DESC_SIZE,
    SCENE_DESC_SIZE,
    DIRECTORY_ENTRY_SIZE,
    ASSET_KEY_SIZE,
    FOOTER_SIZE,
)

__all__ = [
    "Region",
    "Table",
    "inspect_pak",
    "validate_pak",
    "parse_header",
    "parse_footer",
    "compute_crc32",
]


@dataclass(slots=True)
class Region:
    offset: int
    size: int


@dataclass(slots=True)
class Table:
    offset: int
    count: int
    entry_size: int


def _read_exact(data: bytes, offset: int, size: int, label: str) -> bytes:
    end = offset + size
    if end > len(data):
        raise ValueError(
            f"Out of range read for {label}: {offset}+{size}>{len(data)}"
        )
    return data[offset:end]


def parse_footer(data: bytes) -> Dict[str, Any]:
    footer_offset = len(data) - FOOTER_SIZE
    raw = _read_exact(data, footer_offset, FOOTER_SIZE, "footer")
    directory_offset, directory_size, asset_count = struct.unpack_from(
        "<QQQ", raw, 0
    )
    off = 24

    def unpack_region(o: int) -> Tuple[int, int]:
        return struct.unpack_from("<QQ", raw, o)

    texture_region = unpack_region(off)
    off += 16
    buffer_region = unpack_region(off)
    off += 16
    audio_region = unpack_region(off)
    off += 16

    def unpack_table(o: int) -> Tuple[int, int, int]:
        return struct.unpack_from("<QII", raw, o)

    texture_table = unpack_table(off)
    off += 16
    buffer_table = unpack_table(off)
    off += 16
    audio_table = unpack_table(off)
    off += 16

    browse_index_offset, browse_index_size = struct.unpack_from("<QQ", raw, off)
    off += 16
    reserved = raw[off : off + 108]
    off += 108
    pak_crc32 = struct.unpack_from("<I", raw, off)[0]
    off += 4
    magic = raw[off : off + len(FOOTER_MAGIC)]
    return {
        "offset": footer_offset,
        "directory": {
            "offset": directory_offset,
            "size": directory_size,
            "asset_count": asset_count,
        },
        "regions": {
            "texture": Region(*texture_region),
            "buffer": Region(*buffer_region),
            "audio": Region(*audio_region),
        },
        "tables": {
            "texture": Table(*texture_table),
            "buffer": Table(*buffer_table),
            "audio": Table(*audio_table),
        },
        "browse_index": {
            "offset": browse_index_offset,
            "size": browse_index_size,
        },
        "pak_crc32": pak_crc32,
        "reserved_zero": all(b == 0 for b in reserved),
        "magic_ok": magic == FOOTER_MAGIC,
    }


def parse_header(data: bytes) -> Dict[str, Any]:
    raw = _read_exact(data, 0, 64, "header")
    magic, version, content_version = struct.unpack_from("<8sHH", raw, 0)
    return {
        "magic_ok": magic == MAGIC,
        "version": version,
        "content_version": content_version,
    }


def compute_crc32(data: bytes) -> int:
    crc_field_offset = len(data) - 12
    content_before = data[:crc_field_offset]
    content_after = data[crc_field_offset + 4 :]
    return zlib.crc32(content_before + content_after) & 0xFFFFFFFF


def inspect_pak(path: str | Path) -> Dict[str, Any]:
    p = Path(path)
    data = p.read_bytes()
    header = parse_header(data)
    footer = parse_footer(data)
    real_crc_offset = len(data) - 12
    real_crc32 = struct.unpack_from("<I", data, real_crc_offset)[0]
    crc_calc = compute_crc32(data)
    footer_crc_match = crc_calc == real_crc32
    result: Dict[str, Any] = {
        "file_size": len(data),
        "header": header,
        "footer": {
            **footer,
            "regions": {k: asdict(v) for k, v in footer["regions"].items()},
            "tables": {k: asdict(v) for k, v in footer["tables"].items()},
            "crc_match": footer_crc_match,
            "crc_calculated": crc_calc,
            "real_crc32": real_crc32,
        },
    }
    dir_off = footer["directory"]["offset"]
    dir_size = footer["directory"]["size"]
    if dir_size % DIRECTORY_ENTRY_SIZE == 0 and dir_off + dir_size <= len(data):
        entries = []
        for i in range(dir_size // DIRECTORY_ENTRY_SIZE):
            e_off = dir_off + i * DIRECTORY_ENTRY_SIZE
            entry = _read_exact(data, e_off, DIRECTORY_ENTRY_SIZE, f"dir[{i}]")
            key_bytes = entry[:ASSET_KEY_SIZE]
            key = key_bytes.rstrip(b"\x00").hex()
            asset_type, entry_offset, desc_offset, desc_size = (
                struct.unpack_from("<BQQI", entry, ASSET_KEY_SIZE)
            )
            entries.append(
                {
                    "key": key,
                    "asset_type": asset_type,
                    "entry_offset": entry_offset,
                    "desc_offset": desc_offset,
                    "desc_size": desc_size,
                }
            )
        result["directory_entries"] = entries
    return result


def validate_pak(info: Dict[str, Any]) -> List[str]:
    issues: List[str] = []
    if not info["header"]["magic_ok"]:
        issues.append("Header magic mismatch")
    footer = info["footer"]
    if not footer["magic_ok"]:
        issues.append("Footer magic mismatch")
    if not footer["crc_match"]:
        issues.append("CRC mismatch")
    regions = footer["regions"]
    tables = footer["tables"]
    file_size = info["file_size"]
    for name, r in regions.items():
        if r["offset"] or r["size"]:
            if r["offset"] + r["size"] > file_size:
                issues.append(f"Region {name} exceeds file size")
    for name, t in tables.items():
        if t["offset"]:
            table_span = t["offset"] + t["count"] * t["entry_size"]
            if table_span > file_size:
                issues.append(f"Table {name} exceeds file size")
    dir_meta = footer["directory"]
    dir_end = dir_meta["offset"] + dir_meta["size"]
    if dir_end > file_size:
        issues.append("Directory exceeds file size")
    if dir_meta["size"] // DIRECTORY_ENTRY_SIZE != dir_meta["asset_count"]:
        issues.append("Directory size/count mismatch")
    for e in info.get("directory_entries", []):
        if e["desc_size"] in (
            MATERIAL_DESC_SIZE,
            GEOMETRY_DESC_SIZE,
            SCENE_DESC_SIZE,
        ):
            pass
        elif e["desc_size"] < 64:
            issues.append(f"Descriptor size too small for asset {e['key']}")
    return issues
