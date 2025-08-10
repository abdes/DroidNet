from __future__ import annotations

from .base import Reporter, TaskStatus


class SilentReporter(Reporter):
    """No-op reporter (quiet mode)."""

    def start_task(
        self, task_id: str, name: str, total: int | None = None, **meta
    ):
        pass

    def advance(self, task_id: str, step: int = 1, **meta):
        pass

    def end_task(
        self,
        task_id: str,
        status: TaskStatus = TaskStatus.SUCCESS,
        **final_meta,
    ):
        pass

    def status(self, message: str, **fields):
        pass

    def verbose(self, message: str, *, level: int = 1, **fields):
        pass

    def error(self, message: str, **fields):
        pass

    def section(self, title: str) -> None:
        pass

    def warning(self, message: str, **fields):
        pass
