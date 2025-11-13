from __future__ import annotations

from pathlib import Path
from typing import Dict, Optional

from pathspec import PathSpec

__all__ = ["GitIgnoreManager"]


class GitIgnoreManager:
    """Evaluate gitignore rules on-demand as the traversal progresses."""

    def __init__(self) -> None:
        self._cache: Dict[Path, Optional[PathSpec]] = {}

    def is_ignored(self, path: Path, root: Path) -> bool:
        try:
            path_relative = path.resolve().relative_to(root.resolve())
        except ValueError:
            return False

        absolute = path.resolve()
        for ancestor in self._iter_ancestors(absolute, root):
            spec = self._load_spec(ancestor)
            if not spec:
                continue
            rel = absolute.relative_to(ancestor)
            rel_str = rel.as_posix()
            candidates = [rel_str]
            if absolute.is_dir():
                candidates.append(f"{rel_str}/")
            for candidate in candidates:
                if spec.match_file(candidate):
                    return True
        return False

    def _iter_ancestors(self, path: Path, root: Path):
        current = path if path.is_dir() else path.parent
        root = root.resolve()
        while True:
            yield current
            if current == root:
                break
            if (current / ".git").exists():
                break
            if current.parent == current:
                break
            current = current.parent

    def _load_spec(self, directory: Path) -> Optional[PathSpec]:
        if directory in self._cache:
            return self._cache[directory]
        gitignore = directory / ".gitignore"
        if not gitignore.exists():
            self._cache[directory] = None
            return None
        try:
            lines = [
                line.strip()
                for line in gitignore.read_text(encoding="utf-8").splitlines()
                if line.strip() and not line.lstrip().startswith("#")
            ]
        except OSError:
            self._cache[directory] = None
            return None
        if not lines:
            self._cache[directory] = None
            return None
        spec = PathSpec.from_lines("gitwildmatch", lines)
        self._cache[directory] = spec
        return spec
