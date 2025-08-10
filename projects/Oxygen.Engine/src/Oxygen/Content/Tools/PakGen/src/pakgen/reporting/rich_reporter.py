from __future__ import annotations

import time
from typing import Any, Dict, List
import os
from .base import Reporter, TaskStatus, TaskRecord, get_verbosity

from rich.console import Console
from rich.progress import (
    Progress,
    SpinnerColumn,
    TextColumn,
    BarColumn,
    TimeElapsedColumn,
)
from rich.text import Text

_STATUS_ICON = {
    TaskStatus.SUCCESS: "✔",
    TaskStatus.FAILED: "✖",
    TaskStatus.SKIPPED: "→",
}


class RichReporter(Reporter):
    supports_progress = True

    def __init__(self):
        self.console = Console(stderr=True, highlight=False, soft_wrap=False)
        self._transient = os.getenv(
            "PAKGEN_PROGRESS_TRANSIENT", "0"
        ).lower() in ("1", "true", "yes")
        self.progress: Progress | None = None
        self._tasks: Dict[str, TaskRecord] = {}
        self._task_ids: Dict[str, Any] = {}
        self._detail_task_ids: Dict[str, Any] = {}
        self._active = False
        self._transient_completions: List[str] = []

    # Internal -----------------------------------------------------------------
    def _ensure_progress(self) -> None:
        if self.progress is None:

            class HideOnDetailMixin:
                def _is_detail(self, task) -> bool:  # type: ignore[override]
                    return bool(task.fields.get("is_detail"))

            class MaybeSpinnerColumn(HideOnDetailMixin, SpinnerColumn):
                def render(self, task):  # type: ignore[override]
                    if self._is_detail(task):
                        return Text("")
                    return super().render(task)

            class MaybeBarColumn(HideOnDetailMixin, BarColumn):
                def render(self, task):  # type: ignore[override]
                    if self._is_detail(task):
                        return Text("")
                    return super().render(task)

            class MaybeCountColumn(HideOnDetailMixin, TextColumn):
                def render(self, task):  # type: ignore[override]
                    if self._is_detail(task):
                        return Text("")
                    return super().render(task)

            class MaybeTimeColumn(HideOnDetailMixin, TimeElapsedColumn):
                def render(self, task):  # type: ignore[override]
                    if self._is_detail(task):
                        return Text("")
                    return super().render(task)

            class NameColumn(TextColumn):
                def render(self, task):  # type: ignore[override]
                    name = task.fields.get("name", "")
                    if task.fields.get("is_detail"):
                        detail = task.fields.get("detail", "")
                        if detail:
                            name = f"  ↳ {detail}"
                        else:
                            name = ""
                    task.fields["__shown_name"] = name
                    return Text(name)

            self.progress = Progress(
                MaybeSpinnerColumn(spinner_name="dots"),
                NameColumn("{task.fields[name]}", justify="left"),
                MaybeBarColumn(bar_width=None),
                MaybeCountColumn("{task.completed}/{task.total}"),
                MaybeTimeColumn(),
                transient=self._transient,
                console=self.console,
                expand=True,
            )
            self.progress.start()
            self._active = True

    # Tasks --------------------------------------------------------------------
    def start_task(
        self, task_id: str, name: str, total: int | None = None, **meta: Any
    ) -> None:
        # Treat tasks without a known total as simple section headers, not spinners.
        if total is None:
            # Flush any active progress first to avoid rule interleaving.
            if self.progress and self._active:
                try:
                    self.progress.refresh()
                except Exception:
                    pass
            self.console.rule(f"{name}")
            return
        if not self._active:
            self._ensure_progress()
        assert self.progress is not None
        rid = self.progress.add_task("", total=total, name=name)
        # Optional detail sub-task only if not transient.
        if self._transient:
            detail_id = None
        else:
            detail_id = self.progress.add_task(
                "", total=0, name=name, is_detail=True, detail=""
            )
        rec = TaskRecord(task_id, name, total, meta=meta)
        self._tasks[task_id] = rec
        self._task_ids[task_id] = rid
        self._detail_task_ids[task_id] = detail_id
        try:
            self.progress.refresh()
        except Exception:
            pass

    def advance(self, task_id: str, step: int = 1, **meta: Any) -> None:
        rec = self._tasks.get(task_id)
        if not rec:
            return
        rec.completed += step
        rec.meta.update(meta)
        rid = self._task_ids.get(task_id)
        if rid is not None and rec.total is not None and self.progress:
            current_item = meta.get("current_item") if meta else None
            self.progress.update(rid, completed=rec.completed)
            if current_item and not self._transient:
                detail_id = self._detail_task_ids.get(task_id)
                if detail_id is not None:
                    self.progress.update(detail_id, detail=current_item)
            try:
                self.progress.refresh()
            except Exception:
                pass

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
        rid = self._task_ids.get(task_id)
        if rid is not None and self.progress:
            if rec.total is not None:
                self.progress.update(rid, completed=rec.total)
            if not self._transient:
                detail_id = self._detail_task_ids.get(task_id)
                if detail_id is not None:
                    self.progress.remove_task(detail_id)
                # Emit an inline completion line with stats for non-transient mode.
                icon = _STATUS_ICON.get(status, "")
                dur = (rec.end_time - rec.start_time) if rec.end_time else 0.0
                total_part = (
                    f" {rec.completed}/{rec.total}"
                    if rec.total is not None
                    else ""
                )
                stats = []
                for key in ("entries", "blobs", "bytes", "planned"):
                    if key in rec.meta:
                        stats.append(f"{key}={rec.meta[key]}")
                stats_part = f" [{' '.join(stats)}]" if stats else ""
                try:
                    self.console.print(
                        f"{icon} {rec.name}{total_part} ({dur:.2f}s){stats_part}"
                    )
                except Exception:
                    pass
        if self._transient:
            icon = _STATUS_ICON.get(status, "")
            dur = (rec.end_time - rec.start_time) if rec.end_time else 0.0
            total_part = (
                f" {rec.completed}/{rec.total}" if rec.total is not None else ""
            )
            stats = []
            for key in ("entries", "blobs", "bytes", "planned"):
                if key in rec.meta:
                    stats.append(f"{key}={rec.meta[key]}")
            stats_part = f" [{' '.join(stats)}]" if stats else ""
            self._transient_completions.append(
                f"{icon} {rec.name}{total_part} ({dur:.2f}s){stats_part}"
            )
        # bookkeeping
        self._tasks.pop(task_id, None)

        if not self._tasks and self.progress and self._active:
            try:
                self.progress.stop()
            finally:
                self._active = False
                self.progress = None
                if self._transient and self._transient_completions:
                    self.console.print("\n".join(self._transient_completions))
                    self._transient_completions.clear()
                self._task_ids.clear()
                self._detail_task_ids.clear()

    # Messaging / sections ------------------------------------------------------
    def status(self, message: str, **fields: Any) -> None:
        self.console.print(f"[green]INFO[/]: {message}")

    def verbose(self, message: str, *, level: int = 1, **fields: Any) -> None:
        if get_verbosity() < level:
            return
        self.console.print(f"[cyan]VERB{level}[/]: {message}")

    def error(self, message: str, **fields: Any) -> None:
        self.console.print(f"[bold red]ERROR[/]: {message}")

    def warning(self, message: str, **fields: Any) -> None:
        self.console.print(f"[yellow]WARN[/]: {message}")

    def section(self, title: str) -> None:
        self.console.rule(f"{title}")

    def flush(self) -> None:
        if self.progress and self._active:
            try:
                self.progress.stop()
            finally:
                self._active = False
                self.progress = None
