"""Path utilities (safe resolution)."""

from __future__ import annotations
from pathlib import Path

__all__ = ["safe_file_path"]


def safe_file_path(base_dir: Path, file_path: str) -> Path:
    base_dir = base_dir.resolve()
    resolved = (base_dir / file_path).resolve()
    resolved.relative_to(base_dir)  # raises ValueError if escapes
    return resolved
