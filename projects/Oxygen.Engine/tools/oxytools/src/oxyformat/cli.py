"""The shared console, module, PowerShell, and hook entry point."""

from __future__ import annotations

import argparse
import math
import os
import signal
import tempfile
import time
from collections import Counter
from pathlib import Path


def positive_int(value: str) -> int:
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def positive_float(value: str) -> float:
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("must be finite and positive")
    return number


def parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="oxyformat",
        description="Check C++ files against the repository's required formatting.",
    )
    parser.add_argument(
        "paths",
        nargs="*",
        help="Files or recursive directories, relative to the engine root",
    )
    parser.add_argument(
        "--all", action="store_true", help="Select all owned C++ files, including tests"
    )
    parser.add_argument(
        "--fix",
        action="store_true",
        help="Format files, continuing after individual failures",
    )
    parser.add_argument(
        "--project-root",
        metavar="PATH",
        help="Engine root, relative to the current directory",
    )
    parser.add_argument(
        "--paths-from-cwd",
        action="store_true",
        help="Resolve input paths from the current directory (for pre-commit filenames)",
    )
    parser.add_argument(
        "--clang-format-bin", metavar="PATH", help="LLVM 22.x formatter executable"
    )
    parser.add_argument(
        "--jobs", type=positive_int, default=min(8, os.cpu_count() or 1), metavar="N"
    )
    parser.add_argument(
        "--timeout",
        type=positive_float,
        default=30.0,
        metavar="SECONDS",
        help="Timeout per LLVM invocation (default: 30)",
    )
    args = parser.parse_args(argv)
    if bool(args.paths) == args.all:
        parser.error("Specify file/directory paths or --all")
    return args


def find_root() -> Path:
    # Source-checkout discovery matches oxytidy; installed wheels use the cwd.
    for start in (Path(__file__).resolve().parent, Path.cwd()):
        for candidate in (start, *start.parents):
            if (candidate / ".oxytools.json").is_file():
                return candidate
    raise ValueError(
        "Cannot find .oxytools.json; select the engine with --project-root PATH"
    )


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    # Help/argument errors do not pay the schema and formatter import cost.
    from oxytools.common import ToolError
    from oxytools.ownership import Ownership

    from .engine import Cancelled, Formatter, Processes, find_formatter, prepare_style
    from .reporting import Reporter
    from .selection import select_files

    started = time.monotonic()
    processes = Processes(args.timeout)
    reporter = Reporter()
    processing = False
    previous = {}
    try:
        for name in ("SIGINT", "SIGBREAK"):
            if hasattr(signal, name):
                sig = getattr(signal, name)
                previous[sig] = signal.signal(sig, lambda *_: processes.cancelled.set())
        root = Path(args.project_root).resolve() if args.project_root else find_root()
        base = Path.cwd() if args.paths_from_cwd else root
        paths = [base / name for name in args.paths]
        reporter.start(root, paths, fix=args.fix, all_project=args.all)
        policy = Ownership.load(root)
        paths = policy.project_roots if args.all else paths
        selection = select_files(policy, paths)
        counts = Counter(failed=len(selection.errors), skipped=selection.skipped)
        for path, error in selection.errors:
            reporter.error(path, error)
        if selection.files:
            binary = find_formatter(args.clang_format_bin)
            with tempfile.TemporaryDirectory(prefix="oxyformat-") as directory:
                style, source = prepare_style(binary, root, Path(directory), processes)
                formatter = Formatter(
                    binary, style, root / ".clang-format", source, args.fix, processes
                )
                processing = True
                for result in formatter.run(selection.files, args.jobs):
                    counts[result.status] += 1
                    if result.status in {"needed", "changed"}:
                        reporter.file(result.path)
                    elif result.error:
                        reporter.error(
                            result.path,
                            result.error,
                            cancelled=result.status == "cancelled",
                        )
        reporter.finish(
            counts,
            len(selection.files),
            time.monotonic() - started,
            cancelled=processes.cancelled.is_set(),
        )
        if processes.cancelled.is_set():
            return 130
        return 2 if counts["failed"] else 1 if counts["needed"] else 0
    except Cancelled:
        reporter.finish({}, 0, time.monotonic() - started, cancelled=True)
        return 130
    except (OSError, ValueError, ToolError) as error:
        reporter.failure(str(error), started=processing)
        return 2
    finally:
        for sig, handler in previous.items():
            signal.signal(sig, handler)
