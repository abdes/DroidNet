from __future__ import annotations

import logging
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Iterator, List, Mapping, Optional

from rich.console import Console

from .gitignore import GitIgnoreManager
from .task_registry import TaskRegistry, TaskInvocation, TraversalContext

log = logging.getLogger(__name__)


@dataclass(slots=True)
class TraversalConfig:
    start_location: Path
    tasks: List[TaskInvocation]
    exclude_tests: bool = False
    exclude_samples: bool = False
    exclude_pattern: Optional[re.Pattern[str]] = None
    dry_run: bool = False
    forwarded_arguments: Mapping[str, object] | None = None
    forwarded_tokens: List[str] | None = None
    console: Console | None = None
    logger: logging.Logger | None = None
    extra_arguments: List[str] | None = None


class TraversalRunner:
    """Traverse the repository tree and run tasks against discovered projects."""

    def __init__(self, registry: TaskRegistry | None = None) -> None:
        self._registry = registry or TaskRegistry()

    @property
    def registry(self) -> TaskRegistry:
        return self._registry

    def run(self, config: TraversalConfig) -> None:
        start = config.start_location.resolve()
        log.debug("Traversal start: %s", start)
        manager = GitIgnoreManager()
        project_iter = self._discover_projects(
            start,
            manager,
            config.exclude_tests,
            config.exclude_samples,
            config.exclude_pattern,
        )
        forwarded_args = config.forwarded_arguments or {}
        forwarded_tokens = config.forwarded_tokens or []

        context = TraversalContext(
            console=config.console,
            dry_run=config.dry_run,
            forwarded_arguments=forwarded_args,
            forwarded_tokens=forwarded_tokens,
            extra_arguments=list(config.extra_arguments or []),
            logger=config.logger or log,
        )

        project_count = 0
        for project in project_iter:
            project_count += 1
            log.debug("Running tasks for project: %s", project)
            for task in config.tasks:
                log.debug("Invoking task '%s'", task.name)
                task.run(project, context)
        log.debug("Processed %d project(s)", project_count)

    def _discover_projects(
        self,
        start: Path,
        ignore_manager: GitIgnoreManager,
        exclude_tests: bool,
        exclude_samples: bool,
        exclude_pattern: Optional[re.Pattern[str]],
    ) -> Iterator[Path]:
        stack: List[Path] = [start]
        while stack:
            current = stack.pop()
            if ignore_manager.is_ignored(current, start):
                log.debug("Ignoring path due to .gitignore rules: %s", current)
                continue

            if current.is_dir():
                name_lower = current.name.lower()
                if exclude_tests and name_lower == "tests":
                    log.debug("Skipping tests directory: %s", current)
                    continue
                if exclude_samples and name_lower == "samples":
                    log.debug("Skipping samples directory: %s", current)
                    continue

                try:
                    children = sorted(current.iterdir(), reverse=True)
                except PermissionError:
                    log.warning("Skipping directory due to permission error: %s", current)
                    continue

                stack.extend(children)
                continue

            if current.suffix.lower() == ".csproj":
                project_name = current.stem
                if exclude_pattern and exclude_pattern.search(project_name):
                    log.debug("Skipping project due to exclude regex: %s", current)
                    continue
                yield current

    def available_tasks(self) -> Iterable[TaskInvocation]:
        return self._registry.tasks()


__all__ = ["TraversalRunner", "TraversalConfig"]
