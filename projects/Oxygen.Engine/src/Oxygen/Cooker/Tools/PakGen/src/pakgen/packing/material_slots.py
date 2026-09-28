"""Canonical identifiers used by explicit material-slot fixtures."""

from uuid import UUID

from .errors import PakError


def slot_id_bytes(value: object) -> bytes:
    if not isinstance(value, str):
        raise PakError("E_SLOT_ID", "slot_id must be a canonical non-nil UUID")
    try:
        identifier = UUID(value)
    except ValueError as error:
        raise PakError("E_SLOT_ID", "slot_id must be a canonical non-nil UUID") from error
    if identifier.int == 0 or str(identifier) != value:
        raise PakError("E_SLOT_ID", "slot_id must be a canonical non-nil UUID")
    return identifier.bytes


def layout_revision_bytes(value: object) -> bytes:
    if not isinstance(value, str) or len(value) != 64:
        raise PakError("E_SLOT_LAYOUT", "layout_revision must be 64 lowercase hex digits")
    try:
        digest = bytes.fromhex(value)
    except ValueError as error:
        raise PakError("E_SLOT_LAYOUT", "layout_revision must be 64 lowercase hex digits") from error
    if len(digest) != 32 or digest.hex() != value:
        raise PakError("E_SLOT_LAYOUT", "layout_revision must be 64 lowercase hex digits")
    return digest
