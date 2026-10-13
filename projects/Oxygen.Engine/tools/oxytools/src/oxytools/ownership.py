"""Schema-validated first-party file ownership shared by developer tools."""

from __future__ import annotations

import fnmatch
import json
from dataclasses import dataclass
from functools import cached_property
from pathlib import Path

from .common import ToolError, absolute, display, validate, within

OWNERSHIP_SCHEMA = {
    "type": "object",
    "additionalProperties": False,
    "required": ["project_roots", "exclude"],
    "properties": {
        "project_roots": {
            "type": "array",
            "minItems": 1,
            "items": {"type": "string", "minLength": 1},
        },
        "exclude": {"type": "array", "items": {"type": "string"}},
    },
}


@dataclass
class Ownership:
    root: Path
    project_roots: list[Path]
    excludes: list[str]

    @classmethod
    def load(cls, root: Path, ownership_file: Path | None = None) -> Ownership:
        manifest = (
            ownership_file if ownership_file is not None else root / ".oxytools.json"
        )
        if not manifest.is_file():
            raise ToolError(
                f"Missing project ownership configuration: {manifest}\n"
                "Select the intended checkout with --project-root PATH or provide "
                "an existing policy explicitly with --ownership-file PATH. "
                "Policy paths are resolved against the target project root."
            )
        data = json.loads(manifest.read_text(encoding="utf-8-sig"))
        validate(data, OWNERSHIP_SCHEMA, str(manifest))
        projects = [absolute(path, root) for path in data["project_roots"]]
        if any(not within(path, root) for path in projects):
            raise ToolError("Project roots must remain inside the engine directory")
        return cls(root, projects, data["exclude"])

    def owned(self, path: Path) -> bool:
        return self.owned_resolved(path.resolve())

    def owned_resolved(self, path: Path) -> bool:
        """Ownership of a path the caller resolved for this query.

        Queries resolve their path fresh, so a retargeted link is never judged
        by its old target; only the configured roots resolve once.
        """
        if not any(path.is_relative_to(project) for project in self._project_roots):
            return False
        root = self._root
        name = (
            path.relative_to(root).as_posix()
            if path.is_relative_to(root)
            else str(path)
        )
        return not any(fnmatch.fnmatchcase(name, pattern) for pattern in self.excludes)

    @cached_property
    def _root(self) -> Path:
        return self.root.resolve()

    @cached_property
    def _project_roots(self) -> tuple[Path, ...]:
        return tuple(project.resolve() for project in self.project_roots)

    def may_contain_owned_files(self, directory: Path) -> bool:
        """Prune only exclusions that cover a whole subtree, never file globs."""
        directory = directory.resolve()
        if any(root.is_relative_to(directory) for root in self.project_roots):
            return True
        if not any(directory.is_relative_to(root) for root in self.project_roots):
            return False
        name = display(directory, self.root) + "/"
        return not any(
            pattern.endswith("/**") and fnmatch.fnmatchcase(name, pattern)
            for pattern in self.excludes
        )
