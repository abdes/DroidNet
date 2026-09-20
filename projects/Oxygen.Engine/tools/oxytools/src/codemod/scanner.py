"""Gitignore-respecting discovery with literal, NUL-delimited ripgrep searches."""

from __future__ import annotations

import os
import subprocess
from pathlib import Path

from oxytools.common import ToolError


class SymbolScanner:
    def __init__(self, root_dir, includes=(), excludes=()) -> None:
        import pathspec

        self.root = Path(root_dir).resolve()
        self.includes = pathspec.PathSpec.from_lines("gitwildmatch", includes or [])
        self.excludes = pathspec.PathSpec.from_lines("gitwildmatch", excludes or [])

    def _run(self, arguments: list[str], *, filter_includes: bool = True) -> list[Path]:
        process = subprocess.run(
            ["rg", *arguments, str(self.root)], capture_output=True, check=False
        )
        if process.returncode not in (0, 1):
            raise ToolError(
                "File discovery failed: "
                + process.stderr.decode("utf-8", errors="replace").strip()
            )
        found = set()
        for name in process.stdout.split(b"\0"):
            if not name:
                continue
            path = Path(os.fsdecode(name)).resolve()
            if not path.is_relative_to(self.root):
                continue
            relative = path.relative_to(self.root).as_posix()
            if (
                filter_includes
                and self.includes.patterns
                and not self.includes.match_file(relative)
            ):
                continue
            if not self.excludes.match_file(relative):
                found.add(path)
        return sorted(found)

    def files(self) -> list[Path]:
        return self._run(["--files", "--null", "--"], filter_includes=False)

    def scan(self, symbol: str) -> list[Path]:
        return self._run(
            [
                "--files-with-matches",
                "--null",
                "--fixed-strings",
                "--word-regexp",
                "--",
                symbol.rsplit("::", 1)[-1],
            ]
        )
