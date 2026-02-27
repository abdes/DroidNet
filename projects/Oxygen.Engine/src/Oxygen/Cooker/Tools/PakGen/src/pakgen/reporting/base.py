from __future__ import annotations

import sys
import time
from contextlib import contextmanager
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Any, Dict, Optional

__all__ = [
    "TaskStatus",
    "TaskRecord",
    "Reporter",
    "set_reporter",
    "get_reporter",
    "set_verbosity",
    "get_verbosity",
    "section",
    "task",
]


class TaskStatus(Enum):
    RUNNING = auto()
    SUCCESS = auto()
    FAILED = auto()
    SKIPPED = auto()


@dataclass(slots=True)
class TaskRecord:
    task_id: str
    name: str
    total: Optional[int] = None
    completed: int = 0
    status: TaskStatus = TaskStatus.RUNNING
    start_time: float = field(default_factory=time.time)
    end_time: float | None = None
    meta: Dict[str, Any] = field(default_factory=dict)


_VERBOSITY: int = 0  # global verbosity level set by CLI (-v repeats)


def set_verbosity(level: int) -> None:
    global _VERBOSITY
    _VERBOSITY = max(0, level)


def get_verbosity() -> int:
    return _VERBOSITY


class Reporter:
    supports_progress: bool = False

    def start_task(
        self, task_id: str, name: str, total: int | None = None, **meta: Any
    ) -> None:  # noqa: D401
        raise NotImplementedError

    def advance(
        self, task_id: str, step: int = 1, **meta: Any
    ) -> None:  # noqa: D401
        raise NotImplementedError

    def end_task(
        self,
        task_id: str,
        status: TaskStatus = TaskStatus.SUCCESS,
        **final_meta: Any,
    ) -> None:  # noqa: D401
        raise NotImplementedError

    # New structured message API
    def status(self, message: str, **fields: Any) -> None:  # noteworthy info
        raise NotImplementedError

    def verbose(
        self, message: str, *, level: int = 1, **fields: Any
    ) -> None:  # lower-importance, gated by global verbosity
        # Default implementation: ignore unless subclasses override.
        pass

    def error(self, message: str, **fields: Any) -> None:  # error reporting
        raise NotImplementedError

    def warning(self, message: str, **fields: Any) -> None:  # warning messages
        # Optional to override; default routes to status for backward compat.
        self.status(message, **fields)

    def section(self, title: str) -> None:  # noqa: D401
        raise NotImplementedError

    def flush(self) -> None:  # noqa: D401
        pass


_ACTIVE_REPORTER: Reporter | None = None


def set_reporter(rep: Reporter) -> None:
    global _ACTIVE_REPORTER
    _ACTIVE_REPORTER = rep


def get_reporter() -> Reporter:
    global _ACTIVE_REPORTER
    if _ACTIVE_REPORTER is None:
        from .plain import PlainReporter  # local import to avoid cycle

        _ACTIVE_REPORTER = PlainReporter(stream=sys.stderr)
    return _ACTIVE_REPORTER


@contextmanager
def section(title: str):
    rep = get_reporter()
    rep.section(title)
    yield


@contextmanager
def task(task_id: str, name: str, total: int | None = None, **meta: Any):
    rep = get_reporter()
    rep.start_task(task_id, name, total, **meta)
    try:
        yield
    except Exception:
        rep.end_task(task_id, TaskStatus.FAILED)
        raise
    else:
        rep.end_task(task_id, TaskStatus.SUCCESS)
