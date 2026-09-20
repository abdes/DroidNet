"""Select only owned C++ files; explicit file lists never trigger traversal."""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path

from oxytools.common import ToolError, path_key
from oxytools.ownership import Ownership

EXTENSIONS = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".inc",
    ".inl",
    ".ipp",
    ".tpp",
    ".ixx",
    ".cppm",
}


@dataclass
class Selection:
    files: list[Path] = field(default_factory=list)
    errors: list[tuple[Path, str]] = field(default_factory=list)
    skipped: int = 0


def select_files(policy: Ownership, paths: list[Path]) -> Selection:
    result = Selection()
    seen: set[str] = set()
    traversed: set[str] = set()

    def add(path: Path) -> None:
        resolved = path.resolve()
        key = path_key(resolved)
        if key in seen:
            return
        seen.add(key)
        if path.suffix.lower() not in EXTENSIONS or not policy.owned(resolved):
            result.skipped += 1
            return
        result.files.append(resolved)

    for requested in paths:
        try:
            path = requested.resolve(strict=True)
            if path.is_file():
                add(path)
                continue
            if not path.is_dir():
                raise ToolError("Not a regular file or directory")
            if not path.is_relative_to(policy.root):
                raise ToolError("Directory is outside the project root")
            if not any(
                path.is_relative_to(root) or root.is_relative_to(path)
                for root in policy.project_roots
            ):
                raise ToolError("Directory contains no owned project roots")

            def visit_error(error: OSError, directory: Path = path) -> None:
                result.errors.append((Path(error.filename or directory), str(error)))

            for folder, directories, files in os.walk(path, onerror=visit_error):
                current = Path(folder)
                key = path_key(current)
                if key in traversed:
                    directories.clear()
                    continue
                traversed.add(key)
                # Keep ancestors of owned roots, but prune excluded subtrees.
                directories[:] = [
                    name
                    for name in directories
                    if not (current / name).is_symlink()
                    and policy.may_contain_owned_files(current / name)
                ]
                for name in files:
                    candidate = current / name
                    if candidate.suffix.lower() in EXTENSIONS:
                        try:
                            add(candidate)
                        except (OSError, RuntimeError) as error:
                            result.errors.append((candidate, str(error)))
        except (OSError, RuntimeError, ToolError) as error:
            result.errors.append((requested, str(error)))
    result.files.sort(key=path_key)
    return result
