from __future__ import annotations

import sys
import time
from typing import Any, Dict
from .base import Reporter, TaskStatus, TaskRecord, get_verbosity

ICONS = {
    TaskStatus.SUCCESS: "✔",  # success
    TaskStatus.FAILED: "✖",  # failure
    TaskStatus.SKIPPED: "→",  # skipped / forward
}


class PlainReporter(Reporter):
    """Plain deterministic reporter with minimal icons and optional color."""

    supports_progress = False

    def __init__(self, stream=None, use_color: bool | None = None):
        self.stream = stream or sys.stderr
        self.use_color = (
            use_color
            if use_color is not None
            else getattr(self.stream, "isatty", lambda: False)()
        )
        self._tasks: Dict[str, TaskRecord] = {}

    def _c(self, code: str, text: str):
        if not self.use_color:
            return text
        return f"\x1b[{code}m{text}\x1b[0m"

    def start_task(
        self, task_id: str, name: str, total: int | None = None, **meta: Any
    ) -> None:
        self._tasks[task_id] = TaskRecord(task_id, name, total, meta=meta)
        # self.stream.write(f"+ {name}\n")

    def advance(self, task_id: str, step: int = 1, **meta: Any) -> None:
        rec = self._tasks.get(task_id)
        if not rec:
            return
        rec.completed += step
        rec.meta.update(meta)
        # per-item progress line when possible
        item = meta.get("current_item") if meta else None
        if item is None:
            # fallback to simple index notation
            item = f"item#{rec.completed}"
        self.stream.write(
            f"   · {rec.name}: {item} ({rec.completed}/{rec.total if rec.total is not None else '?'})\n"
        )

    def end_task(
        self,
        task_id: str,
        status: TaskStatus = TaskStatus.SUCCESS,
        **final_meta: Any,
    ) -> None:
        rec = self._tasks.get(task_id)
        if not rec:
            return
        rec.status = status
        rec.end_time = time.time()
        rec.meta.update(final_meta)
        icon = ICONS.get(status, "?")
        duration = rec.end_time - rec.start_time if rec.end_time else 0
        extra = ""
        if rec.total is not None:
            extra = f" {rec.completed}/{rec.total}"
        stats = []
        for key in ("entries", "blobs", "bytes", "planned"):
            if key in rec.meta:
                stats.append(f"{key}={rec.meta[key]}")
        stats_part = f" [{' '.join(stats)}]" if stats else ""
        duration_part = f" ({duration:.2f}s)"
        self.stream.write(
            f" {icon} {rec.name}{extra}{duration_part}{stats_part}\n"
        )

    def status(self, message: str, **fields: Any) -> None:
        prefix = self._c("32", "INFO")
        self.stream.write(f"{prefix}: {message}\n")

    def verbose(self, message: str, *, level: int = 1, **fields: Any) -> None:
        if get_verbosity() < level:
            return
        prefix = self._c("36", f"VERB{level}")
        self.stream.write(f"{prefix}: {message}\n")

    def error(self, message: str, **fields: Any) -> None:
        prefix = self._c("31", "ERROR")
        self.stream.write(f"{prefix}: {message}\n")

    def warning(self, message: str, **fields: Any) -> None:  # new
        prefix = self._c("33", "WARN")
        self.stream.write(f"{prefix}: {message}\n")

    def section(self, title: str) -> None:
        self.stream.write(f"\n[{title}]\n")

    def flush(self) -> None:
        pass
