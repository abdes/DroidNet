"""Keep the requested edit root separate from the compilation configuration root."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from oxytools.common import ToolError
from oxytools.compilation import ClangdConfig, parse_clangd


@dataclass(frozen=True)
class ProjectInfo:
    root_dir: Path
    configuration_root: Path

    def compile_config(self, build_dir: str | None = None) -> ClangdConfig:
        return parse_clangd(
            self.configuration_root / ".clangd",
            Path(build_dir).resolve() if build_dir else None,
        )


class ProjectResolver:
    @staticmethod
    def resolve(start_path, *, explicit: bool = False) -> ProjectInfo:
        start = Path(start_path).resolve()
        if not start.is_dir():
            raise ToolError(f"Root is not an existing directory: {start}")
        ancestors = (start, *start.parents)
        configured = next(
            (path for path in ancestors if (path / ".clangd").is_file()), None
        )
        nearest = next(
            (
                path
                for path in ancestors
                if any(
                    (path / name).exists()
                    for name in (
                        ".oxytools.json",
                        ".git",
                        "pyproject.toml",
                        "CMakeLists.txt",
                    )
                )
            ),
            start,
        )
        root = start if explicit else configured or nearest
        return ProjectInfo(root, configured or root)
