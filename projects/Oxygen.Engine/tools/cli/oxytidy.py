#!/usr/bin/env python3
"""
Run clang-tidy against a scoped subset of the repo using the project compile
database, .clang-tidy, and .clangd Add/Remove flag adjustments.

The tool is intentionally read-only:
- no autofix flags are ever emitted
- diagnostics are filtered to the selected scope roots
- logs and the sanitized compile database are written under out/clang-tidy/
"""

from __future__ import annotations

import argparse
import concurrent.futures
import ctypes
import json
import os
import re
import shlex
import signal
import subprocess
import sys
import threading
import time
from collections import Counter
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Iterable


WINDOWS = os.name == "nt"
WINDOWS_CTRL_C_EXIT = 0xC000013A


@dataclass(frozen=True)
class ClangdConfig:
    compilation_database: Path
    add_args: tuple[str, ...]
    remove_args: tuple[str, ...]


@dataclass(frozen=True)
class FileResult:
    file: str
    returncode: int
    diagnostics: tuple[dict[str, str], ...]
    external_errors: int
    log_file: str


class ProgressReporter:
    def __init__(self, total: int, workers: int, started_at: float) -> None:
        self.total = total
        self.workers = workers
        self.started_at = started_at
        self.started = 0
        self.finished = 0
        self.active = 0
        self._lock = threading.Lock()

    def start(self, file_path: str) -> None:
        with self._lock:
            self.started += 1
            self.active += 1
            started = self.started
            active = self.active
        elapsed = time.perf_counter() - self.started_at
        print(
            f"[start {started}/{self.total}] {Path(file_path).name} "
            f"(active {active}/{self.workers}, {elapsed:.1f}s)",
            flush=True,
        )

    def finish(self, result: FileResult) -> None:
        with self._lock:
            self.finished += 1
            self.active -= 1
            finished = self.finished
            active = self.active
        elapsed = time.perf_counter() - self.started_at
        status = f"{len(result.diagnostics)} in-scope diagnostic(s)"
        if result.returncode != 0:
            status += f", rc={result.returncode}"
        if result.external_errors:
            status += f", external-errors={result.external_errors}"
        print(
            f"[done  {finished}/{self.total}] {Path(result.file).name}: {status} "
            f"(active {active}/{self.workers}, {elapsed:.1f}s)",
            flush=True,
        )


class ProcessRegistry:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._active: dict[int, subprocess.Popen[str]] = {}
        self.cancel_event = threading.Event()
        self._interrupt_count = 0

    def register(self, process: subprocess.Popen[str]) -> None:
        with self._lock:
            self._active[process.pid] = process

    def unregister(self, process: subprocess.Popen[str]) -> None:
        with self._lock:
            self._active.pop(process.pid, None)

    def request_cancel(self, *, force_kill: bool = False) -> None:
        with self._lock:
            self.cancel_event.set()
            if force_kill:
                self._interrupt_count = max(self._interrupt_count, 2)
            else:
                self._interrupt_count += 1
            processes = list(self._active.values())
        action = "Killing" if self._interrupt_count >= 2 else "Stopping"
        print(f"\n{action} active clang-tidy processes...", flush=True)
        for process in processes:
            try:
                if self._interrupt_count >= 2:
                    process.kill()
                else:
                    process.terminate()
            except OSError:
                pass


REGISTRY = ProcessRegistry()


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def parse_inline_list(text: str, key: str) -> tuple[str, ...]:
    match = re.search(rf"{re.escape(key)}:\s*\[([^\]]*)\]", text, re.S)
    if not match:
        return ()
    parts = []
    for raw in match.group(1).split(","):
        item = raw.strip()
        if not item:
            continue
        if len(item) >= 2 and item[0] == item[-1] and item[0] in {"'", '"'}:
            item = item[1:-1]
        parts.append(item)
    return tuple(parts)


def parse_clangd(clangd_path: Path) -> ClangdConfig:
    text = read_text(clangd_path)
    match = re.search(r'CompilationDatabase:\s*"([^"]+)"', text)
    if not match:
        raise SystemExit(f"Could not find CompilationDatabase in {clangd_path}")
    compilation_database = Path(match.group(1))
    if not compilation_database.is_absolute():
        compilation_database = (clangd_path.parent / compilation_database).resolve()
    return ClangdConfig(
        compilation_database=compilation_database,
        add_args=parse_inline_list(text, "Add"),
        remove_args=parse_inline_list(text, "Remove"),
    )


if WINDOWS:
    _kernel32 = ctypes.windll.kernel32
    _shell32 = ctypes.windll.shell32
    _kernel32.LocalFree.argtypes = [ctypes.c_void_p]
    _shell32.CommandLineToArgvW.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    _shell32.CommandLineToArgvW.restype = ctypes.POINTER(ctypes.c_wchar_p)


def split_command_line(command: str) -> list[str]:
    if WINDOWS:
        argc = ctypes.c_int()
        argv = _shell32.CommandLineToArgvW(command, ctypes.byref(argc))
        if not argv:
            raise OSError("CommandLineToArgvW failed")
        try:
            return [argv[index] for index in range(argc.value)]
        finally:
            _kernel32.LocalFree(argv)
    return shlex.split(command)


def should_remove_arg(arg: str, remove_patterns: Iterable[str]) -> bool:
    if arg.startswith("@"):
        return True
    for pattern in remove_patterns:
        if pattern.endswith("*"):
            if arg.startswith(pattern[:-1]):
                return True
        elif arg == pattern:
            return True
    return False


def normalize_path(path: Path) -> str:
    return str(path.resolve(strict=False)).replace("\\", "/").casefold()


def is_under_any(path: Path, roots: Iterable[Path]) -> bool:
    normalized = normalize_path(path)
    for root in roots:
        root_prefix = normalize_path(root)
        if normalized == root_prefix or normalized.startswith(root_prefix + "/"):
            return True
    return False


def infer_scope_name(roots: list[Path]) -> str:
    if len(roots) == 1:
        return roots[0].name.lower()
    return "multi"


def output_matches_configuration(output: str, configuration: str) -> bool:
    if not output:
        return True
    normalized = output.replace("\\", "/")
    return f"/{configuration}/" in normalized


def select_entries(
    compile_commands: list[dict[str, object]],
    scope_roots: list[Path],
    configuration: str,
    include_tests: bool,
    max_files: int | None,
) -> list[dict[str, object]]:
    selected: list[dict[str, object]] = []
    seen: set[str] = set()
    for entry in compile_commands:
        file_path = Path(str(entry["file"])).resolve(strict=False)
        if file_path.suffix.lower() not in {".c", ".cc", ".cpp", ".cxx"}:
            continue
        if not is_under_any(file_path, scope_roots):
            continue
        if not include_tests and "Test" in file_path.parts:
            continue
        output = str(entry.get("output", ""))
        if not output_matches_configuration(output, configuration):
            continue
        normalized = normalize_path(file_path)
        if normalized in seen:
            continue
        seen.add(normalized)
        selected.append(entry)
        if max_files is not None and len(selected) >= max_files:
            break
    return selected


def sanitize_compile_db_entries(
    entries: list[dict[str, object]],
    clangd_config: ClangdConfig,
) -> list[dict[str, object]]:
    sanitized = []
    for entry in entries:
        if "arguments" in entry:
            args = [str(arg) for arg in entry["arguments"]]
        else:
            args = split_command_line(str(entry["command"]))
        args = [arg for arg in args if not should_remove_arg(arg, clangd_config.remove_args)]
        args.extend(clangd_config.add_args)
        sanitized.append(
            {
                "directory": entry["directory"],
                "file": entry["file"],
                "arguments": args,
                "output": entry.get("output", ""),
            }
        )
    return sanitized


def path_regex_fragment(path: Path, repo_root: Path) -> str:
    relative = path.resolve(strict=False).relative_to(repo_root.resolve(strict=False))
    parts = [re.escape(part) for part in relative.parts]
    return r"[/\\]".join(parts)


def make_header_filter(scope_roots: list[Path], repo_root: Path) -> str:
    fragments = [path_regex_fragment(path, repo_root) for path in scope_roots]
    joined = "|".join(fragments)
    return rf"^.*(?:{joined}).*$"


def make_exclude_filter(scope_roots: list[Path], repo_root: Path) -> str | None:
    test_roots = []
    for root in scope_roots:
        candidate = root / "Test"
        if candidate.exists():
            test_roots.append(candidate)
    if not test_roots:
        return None
    return make_header_filter(test_roots, repo_root)


def make_log_dir(repo_root: Path, scope_name: str, explicit: str | None) -> Path:
    if explicit:
        return Path(explicit).resolve()
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    return repo_root / "out" / "clang-tidy" / f"{scope_name}-{stamp}"


def parse_diagnostics(
    text: str,
    scope_roots: list[Path],
) -> tuple[list[dict[str, str]], int]:
    diagnostic_re = re.compile(
        r"^(?P<file>[A-Za-z]:[^:]+|\./[^:]+|/[^:]+):(?P<line>\d+):(?P<column>\d+): "
        r"(?P<level>warning|error): (?P<message>.*?)(?: \[(?P<check>[^\]]+)])?$",
        re.M,
    )
    diagnostics: list[dict[str, str]] = []
    external_errors = 0
    for match in diagnostic_re.finditer(text):
        path = Path(match.group("file")).resolve(strict=False)
        record = {
            "file": str(path),
            "line": match.group("line"),
            "column": match.group("column"),
            "level": match.group("level"),
            "message": match.group("message"),
            "check": match.group("check") or "(no-check-id)",
        }
        if is_under_any(path, scope_roots):
            diagnostics.append(record)
        elif record["level"] == "error":
            external_errors += 1
    return diagnostics, external_errors


def run_file(
    *,
    file_path: str,
    repo_root: Path,
    run_dir: Path,
    config_file: Path | None,
    clang_tidy_bin: str,
    header_filter: str,
    exclude_filter: str | None,
    checks_override: str | None,
    quiet: bool,
    scope_roots: list[Path],
    reporter: ProgressReporter | None,
) -> FileResult:
    if REGISTRY.cancel_event.is_set():
        return FileResult(
            file=file_path,
            returncode=130,
            diagnostics=(),
            external_errors=0,
            log_file="",
        )

    if reporter is not None:
        reporter.start(file_path)
    command = [
        clang_tidy_bin,
        file_path,
        "-p",
        str(run_dir),
        "--use-color=false",
    ]
    if config_file is not None:
        command.extend(["--config-file", str(config_file)])
    if quiet:
        command.append("--quiet")
    if header_filter:
        command.append(f"--header-filter={header_filter}")
    if exclude_filter:
        command.append(f"--exclude-header-filter={exclude_filter}")
    if checks_override:
        command.append(f"--checks={checks_override}")

    process = subprocess.Popen(
        command,
        cwd=repo_root,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        encoding="utf-8",
        errors="replace",
    )
    REGISTRY.register(process)
    try:
        stdout, stderr = process.communicate()
    finally:
        REGISTRY.unregister(process)
    text = stdout or ""
    if stderr:
        if text and not text.endswith("\n"):
            text += "\n"
        text += stderr

    log_file = run_dir / f"{Path(file_path).name}.log"
    log_file.write_text(text, encoding="utf-8")
    diagnostics, external_errors = parse_diagnostics(text, scope_roots)
    return_code = process.returncode
    if return_code == WINDOWS_CTRL_C_EXIT:
        return_code = 130
    if REGISTRY.cancel_event.is_set() and return_code not in {0, 130}:
        return_code = 130
    result = FileResult(
        file=file_path,
        returncode=return_code,
        diagnostics=tuple(diagnostics),
        external_errors=external_errors,
        log_file=str(log_file),
    )
    if reporter is not None:
        reporter.finish(result)
    return result


def parse_args(argv: list[str]) -> argparse.Namespace:
    examples = """examples:
  python tools/cli/oxytidy.py
  python tools/cli/oxytidy.py src/Oxygen/Vortex --summary-only
  python tools/cli/oxytidy.py src/Oxygen/Vortex --jobs 6 --configuration Debug
  python tools/cli/oxytidy.py src/Oxygen/Vortex/ScenePrep/ScenePrepPipeline.cpp --max-files 1
  python tools/cli/oxytidy.py src/Oxygen/Vortex src/Oxygen/Scene --include-tests
"""
    parser = argparse.ArgumentParser(
        description=(
            "Run clang-tidy against selected source roots using the repo compile "
            "database, .clang-tidy, and .clangd flag sanitation."
        ),
        epilog=examples,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "paths",
        nargs="*",
        default=["src/Oxygen/Vortex"],
        help="File or directory roots to analyze. Defaults to src/Oxygen/Vortex.",
    )
    parser.add_argument(
        "--configuration",
        default="Debug",
        help="Build configuration slice to select from a multi-config compile database.",
    )
    parser.add_argument(
        "--build-dir",
        help="Override the compilation database directory. Defaults to .clangd.",
    )
    parser.add_argument(
        "--config-file",
        help=(
            "Optional explicit clang-tidy config file path. By default clang-tidy "
            "discovers the nearest .clang-tidy file for each analyzed file."
        ),
    )
    parser.add_argument(
        "--clangd-file",
        default=".clangd",
        help="Path to the clangd config file. Defaults to .clangd.",
    )
    parser.add_argument(
        "--clang-tidy-bin",
        default="clang-tidy",
        help="clang-tidy executable name or path.",
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=max(1, min(8, os.cpu_count() or 1)),
        help="Parallel clang-tidy worker count. Defaults to min(cpu_count, 8).",
    )
    parser.add_argument(
        "--include-tests",
        action="store_true",
        help="Include files under Test/ in the selected scope roots.",
    )
    parser.add_argument(
        "--max-files",
        type=int,
        help="Limit the number of selected translation units. Useful for smoke tests.",
    )
    parser.add_argument(
        "--checks",
        help="Optional clang-tidy checks override string appended as --checks=...",
    )
    parser.add_argument(
        "--log-dir",
        help="Explicit output directory for logs and the sanitized compile database.",
    )
    parser.add_argument(
        "--list-files",
        action="store_true",
        help="List the selected translation units and exit without running clang-tidy.",
    )
    parser.add_argument(
        "--summary-only",
        action="store_true",
        help="Print only the summary instead of replaying per-diagnostic output.",
    )
    parser.add_argument(
        "--no-quiet",
        action="store_true",
        help="Do not pass --quiet to clang-tidy.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    started_at = time.perf_counter()
    repo_root = repo_root_from_script()

    def handle_interrupt(_signum: int, _frame: object) -> None:
        REGISTRY.request_cancel()

    signal.signal(signal.SIGINT, handle_interrupt)
    if hasattr(signal, "SIGBREAK"):
        signal.signal(signal.SIGBREAK, handle_interrupt)
    clangd_path = (repo_root / args.clangd_file).resolve()
    config_file = None
    if args.config_file:
        config_file = (repo_root / args.config_file).resolve()
    clangd_config = parse_clangd(clangd_path)
    compile_db_dir = Path(args.build_dir).resolve() if args.build_dir else clangd_config.compilation_database
    compile_db_path = compile_db_dir / "compile_commands.json"
    if not compile_db_path.exists():
        raise SystemExit(f"Missing compile_commands.json: {compile_db_path}")
    if config_file is not None and not config_file.exists():
        raise SystemExit(f"Missing clang-tidy config file: {config_file}")

    scope_roots = []
    for raw in args.paths:
        path = Path(raw)
        if not path.is_absolute():
            path = repo_root / path
        scope_roots.append(path.resolve(strict=False))

    compile_commands = json.loads(read_text(compile_db_path))
    selected = select_entries(
        compile_commands=compile_commands,
        scope_roots=scope_roots,
        configuration=args.configuration,
        include_tests=args.include_tests,
        max_files=args.max_files,
    )
    if not selected:
        raise SystemExit("No matching translation units were found in compile_commands.json")

    if args.list_files:
        for entry in selected:
            print(entry["file"])
        return 0

    print(
        f"Selected {len(selected)} translation unit(s) from "
        f"{', '.join(str(path.relative_to(repo_root)) if path.is_relative_to(repo_root) else str(path) for path in scope_roots)}",
        flush=True,
    )
    print(f"Using compile database: {compile_db_path}", flush=True)
    if config_file is None:
        print("Using clang-tidy config discovery: nearest .clang-tidy per file", flush=True)
    else:
        print(f"Using clang-tidy config: {config_file}", flush=True)

    sanitized = sanitize_compile_db_entries(selected, clangd_config)
    log_dir = make_log_dir(repo_root, infer_scope_name(scope_roots), args.log_dir)
    log_dir.mkdir(parents=True, exist_ok=True)
    sanitized_db_path = log_dir / "compile_commands.json"
    sanitized_db_path.write_text(json.dumps(sanitized, indent=2), encoding="utf-8")
    print(f"Wrote sanitized compile database: {sanitized_db_path}", flush=True)

    header_filter = make_header_filter(scope_roots, repo_root)
    exclude_filter = None if args.include_tests else make_exclude_filter(scope_roots, repo_root)
    worker_count = max(1, args.jobs)
    print(f"Dispatching {len(sanitized)} translation unit(s) across {worker_count} worker(s)...", flush=True)
    reporter = ProgressReporter(total=len(sanitized), workers=worker_count, started_at=started_at)

    executor = concurrent.futures.ThreadPoolExecutor(max_workers=worker_count)
    results: list[FileResult] = []
    try:
        futures = [
            executor.submit(
                run_file,
                file_path=str(entry["file"]),
                repo_root=repo_root,
                run_dir=log_dir,
                config_file=config_file,
                clang_tidy_bin=args.clang_tidy_bin,
                header_filter=header_filter,
                exclude_filter=exclude_filter,
                checks_override=args.checks,
                quiet=not args.no_quiet,
                scope_roots=scope_roots,
                reporter=reporter,
            )
            for entry in sanitized
        ]

        for future in concurrent.futures.as_completed(futures):
            results.append(future.result())
            if REGISTRY.cancel_event.is_set():
                break
    except KeyboardInterrupt:
        REGISTRY.request_cancel(force_kill=True)
    finally:
        executor.shutdown(wait=True, cancel_futures=True)

    results.sort(key=lambda item: item.file.casefold())

    diagnostics = [diagnostic for result in results for diagnostic in result.diagnostics]
    external_errors = sum(result.external_errors for result in results)
    failures = [result for result in results if result.returncode != 0]

    level_counts = Counter(diagnostic["level"] for diagnostic in diagnostics)
    check_counts = Counter(diagnostic["check"] for diagnostic in diagnostics)
    file_counts = Counter(diagnostic["file"] for diagnostic in diagnostics)

    summary = {
        "scope_roots": [str(path) for path in scope_roots],
        "cancelled": REGISTRY.cancel_event.is_set(),
        "configuration": args.configuration,
        "jobs": args.jobs,
        "build_dir": str(compile_db_dir),
        "clang_tidy_config": str(config_file) if config_file is not None else "(discovered)",
        "clangd_file": str(clangd_path),
        "sanitized_compile_database": str(sanitized_db_path),
        "files_analyzed": len(results),
        "returncode_failures": [result.file for result in failures],
        "external_error_count": external_errors,
        "level_counts": dict(level_counts),
        "check_counts": dict(check_counts),
        "file_counts": dict(file_counts),
        "per_file": [
            {
                "file": result.file,
                "returncode": result.returncode,
                "diagnostics": len(result.diagnostics),
                "external_errors": result.external_errors,
                "log_file": result.log_file,
            }
            for result in results
        ],
    }
    summary_path = log_dir / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")

    print(f"Log directory: {log_dir}")
    print(f"Sanitized compile database: {sanitized_db_path}")
    print(f"Summary: {summary_path}")
    print(f"Files analyzed: {len(results)}")
    print(f"Diagnostics in scope: {level_counts.get('warning', 0)} warnings, {level_counts.get('error', 0)} errors")
    if external_errors:
        print(f"External errors outside scope: {external_errors}")
    if failures and not REGISTRY.cancel_event.is_set():
        print(f"clang-tidy returned nonzero for {len(failures)} translation unit(s)")
    print("Top checks:")
    for name, count in check_counts.most_common(10):
        print(f"  {count:4d}  {name}")
    print("Top files:")
    for name, count in file_counts.most_common(10):
        print(f"  {count:4d}  {name}")

    if not args.summary_only:
        for diagnostic in diagnostics:
            print(
                f"{diagnostic['file']}:{diagnostic['line']}:{diagnostic['column']}: "
                f"{diagnostic['level']}: {diagnostic['message']} [{diagnostic['check']}]"
            )

    if REGISTRY.cancel_event.is_set():
        print("clang-tidy run cancelled.", flush=True)
        return 130

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
