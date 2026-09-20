"""Atomic file replacement shared by developer tools."""

import os
import tempfile
from pathlib import Path


def atomic_write(path: Path, content: bytes, mode: int) -> None:
    descriptor, name = tempfile.mkstemp(prefix=".oxytools-", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.chmod(mode)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)
