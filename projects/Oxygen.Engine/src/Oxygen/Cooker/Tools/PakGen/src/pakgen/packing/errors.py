"""Error definitions for PakGen (refactored)."""

from __future__ import annotations
from dataclasses import dataclass
from typing import Any, Dict, Optional

E_SPEC_MISSING_FIELD = "E_SPEC_MISSING_FIELD"
E_SPEC_TYPE_MISMATCH = "E_SPEC_TYPE_MISMATCH"
E_SPEC_VALUE_RANGE = "E_SPEC_VALUE_RANGE"
E_DUP_RESOURCE_NAME = "E_DUP_RESOURCE_NAME"
E_DUP_ASSET_KEY = "E_DUP_ASSET_KEY"
E_INVALID_REFERENCE = "E_INVALID_REFERENCE"
E_INDEX_OUT_OF_RANGE = "E_INDEX_OUT_OF_RANGE"
E_ALIGNMENT = "E_ALIGNMENT"
E_OVERLAP = "E_OVERLAP"
E_SIZE_MISMATCH = "E_SIZE_MISMATCH"
E_DESC_TOO_LARGE = "E_DESC_TOO_LARGE"
E_WRITE_IO = "E_WRITE_IO"
E_CRC_MISMATCH = "E_CRC_MISMATCH"
E_INTERNAL = "E_INTERNAL"


@dataclass
class PakError(Exception):
    code: str
    message: str
    context: Optional[Dict[str, Any]] = None

    def __str__(self) -> str:  # pragma: no cover
        return f"{self.code}: {self.message}" + (
            f" | ctx={self.context}" if self.context else ""
        )

    def to_dict(self) -> Dict[str, Any]:
        return {
            "code": self.code,
            "message": self.message,
            "context": self.context or {},
        }


class SpecificationError(PakError):
    pass


class ResourceError(PakError):
    pass


class AssetError(PakError):
    pass


class DependencyError(PakError):
    pass


class BinaryFormatError(PakError):
    pass


class ValidationError(PakError):
    pass


def spec_error(
    code: str, message: str, context: Optional[Dict[str, Any]] = None
) -> SpecificationError:
    return SpecificationError(code=code, message=message, context=context)


def internal_error(
    message: str, context: Optional[Dict[str, Any]] = None
) -> PakError:
    return PakError(code=E_INTERNAL, message=message, context=context)


__all__ = [
    "PakError",
    "SpecificationError",
    "ResourceError",
    "AssetError",
    "DependencyError",
    "BinaryFormatError",
    "ValidationError",
    "spec_error",
    "internal_error",
    "E_SPEC_MISSING_FIELD",
    "E_SPEC_TYPE_MISMATCH",
    "E_SPEC_VALUE_RANGE",
    "E_DUP_RESOURCE_NAME",
    "E_DUP_ASSET_KEY",
    "E_INVALID_REFERENCE",
    "E_INDEX_OUT_OF_RANGE",
    "E_ALIGNMENT",
    "E_OVERLAP",
    "E_SIZE_MISMATCH",
    "E_DESC_TOO_LARGE",
    "E_WRITE_IO",
    "E_CRC_MISMATCH",
    "E_INTERNAL",
]
