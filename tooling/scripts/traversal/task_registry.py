from __future__ import annotations

import importlib
import importlib.metadata
import logging
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, Iterable, List, Mapping, Optional

from rich.console import Console

log = logging.getLogger(__name__)

@dataclass(slots=True)
class TaskInvocation:
    name: str
    func: Callable[[Path, "TraversalContext"], None]
    description: str = ""

    def run(self, project: Path, context: "TraversalContext") -> None:
        context.logger.debug("Starting task '%s' for %s", self.name, project.name)
        context.project = project
        context.active_task = self.name
        self.func(project, context)


@dataclass(slots=True)
class TraversalContext:
    project: Optional[Path] = None
    console: Optional[Console] = None
    dry_run: bool = False
    forwarded_arguments: Mapping[str, object] = field(default_factory=dict)
    forwarded_tokens: List[str] = field(default_factory=list)
    extra_arguments: List[str] = field(default_factory=list)
    logger: logging.Logger = field(default_factory=lambda: logging.getLogger("traversal"))
    active_task: Optional[str] = None


class TaskRegistry:
    ENTRYPOINT_GROUP = "droidnet.traversal.tasks"

    def __init__(self) -> None:
        self._tasks: Dict[str, TaskInvocation] = {}
        self._aliases: Dict[str, str] = {}

    def register(self, name: str, func: Callable[[Path, TraversalContext], None], *, aliases: Iterable[str] | None = None, description: str = "") -> None:
        key = name.lower()
        if key in self._tasks:
            raise ValueError(f"Task '{name}' already registered")
        self._tasks[key] = TaskInvocation(name=name, func=func, description=description)
        for alias in aliases or []:
            self._aliases[alias.lower()] = key
        log.debug("Registered task '%s'", name)

    def resolve(self, name: str) -> TaskInvocation:
        key = name.lower()
        if key in self._aliases:
            key = self._aliases[key]
        if key not in self._tasks:
            raise KeyError(f"Unknown task '{name}'")
        return self._tasks[key]

    def tasks(self) -> Iterable[TaskInvocation]:
        return self._tasks.values()

    def load_builtin(self) -> None:
        import tooling.scripts.traversal.tasks  # noqa: F401

    def load_plugins(self) -> None:
        for entry_point in importlib.metadata.entry_points().select(group=self.ENTRYPOINT_GROUP):
            try:
                entry_point.load()
                log.debug("Loaded task plugin '%s'", entry_point.name)
            except Exception as exc:  # pragma: no cover
                log.error("Failed to load task plugin '%s': %s", entry_point.name, exc)

    def load_local_packages(self, directory: Path) -> None:
        tasks_dir = directory / "tasks"
        if not tasks_dir.exists():
            return
        for module_path in tasks_dir.glob("*.py"):
            if module_path.name.startswith("_"):
                continue
            module_name = f"{module_path.parent.name}.{module_path.stem}"
            try:
                importlib.import_module(f"tooling.scripts.traversal.tasks.{module_path.stem}")
                log.debug("Imported local task module '%s'", module_name)
            except Exception as exc:  # pragma: no cover
                log.error("Failed to import task module '%s': %s", module_name, exc)


def task(name: str, *, aliases: Iterable[str] | None = None, description: str = "") -> Callable[[Callable[[Path, TraversalContext], None]], Callable[[Path, TraversalContext], None]]:
    def decorator(func: Callable[[Path, TraversalContext], None]) -> Callable[[Path, TraversalContext], None]:
        registry = get_global_registry()
        registry.register(name, func, aliases=aliases, description=description)
        return func

    return decorator


_GLOBAL_REGISTRY: Optional[TaskRegistry] = None


def get_global_registry() -> TaskRegistry:
    global _GLOBAL_REGISTRY
    if _GLOBAL_REGISTRY is None:
        _GLOBAL_REGISTRY = TaskRegistry()
    return _GLOBAL_REGISTRY
