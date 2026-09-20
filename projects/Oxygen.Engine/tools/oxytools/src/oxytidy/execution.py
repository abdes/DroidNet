"""Bounded subprocess execution with process-tree cancellation and durable logs."""

from __future__ import annotations

import concurrent.futures
import os
import shutil
import signal
import subprocess
import threading
import time
from collections.abc import Callable, Iterable
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import TypeVar

from oxytools.common import ToolError, write_json
from oxytools.process import WindowsJob

from .reporting import Reporter

T = TypeVar("T")
R = TypeVar("R")


@dataclass
class ProcessResult:
    command: list[str]
    returncode: int
    stdout: str
    stderr: str
    duration: float
    status: str


class Runner:
    def __init__(self, timeout: float | None) -> None:
        self.timeout = timeout
        self.cancelled = threading.Event()
        self.reporter = Reporter()

    def run(
        self,
        command: list[str],
        cwd: Path,
        artifact: Path | None = None,
        input_text: str | None = None,
        preserve_newlines: bool = False,
    ) -> ProcessResult:
        started = time.monotonic()
        status = "completed"
        stdout = stderr = ""
        code = 0
        job = None
        process = None
        if self.cancelled.is_set():
            return ProcessResult(
                command, 130, "", "Cancelled before dispatch", 0, "cancelled"
            )
        try:
            process = subprocess.Popen(
                command,
                cwd=cwd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                stdin=subprocess.PIPE if input_text is not None else None,
                text=True,
                encoding="utf-8",
                errors="replace",
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
                start_new_session=os.name != "nt",
            )
            if os.name == "nt":
                job = WindowsJob(process)
            if preserve_newlines:
                # Formatting exchanges source text, not normalized log lines.
                for stream in (process.stdin, process.stdout, process.stderr):
                    if stream is not None:
                        stream.reconfigure(newline="")
            while True:
                try:
                    wait = (
                        0.1
                        if self.timeout is None
                        else min(
                            0.1, max(0.0, self.timeout - (time.monotonic() - started))
                        )
                    )
                    stdout, stderr = process.communicate(input=input_text, timeout=wait)
                    code = process.returncode
                    break
                except subprocess.TimeoutExpired:
                    input_text = None
                    expired = (
                        self.timeout is not None
                        and time.monotonic() - started >= self.timeout
                    )
                    if self.cancelled.is_set() or expired:
                        status = "cancelled" if self.cancelled.is_set() else "timed_out"
                        self._kill(process, job)
                        stdout, stderr = process.communicate()
                        code = 130 if status == "cancelled" else 124
                        break
        except OSError as error:
            code, status, stderr = 2, "failed", str(error)
            if process and process.poll() is None:
                process.kill()
                process.communicate()
        finally:
            if job:
                job.terminate()
                job.close()
        if code and status == "completed":
            status = "failed"
        result = ProcessResult(
            command, code, stdout, stderr, time.monotonic() - started, status
        )
        if artifact:
            artifact.parent.mkdir(parents=True, exist_ok=True)
            artifact.with_suffix(".log").write_text(
                stdout + "\n" + stderr, encoding="utf-8", newline="\n"
            )
            write_json(artifact.with_suffix(".json"), asdict(result))
        return result

    @staticmethod
    def _kill(process: subprocess.Popen, job: WindowsJob | None) -> None:
        if os.name == "nt":
            job.terminate()
        else:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        if process.poll() is None:
            process.kill()


def bounded_map(
    worker: Callable[[T], R], items: Iterable[T], jobs: int, runner: Runner
) -> Iterable[tuple[T, R | Exception]]:
    """Keep at most jobs futures outstanding and drain all dispatched work."""
    iterator = iter(items)
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        pending = {}

        def fill() -> None:
            while len(pending) < jobs and not runner.cancelled.is_set():
                try:
                    item = next(iterator)
                except StopIteration:
                    break
                pending[pool.submit(worker, item)] = item

        fill()
        while pending:
            done, _ = concurrent.futures.wait(
                pending, return_when=concurrent.futures.FIRST_COMPLETED
            )
            for future in done:
                item = pending.pop(future)
                try:
                    value = future.result()
                except Exception as error:  # noqa: BLE001 -- preserve every worker failure in the run report
                    value = error
                yield item, value
            fill()


def executable(name: str, sibling: Path | None = None) -> str:
    found = shutil.which(name)
    if not found and sibling:
        candidate = sibling.parent / (
            name + (".exe" if os.name == "nt" and not name.endswith(".exe") else "")
        )
        if candidate.is_file():
            found = str(candidate)
    if not found:
        raise ToolError(
            f"Executable not found: {name}. Install LLVM or provide its executable path."
        )
    return str(Path(found).resolve())


def checked(result: ProcessResult, operation: str) -> str:
    if result.returncode:
        raise ToolError(
            f"{operation} {result.status}: {result.stderr.strip() or result.stdout.strip()}"
        )
    return result.stdout
