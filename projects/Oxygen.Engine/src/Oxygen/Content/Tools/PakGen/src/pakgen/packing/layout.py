"""Low-level layout helpers (alignment, name packing)."""

from __future__ import annotations
from typing import BinaryIO

from .constants import ASSET_NAME_MAX_LENGTH

__all__ = [
    "pack_name_string"
]  # align_file removed (Phase 6 plan-driven writer)


def pack_name_string(name: str, size: int) -> bytes:
    name_bytes = name.encode("utf-8")[: size - 1]
    if len(name_bytes) >= ASSET_NAME_MAX_LENGTH:
        name_bytes = name_bytes[: ASSET_NAME_MAX_LENGTH - 1]
    return name_bytes + b"\x00" * (size - len(name_bytes))
