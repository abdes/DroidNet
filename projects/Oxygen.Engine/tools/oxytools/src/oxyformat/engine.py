"""Bounded formatting with immutable style snapshots and per-file writes."""

from __future__ import annotations

import concurrent.futures
import os
import shutil
import stat
import subprocess
import threading
import time
from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from pathlib import Path

from oxytools.common import ToolError, yaml_documents
from oxytools.files import atomic_write
from oxytools.includes import prepare_includes
from oxytools.llvm import REQUIRED_LLVM_MAJOR, require_version
from oxytools.process import WindowsJob


class Cancelled(ToolError):
    """The run was interrupted by the user."""


class Processes:
    def __init__(self, timeout: float) -> None:
        self.timeout = timeout
        self.cancelled = threading.Event()

    def run(self, command: list[str], data: bytes | None = None) -> bytes:
        if self.cancelled.is_set():
            raise Cancelled("Cancelled before dispatch")
        process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
        )
        job = None
        started = time.monotonic()
        try:
            if os.name == "nt":
                job = WindowsJob(process)
            while True:
                if self.cancelled.is_set():
                    raise Cancelled("Cancelled during formatting")
                remaining = self.timeout - (time.monotonic() - started)
                if remaining <= 0:
                    raise ToolError(f"Formatter timed out after {self.timeout:g}s")
                try:
                    stdout, stderr = process.communicate(
                        input=data, timeout=min(0.1, remaining)
                    )
                    break
                except subprocess.TimeoutExpired:
                    data = None
            if process.returncode:
                detail = stderr.decode("utf-8", errors="replace").strip()
                raise ToolError(
                    detail or f"Formatter exited with status {process.returncode}"
                )
            return stdout
        finally:
            if job:
                job.terminate()
                job.close()
            if process.poll() is None:
                process.kill()
            process.communicate()


def find_formatter(explicit: str | None) -> str:
    if explicit:
        found = shutil.which(explicit)
    else:
        found = shutil.which("clang-format")
        if not found and os.name == "nt":
            candidate = (
                Path(os.environ.get("ProgramFiles", "C:/Program Files"))
                / "LLVM/bin/clang-format.exe"
            )
            found = str(candidate) if candidate.is_file() else None
    if not found:
        raise ToolError(
            f"clang-format {REQUIRED_LLVM_MAJOR}.x was not found; "
            "install LLVM or use --clang-format-bin PATH"
        )
    return str(Path(found).resolve())


def prepare_style(
    binary: str, root: Path, temporary: Path, processes: Processes
) -> tuple[Path, bytes]:
    version = processes.run([binary, "--version"]).decode("utf-8", errors="replace")
    require_version("clang-format", version)
    config = root / ".clang-format"
    contents = config.read_bytes()
    if any(
        isinstance(document, dict)
        and document.get("BasedOnStyle") == "InheritParentConfig"
        for document in yaml_documents(contents.decode("utf-8-sig"))
    ):
        raise ToolError(
            "Root .clang-format must not inherit configuration from outside the project"
        )
    # Resolve the root's complete C++ style once. Workers use the same snapshot,
    # including when a nested .clang-format or .clang-format-ignore exists.
    effective = processes.run(
        [
            binary,
            f"--style=file:{config}",
            "--assume-filename=oxyformat.cpp",
            "--dump-config",
        ]
    )
    if config.read_bytes() != contents:
        raise ToolError("Root .clang-format changed while preparing the run")
    snapshot = temporary / ".clang-format"
    snapshot.write_bytes(effective)
    return snapshot, contents


@dataclass
class Result:
    path: Path
    status: str
    error: str = ""


class Formatter:
    def __init__(
        self,
        binary: str,
        style: Path,
        root_style: Path,
        original_style: bytes,
        fix: bool,
        processes: Processes,
    ) -> None:
        self.binary = binary
        self.style = style
        self.root_style = root_style
        self.original_style = original_style
        self.fix = fix
        self.processes = processes

    def format(self, path: Path) -> Result:
        try:
            original = path.read_bytes()
            # clang-format accepts several encodings; the repository write
            # contract is UTF-8, optionally with a BOM, without transcoding.
            original.decode("utf-8-sig")
            mode = stat.S_IMODE(path.stat().st_mode)
            normalized, _ = prepare_includes(original)
            formatted = self.processes.run(
                [
                    self.binary,
                    f"--style=file:{self.style}",
                    f"--assume-filename={path}",
                    "--fail-on-incomplete-format",
                    "--Werror",
                ],
                normalized,
            )
            if self.processes.cancelled.is_set():
                raise Cancelled("Cancelled before writing")
            if path.read_bytes() != original:
                raise ToolError(
                    "Contents changed during formatting; file was not overwritten"
                )
            if self.root_style.read_bytes() != self.original_style:
                raise ToolError("Root .clang-format changed during the run")
            if formatted == original:
                return Result(path, "unchanged")
            if not self.fix:
                return Result(path, "needed")
            # Preserve the actual filename spelling; normalized identity keys
            # must never become write destinations on case-insensitive systems.
            atomic_write(path, formatted, mode, expected=original)
            return Result(path, "changed")
        except Cancelled as error:
            return Result(path, "cancelled", str(error))
        except (OSError, ValueError, ToolError) as error:
            return Result(path, "failed", str(error))

    def run(self, paths: Iterable[Path], jobs: int) -> Iterator[Result]:
        iterator = iter(paths)
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
            pending = {}
            while True:
                while len(pending) < jobs and not self.processes.cancelled.is_set():
                    try:
                        path = next(iterator)
                    except StopIteration:
                        break
                    pending[pool.submit(self.format, path)] = path
                if not pending:
                    break
                done, _ = concurrent.futures.wait(
                    pending, return_when=concurrent.futures.FIRST_COMPLETED
                )
                for future in done:
                    path = pending.pop(future)
                    try:
                        yield future.result()
                    except Exception as error:  # noqa: BLE001 -- keep failures local to one file
                        yield Result(path, "failed", str(error))
