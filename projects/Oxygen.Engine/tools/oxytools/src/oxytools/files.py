"""Atomic file replacement shared by developer tools."""

import os
import tempfile
from pathlib import Path

from .common import ToolError


def atomic_write(
    path: Path, content: bytes, mode: int, *, expected: bytes | None = None
) -> None:
    descriptor, name = tempfile.mkstemp(prefix=".oxytools-", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.chmod(mode)
        if expected is not None and path.read_bytes() != expected:
            raise ToolError(f"Contents changed before replacement: {path}")
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)
