# SPDX-License-Identifier: BSD-3-Clause

from __future__ import annotations

from collections.abc import Generator
import tempfile
from pathlib import Path

import pytest


def _is_under_base(path: Path, base: Path) -> bool:
    try:
        path.relative_to(base)
        return True
    except ValueError:
        return False


def _canonical(path: Path) -> Path:
    # strict=False keeps behavior stable while still normalizing traversal.
    return path.resolve(strict=False)


def _validate_safe_root(candidate: Path, temp_base: Path) -> Path:
    canonical_candidate = _canonical(candidate)
    canonical_base = _canonical(temp_base)

    if not canonical_candidate.is_absolute():
        raise RuntimeError(
            f"Temp test path is not absolute: {canonical_candidate}"
        )
    if canonical_candidate == canonical_base:
        raise RuntimeError(
            f"Refusing to use temp base directory directly: {canonical_candidate}"
        )
    if not _is_under_base(canonical_candidate, canonical_base):
        raise RuntimeError(
            "Refusing to use non-temp directory for tests: "
            f"{canonical_candidate} (base: {canonical_base})"
        )

    return canonical_candidate


def _explicit_delete_tree(root: Path) -> None:
    # Delete only explicit, enumerated paths; no globs.
    to_visit = [root]
    to_remove: list[Path] = []

    while to_visit:
        current = to_visit.pop()
        for child in current.iterdir():
            to_remove.append(child)
            if child.is_dir() and not child.is_symlink():
                to_visit.append(child)

    to_remove.sort(key=lambda p: len(p.parts), reverse=True)

    for target in to_remove:
        if target.is_symlink() or target.is_file():
            target.unlink(missing_ok=True)
        elif target.is_dir():
            target.rmdir()

    root.rmdir()


@pytest.fixture
def tmp_path(
    tmp_path_factory: pytest.TempPathFactory,
) -> Generator[Path, None, None]:
    os_temp_base = Path(tempfile.gettempdir())
    root = tmp_path_factory.mktemp("oxygen_pakgen_tests", numbered=True)
    root = _validate_safe_root(Path(root), os_temp_base)

    yield root

    if not root.exists():
        return

    if not root.is_absolute():
        raise RuntimeError(f"Refusing to delete non-absolute path: {root}")

    canonical_root = _canonical(root)
    canonical_base = _canonical(os_temp_base)

    if not _is_under_base(canonical_root, canonical_base):
        raise RuntimeError(
            "Refusing to delete path outside OS temp directory: "
            f"{canonical_root} (base: {canonical_base})"
        )

    _explicit_delete_tree(canonical_root)
