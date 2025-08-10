from __future__ import annotations

import json
import sys
import time
from typing import Any, Dict
from .base import Reporter, TaskStatus, TaskRecord, get_verbosity


class JsonLinesReporter(Reporter):
    """Machine-readable JSON lines reporter."""

    supports_progress = False

    def __init__(self, stream=None):
        self.stream = stream or sys.stdout
        self._tasks: Dict[str, TaskRecord] = {}

    def _emit(self, obj: dict):
        self.stream.write(json.dumps(obj, sort_keys=True) + "\n")

    def start_task(
        self, task_id: str, name: str, total: int | None = None, **meta: Any
    ) -> None:
        rec = TaskRecord(task_id, name, total, meta=meta)
        self._tasks[task_id] = rec
        self._emit(
            {
                "event": "task_start",
                "id": task_id,
                "name": name,
                "total": total,
                **meta,
            }
        )

    def advance(self, task_id: str, step: int = 1, **meta: Any) -> None:
        rec = self._tasks.get(task_id)
        if not rec:
            return
        rec.completed += step
        rec.meta.update(meta)
        self._emit(
            {
                "event": "task_progress",
                "id": task_id,
                "completed": rec.completed,
                **meta,
            }
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
        self._emit(
            {
                "event": "task_end",
                "id": task_id,
                "status": status.name.lower(),
                "completed": rec.completed,
                "total": rec.total,
                "duration_seconds": (
                    (rec.end_time - rec.start_time) if rec.end_time else 0
                ),
                **rec.meta,
            }
        )

    def _maybe_summary(self, message: str, level: str, **fields: Any) -> None:
        lower = message.lower()
        summary_map: Dict[str, str] = {
            "resources summary": "resources",
            "assets summary": "assets",
            "plan summary": "plan",
            "write summary": "write",
            "manifest summary": "manifest",
            "build summary": "build",
            "diff summary": "diff",
        }
        for prefix, stype in summary_map.items():
            if lower.startswith(prefix):
                parts = message.split(":", 1)
                kv_text = parts[1] if len(parts) > 1 else ""
                kv_pairs = {}
                for token in kv_text.strip().split():
                    if "=" in token:
                        k, v = token.split("=", 1)
                        kv_pairs[k] = v
                self._emit(
                    {
                        "event": "summary",
                        "summary_type": stype,
                        "level": level.lower(),
                        "raw": message,
                        **kv_pairs,
                        **fields,
                    }
                )
                break

    def status(self, message: str, **fields: Any) -> None:
        self._maybe_summary(message, "info", **fields)
        self._emit(
            {"event": "status", "message": message, "level": "info", **fields}
        )

    def verbose(self, message: str, *, level: int = 1, **fields: Any) -> None:
        if get_verbosity() < level:
            return
        self._emit(
            {
                "event": "status",
                "message": message,
                "level": f"verbose{level}",
                "vlevel": level,
                **fields,
            }
        )

    def error(self, message: str, **fields: Any) -> None:
        self._emit(
            {"event": "status", "message": message, "level": "error", **fields}
        )

    def warning(self, message: str, **fields: Any) -> None:
        self._emit(
            {
                "event": "status",
                "message": message,
                "level": "warning",
                **fields,
            }
        )

    def section(self, title: str) -> None:
        self._emit({"event": "section", "title": title})
