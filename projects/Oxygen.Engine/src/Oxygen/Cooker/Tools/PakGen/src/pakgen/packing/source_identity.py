"""Required caller-owned UUIDv7 identities for native content sources."""

from typing import Any
from uuid import RFC_4122, UUID

from .errors import PakError


def source_identity_bytes(value: Any) -> bytes:
    """Validate canonical lowercase UUIDv7 text without generating an identity."""
    if not isinstance(value, str):
        raise PakError(
            "E_SOURCE_IDENTITY",
            "source_identity must be an explicit canonical UUIDv7 string",
        )
    try:
        identity = UUID(value)
    except ValueError as error:
        raise PakError("E_SOURCE_IDENTITY", "source_identity is not a UUID") from error
    if (
        str(identity) != value
        or identity.int == 0
        or identity.version != 7
        or identity.variant != RFC_4122
    ):
        raise PakError(
            "E_SOURCE_IDENTITY",
            "source_identity must be a non-nil canonical lowercase UUIDv7",
        )
    return identity.bytes


def validate_source_identity_bytes(value: bytes) -> None:
    """Validate the native header boundary even when the spec loader is bypassed."""
    if not isinstance(value, bytes) or len(value) != 16:
        raise PakError(
            "E_SOURCE_IDENTITY", "source_identity must contain 16 UUIDv7 bytes"
        )
    source_identity_bytes(str(UUID(bytes=value)))
