"""Project ownership, explicit scopes, and LLVM-compatible diagnostic filters."""

from __future__ import annotations

import os
import re
from dataclasses import dataclass
from functools import cached_property
from pathlib import Path

from oxytools.common import ToolError, absolute, path_key, within
from oxytools.compilation import HEADERS, SOURCES
from oxytools.ownership import Ownership


@dataclass(frozen=True)
class _PathIndex:
    """Canonical scope roots; lookups depend on path depth, not root count."""

    files: frozenset[Path]
    directories: frozenset[Path]

    @classmethod
    def build(cls, roots: list[Path]) -> _PathIndex:
        paths = {root.resolve() for root in roots}
        directories = frozenset(path for path in paths if path.is_dir())
        return cls(frozenset(paths - directories), directories)

    def contains(self, canonical: Path) -> bool:
        return (
            canonical in self.files
            or canonical in self.directories
            or any(parent in self.directories for parent in canonical.parents)
        )

    def traversal_roots(self) -> list[Path]:
        """Visit overlapping directory and file arguments only once."""
        return sorted(
            path
            for path in self.files | self.directories
            if not any(parent in self.directories for parent in path.parents)
        )


@dataclass
class Scope(Ownership):
    roots: list[Path]
    include_tests: bool

    @classmethod
    def select(
        cls,
        root: Path,
        paths: list[str],
        all_project: bool,
        include_tests: bool,
        *,
        ownership_file: Path | None = None,
    ) -> Scope:
        policy = Ownership.load(root, ownership_file)
        projects = policy.project_roots
        roots = (
            [path for path in projects if path.is_dir()]
            if all_project
            else [absolute(path, root) for path in paths]
        )
        scope = cls(root, projects, policy.excludes, roots, include_tests)
        for path in roots:
            if not path.exists():
                raise ToolError(f"Missing scope path: {path}")
            if not within(path, root):
                raise ToolError(f"Scope is outside this engine checkout: {path}")
            if path.is_file() and (
                not scope.eligible(path) or path.suffix.lower() not in SOURCES | HEADERS
            ):
                raise ToolError(f"Not an eligible project source/header: {path}")
            if path.is_dir() and not any(
                within(path, project) or within(project, path) for project in projects
            ):
                raise ToolError(
                    f"Directory contains no configured project roots: {path}"
                )
        return scope

    def eligible(self, path: Path) -> bool:
        return self._eligible(path.resolve())

    def _eligible(self, resolved: Path) -> bool:
        return self.owned_resolved(resolved) and (
            self.include_tests
            or "test"
            not in {p.casefold() for p in resolved.relative_to(self._root).parts}
        )

    def contains(self, path: Path) -> bool:
        return self._contains(path.resolve())

    def names_file(self, path: Path) -> bool:
        """Whether the caller named this file itself instead of a directory."""
        return path.resolve() in self._index.files

    def _contains(self, resolved: Path) -> bool:
        return self._index.contains(resolved) and self._eligible(resolved)

    @cached_property
    def _index(self) -> _PathIndex:
        return _PathIndex.build(self.roots)

    @cached_property
    def discovery_roots(self) -> list[Path]:
        """Directories bound discovery; explicit sources keep their exact scope."""
        roots = [
            root.parent if root.is_file() and root.suffix.lower() in HEADERS else root
            for root in self.roots
        ]
        return list({path_key(root): root for root in roots}.values())

    def discovery_contains(self, path: Path) -> bool:
        resolved = path.resolve()
        return self._discovery_index.contains(resolved) and self._eligible(resolved)

    @cached_property
    def _discovery_index(self) -> _PathIndex:
        return _PathIndex.build(self.discovery_roots)

    def inputs(self) -> list[Path]:
        found = {}
        for root in self._index.traversal_roots():
            for path in root.rglob("*") if root.is_dir() else [root]:
                if not path.is_file() or path.suffix.lower() not in SOURCES | HEADERS:
                    continue
                resolved = path.resolve()
                if self._contains(resolved):
                    found[os.path.normcase(str(resolved))] = resolved
        if not found:
            raise ToolError(
                "The scope contains no eligible project source/header files"
            )
        return sorted(found.values())

    def header_filter(self) -> str:
        fragments = []
        for root in self.roots:
            # LLVM uses POSIX ERE; Python noncapturing groups are invalid here.
            relative = root.resolve().relative_to(self.root).as_posix()
            escaped = re.escape(relative).replace("/", r"[/\\]")
            fragments.append(escaped + (r"([/\\].*)?" if root.is_dir() else ""))
        # Clang can print paths relative to the compilation directory. Accept
        # either spelling; canonical ownership and scope are enforced again on
        # structured diagnostics and every proposed replacement.
        return r"(^|.*[/\\])(" + "|".join(fragments) + ")$"

    def dependency_filter(
        self,
        names: list[str],
        directory: Path,
        resolved: dict[str, Path] | None = None,
    ) -> str:
        """Match compiler spellings, including uncollapsed ../ include paths.

        `resolved` maps names the caller already resolved for this query.
        """
        spellings = set()
        for name in names:
            canonical = (
                resolved[name] if resolved is not None else absolute(name, directory)
            )
            if not self._contains(canonical):
                continue
            raw = Path(name)
            lexical = directory / raw
            spellings.update([str(raw), str(lexical), str(canonical)])
            for path in [lexical, canonical]:
                if path.is_relative_to(directory):
                    spellings.add(str(path.relative_to(directory)))
        if not spellings:
            return "a^"  # valid ERE that matches no headers
        fragments = [
            re.escape(name.replace("\\", "/")).replace("/", r"[/\\]")
            for name in sorted(spellings)
        ]
        return "^(" + "|".join(fragments) + ")$"
