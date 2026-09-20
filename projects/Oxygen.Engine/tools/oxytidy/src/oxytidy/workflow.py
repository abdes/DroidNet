"""Discover project coverage, analyze, optionally fix, then verify."""

from __future__ import annotations

import argparse
import math
import os
import re
import signal
import sys
import tempfile
import time
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path

from rich_argparse import RichHelpFormatter

from .analysis import Analyzer, Prepared
from .common import (
    ToolError,
    absolute,
    display,
    file_hash,
    path_key,
    unchanged,
    write_json,
)
from .compilation import HEADERS, parse_clangd, read_database
from .diagnostics import deduplicate, scoped
from .execution import Runner, bounded_map, checked, executable
from .fixes import apply_fixes, plan_fixes
from .scope import Scope


def positive_int(value: str) -> int:
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return number


def positive_float(value: str) -> float:
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("must be a finite positive number")
    return number


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="oxytidy",
        formatter_class=RichHelpFormatter,
        usage="%(prog)s [OPTIONS] [PATH ...]",
        description=__doc__,
        epilog="Examples: oxytidy src/Oxygen/Base; oxytidy src/Oxygen/Base/Foo.h --fix; oxytidy --all --configuration Debug --incremental",
    )
    scope_options = parser.add_argument_group("Scope")
    build_options = parser.add_argument_group("Build configuration")
    execution_options = parser.add_argument_group("Execution")
    output_options = parser.add_argument_group("Output")
    fix_options = parser.add_argument_group("Fixes")
    parser.add_argument(
        "paths",
        nargs="*",
        help="Project source/header files or directories, relative to the engine root",
    )
    scope_options.add_argument(
        "--project-root",
        metavar="PATH",
        help="Target engine checkout, relative to the current directory; overrides automatic root discovery",
    )
    scope_options.add_argument(
        "--ownership-file",
        metavar="PATH",
        help="Project ownership JSON, relative to the current directory; otherwise use the target checkout policy, then the tool/run-location policy",
    )
    scope_options.add_argument(
        "--all", action="store_true", help="Analyze all project roots in .oxytidy.json"
    )
    build_options.add_argument(
        "--configuration",
        metavar="NAME",
        default="Debug",
        help="Configuration to select (default Debug); 'all' explicitly retains every database context",
    )
    build_options.add_argument(
        "--build-dir",
        metavar="DIR",
        help="Compilation database directory (relative to the current directory)",
    )
    build_options.add_argument(
        "--clangd-file",
        metavar="PATH",
        default=".clangd",
        help="Unconditional compile adjustments, relative to the engine root",
    )
    build_options.add_argument(
        "--config-file",
        metavar="PATH",
        help="Explicit clang-tidy configuration, relative to the engine root",
    )
    build_options.add_argument("--clang-tidy-bin", metavar="EXE", default="clang-tidy")
    build_options.add_argument(
        "--clang-scan-deps-bin", metavar="EXE", default="clang-scan-deps"
    )
    build_options.add_argument(
        "--clang-format-bin", metavar="EXE", default="clang-format"
    )
    execution_options.add_argument(
        "--jobs", metavar="N", type=positive_int, default=min(8, os.cpu_count() or 1)
    )
    execution_options.add_argument(
        "--timeout",
        metavar="SECONDS",
        type=positive_float,
        help="Timeout in seconds for each tool invocation",
    )
    scope_options.add_argument(
        "--include-tests", action="store_true", help="Include project Test directories"
    )
    execution_options.add_argument(
        "--max-files",
        metavar="N",
        type=positive_int,
        help="Cap analysis contexts; omitted work is reported as incomplete coverage",
    )
    build_options.add_argument(
        "--checks",
        help="Checks appended to config; begin with '-*,' to replace the configured checks",
    )
    execution_options.add_argument(
        "--fail-on", choices=["none", "warning", "error"], default="none"
    )
    output_options.add_argument(
        "--log-dir", metavar="DIR", help="Parent for a unique run directory"
    )
    output_options.add_argument(
        "--list-files",
        action="store_true",
        help="Discover coverage without running tidy checks",
    )
    output_options.add_argument(
        "--summary-only",
        action="store_true",
        help="Hide individual diagnostics; retain run context, progress, and summary",
    )
    output_options.add_argument(
        "--no-quiet",
        action="store_true",
        help="Retain LLVM suppression statistics in raw logs",
    )
    mode = fix_options.add_mutually_exclusive_group()
    mode.add_argument(
        "--fix",
        action="store_true",
        help="Apply a validated replacement batch, then reanalyze",
    )
    mode.add_argument(
        "--export-fixes",
        metavar="PATH",
        help="Export the validated replacement plan as JSON without editing sources",
    )
    fix_options.add_argument(
        "--format",
        action="store_true",
        help="Format changed ranges using project .clang-format; requires --fix",
    )
    execution_options.add_argument(
        "--incremental",
        action="store_true",
        help="Reuse analysis with matching configuration and freshly scanned dependency content",
    )
    execution_options.add_argument(
        "--force", action="store_true", help="Bypass cached analysis"
    )
    execution_options.add_argument(
        "--cache-dir",
        metavar="DIR",
        help="Analysis cache directory (default out/clang-tidy/cache)",
    )
    args = parser.parse_args(argv)
    if bool(args.paths) == args.all:
        parser.error("Specify file/directory paths or --all")
    if args.format and not args.fix:
        parser.error("--format requires --fix")
    if args.list_files and (args.fix or args.export_fixes):
        parser.error("--list-files cannot be combined with fix/export modes")
    return args


def resolve_tools(
    args, runner: Runner, run_dir: Path
) -> tuple[str, str, str | None, dict]:
    tidy = executable(args.clang_tidy_bin)
    scanner = executable(args.clang_scan_deps_bin, Path(tidy))
    formatter = executable(args.clang_format_bin, Path(tidy)) if args.format else None
    versions = {}
    for name, binary in [
        ("clang-tidy", tidy),
        ("clang-scan-deps", scanner),
        *([("clang-format", formatter)] if formatter else []),
    ]:
        version = checked(
            runner.run([binary, "--version"], run_dir, run_dir / (name + "-version")),
            f"Reading {name} version",
        ).strip()
        versions[name] = {
            "path": binary,
            "version": version,
            "binary_hash": file_hash(binary),
        }
        runner.reporter.tool(name, binary, version)
    majors = [
        re.search(r"version\s+(\d+)", item["version"]) for item in versions.values()
    ]
    if (
        any(match is None for match in majors)
        or len({match[1] for match in majors}) != 1
    ):
        raise ToolError("LLVM tools must have matching major versions")
    help_text = checked(
        runner.run([tidy, "--help"], run_dir), "Inspecting clang-tidy options"
    )
    for flag in (
        "--export-fixes",
        "--verify-config",
        "--dump-config",
        "--config-file",
        "--use-color",
    ):
        if flag not in help_text:
            raise ToolError(f"clang-tidy does not support required option {flag}")
    return tidy, scanner, formatter, versions


def outcome(
    results: list[dict],
    gaps: list,
    cancelled: bool,
    fail_on: str,
    diagnostics: list[dict],
) -> tuple[str, int]:
    if cancelled:
        return "cancelled", 130
    if gaps or any(result["status"] != "completed" for result in results):
        return "incomplete", 2
    if any(result.get("policy_failure") for result in results):
        return "findings", 1
    if diagnostics:
        fail = any(
            record["level"] == "error"
            or (fail_on == "warning" and record["level"] == "warning")
            for record in diagnostics
        )
        return "findings", 1 if fail_on != "none" and fail else 0
    return "clean", 0


def select_ownership_file(root: Path, explicit: str | None) -> tuple[Path, str]:
    """Only absent target policies fall back; invalid policies still fail."""
    if explicit:
        return Path(explicit).resolve(), "explicit --ownership-file"
    target = root / ".oxytidy.json"
    if target.exists():
        return target, "target checkout policy"
    try:
        fallback = engine_root() / ".oxytidy.json"
    except ToolError:
        return target, "target policy missing; no tool/run-location policy found"
    return fallback, f"tool/run-location fallback; target policy absent: {target}"


def run(args, root: Path, runner: Runner, run_dir: Path, summary: dict) -> int:
    ownership_file, ownership_reason = select_ownership_file(root, args.ownership_file)
    summary["ownership_file"] = str(ownership_file)
    summary["ownership_selection"] = ownership_reason
    runner.reporter.ownership(ownership_file, ownership_reason)
    scope = Scope.load(
        root, args.paths, args.all, args.include_tests, ownership_file=ownership_file
    )
    inputs = scope.inputs()
    runner.reporter.rows(
        [
            (
                "Owned roots",
                ", ".join(runner.reporter.path(path) for path in scope.project_roots),
            ),
            (
                "Exclusions",
                f"{len(scope.excludes)} policy patterns; system headers are dependencies only",
            ),
        ]
    )
    summary.update(
        scope=[str(path) for path in scope.roots],
        configuration=args.configuration,
        coverage_gaps=[],
        unreached_headers=[],
        header_discovery_complete=False,
        discovery_scope=[str(path) for path in scope.discovery_roots],
        selected=[],
        results=[],
        fixes={},
    )
    config_file = absolute(args.config_file, root) if args.config_file else None
    runner.reporter.rows(
        [
            (
                "Tidy config",
                config_file
                if config_file
                else "discover per source; LLVM resolves inheritance",
            )
        ]
    )
    if config_file and not config_file.is_file():
        raise ToolError(
            f"Missing clang-tidy configuration: {config_file}\n"
            "Select an existing file with --config-file PATH, or omit that "
            "option to use normal .clang-tidy discovery."
        )
    clangd_file = absolute(args.clangd_file, root)
    runner.reporter.rows([("Clangd", clangd_file)])
    summary["clangd_file"] = str(clangd_file)
    config = parse_clangd(
        clangd_file,
        Path(args.build_dir).resolve() if args.build_dir else None,
    )
    database = config.database / "compile_commands.json"
    database_reason = (
        "explicit --build-dir" if args.build_dir else "CompilationDatabase from .clangd"
    )
    summary.update(
        build_dir=str(config.database),
        compilation_database=str(database),
        database_selection=database_reason,
    )
    runner.reporter.rows(
        [
            ("Compile DB", database),
            ("DB choice", database_reason),
            ("Remove flags", ", ".join(config.remove) or "none"),
            ("Add flags", ", ".join(config.add) or "none"),
            ("Compiler", config.compiler or "preserve database compiler"),
        ]
    )
    contexts = read_database(
        database,
        config,
        None if args.configuration == "all" else args.configuration,
        source_filter=scope.discovery_contains,
    )
    candidates = contexts
    headers = {
        path_key(path): path for path in inputs if path.suffix.lower() in HEADERS
    }
    runner.reporter.rows(
        [
            (
                "Discovery scope",
                ", ".join(runner.reporter.path(path) for path in scope.discovery_roots),
            ),
            (
                "Discovery rule",
                "input directories; standalone headers use their containing directory",
            ),
            (
                "Contexts",
                f"{len(candidates)} in discovery scope match {args.configuration}",
            ),
        ]
    )
    runner.reporter.section("LLVM", [])
    tidy, scanner, formatter, versions = resolve_tools(args, runner, run_dir)
    summary["tools"] = versions
    analyzer = Analyzer(
        runner,
        tidy,
        scanner,
        versions,
        run_dir,
        config_file,
        args.checks,
        scope,
        Path(summary["cache_dir"]),
        args.incremental,
        args.force,
        not args.no_quiet,
    )
    runner.reporter.start_phase("Discovery", len(candidates))
    summary["phase"] = "discovery"
    prepared: list[Prepared] = []
    covered = set()
    for context, result in bounded_map(analyzer.prepare, candidates, args.jobs, runner):
        runner.reporter.advance(failed=isinstance(result, Exception))
        if isinstance(result, Exception):
            summary["coverage_gaps"].append(
                {
                    "file": str(context.file),
                    "context": context.identity,
                    "reason": str(result),
                }
            )
            continue
        dependencies = {path_key(path) for path in result.dependencies}
        reasons = []
        if scope.contains(context.file):
            covered.add(path_key(context.file))
            reasons.append(display(context.file, root))
        for key in headers.keys() & dependencies:
            covered.add(key)
            reasons.append(display(headers[key], root))
        if reasons:
            prepared.append(result)
            summary["selected"].append(
                {
                    "file": str(context.file),
                    "context": context.identity,
                    "reasons": sorted(reasons),
                }
            )
    runner.reporter.stop_phase()
    summary["header_discovery_complete"] = (
        not summary["coverage_gaps"] and not runner.cancelled.is_set()
    )
    context_sources = {path_key(context.file) for context in candidates}
    for path in inputs:
        if path.suffix.lower() in HEADERS:
            if path_key(path) not in covered:
                summary["unreached_headers"].append(str(path))
        elif path_key(path) not in context_sources:
            summary["coverage_gaps"].append(
                {
                    "file": str(path),
                    "reason": "No matching compile command",
                }
            )
    prepared.sort(key=lambda item: (path_key(item.context.file), item.context.identity))
    summary["selected"].sort(key=lambda item: (path_key(item["file"]), item["context"]))
    summary["selected_count"] = len(prepared)
    runner.reporter.rows(
        [
            (
                "Selection",
                f"{len(prepared)} contexts; {len(summary['unreached_headers'])} unreached headers; {len(summary['coverage_gaps'])} analysis gaps",
            )
        ]
    )
    if args.max_files and len(prepared) > args.max_files:
        summary["coverage_gaps"].append(
            {
                "reason": f"--max-files omitted {len(prepared) - args.max_files} compilation contexts"
            }
        )
        prepared = prepared[: args.max_files]
    scheduled = {item.context.identity for item in prepared}
    for item in summary["selected"]:
        item["scheduled"] = item["context"] in scheduled
    if args.list_files:
        for item in summary["selected"]:
            runner.reporter.listed_file(
                Path(item["file"]), item["context"], item["reasons"], item["scheduled"]
            )
        summary["status"] = (
            "cancelled"
            if runner.cancelled.is_set()
            else "incomplete"
            if summary["coverage_gaps"]
            else "listed"
        )
        return (
            130 if runner.cancelled.is_set() else 2 if summary["coverage_gaps"] else 0
        )

    if not prepared:
        summary["status"] = (
            "cancelled"
            if runner.cancelled.is_set()
            else "incomplete"
            if summary["coverage_gaps"]
            else "no_analysis"
        )
        if args.fix or args.export_fixes:
            summary["fixes"] = {
                "applied_files": [],
                "reason": "No analyzable contexts in scope; no fixes applied or exported",
            }
        return (
            130 if runner.cancelled.is_set() else 2 if summary["coverage_gaps"] else 0
        )

    snapshot = {}
    for item in prepared:
        for path, expected in item.snapshot.items():
            if path in snapshot and snapshot[path] != expected:
                raise ToolError(f"Dependency changed during discovery: {path}")
            snapshot[path] = expected
    summary["analysis_snapshot"] = snapshot
    seen = set()

    def collect(items: list[Prepared], verification: bool = False) -> list[dict]:
        results = []
        runner.reporter.start_phase(
            "Verification" if verification else "Analysis", len(items)
        )
        for item, value in bounded_map(
            lambda prepared: analyzer.analyze(prepared, verification=verification),
            items,
            args.jobs,
            runner,
        ):
            if isinstance(value, Exception):
                value = {
                    "id": item.context.identity,
                    "file": str(item.context.file),
                    "status": "failed",
                    "returncode": 2,
                    "duration": 0,
                    "diagnostics": [],
                    "reused": False,
                    "error": str(value),
                }
            results.append(value)
            runner.reporter.advance(failed=value["status"] != "completed")
            if value["status"] != "completed":
                runner.reporter.rows(
                    [
                        ("Failed", item.context.file),
                        (
                            "Reason",
                            value.get(
                                "error", f"{value['status']}; see the invocation log"
                            ),
                        ),
                    ]
                )
            if not args.summary_only:
                new_diagnostics = []
                for diagnostic in scoped(value["diagnostics"], scope):
                    if diagnostic["id"] not in seen:
                        new_diagnostics.append(diagnostic)
                        seen.add(diagnostic["id"])
                runner.reporter.diagnostics(new_diagnostics)
        runner.reporter.stop_phase()
        return results

    summary["phase"] = "analysis" if prepared else "discovery"
    summary["analysis_started"] = bool(prepared) and not runner.cancelled.is_set()
    summary["results"] = collect(prepared)
    diagnostics = deduplicate(
        scoped(
            [
                record
                for result in summary["results"]
                for record in result["diagnostics"]
            ],
            scope,
        )
    )
    summary["diagnostics"] = diagnostics
    status, code = outcome(
        summary["results"],
        summary["coverage_gaps"],
        runner.cancelled.is_set(),
        args.fail_on,
        diagnostics,
    )
    if (args.fix or args.export_fixes) and status not in {"incomplete", "cancelled"}:
        summary["phase"] = "fixes"
        if not unchanged(snapshot):
            raise ToolError(
                "Analyzed inputs changed; replacement batch was not applied"
            )
        edits, skipped = plan_fixes(diagnostics, scope, snapshot)
        summary["fixes"] = {"proposed": edits, "skipped": skipped, "applied_files": []}
        write_json(run_dir / "replacement-plan.json", summary["fixes"])
        if args.export_fixes:
            export_path = Path(args.export_fixes).resolve()
            if export_path.exists():
                raise ToolError(f"Refusing to overwrite existing export: {export_path}")
            write_json(export_path, {"snapshot": snapshot, **summary["fixes"]})
        if args.fix and edits:

            def format_ranges(
                path: Path, content: bytes, ranges: list[tuple[int, int]]
            ) -> bytes:
                command = [
                    formatter,
                    f"--assume-filename={path}",
                    "--style=file",
                    "--fallback-style=none",
                ]
                for offset, length in ranges:
                    command.extend(
                        [
                            f"--offset={min(offset, len(content))}",
                            f"--length={min(length, max(0, len(content) - offset))}",
                        ]
                    )
                return checked(
                    runner.run(
                        command,
                        path.parent,
                        input_text=content.decode("utf-8"),
                        preserve_newlines=True,
                    ),
                    f"Formatting {path}",
                ).encode("utf-8")

            analyzer.invalidate(prepared)
            applied = apply_fixes(
                edits,
                snapshot,
                format_ranges if formatter else None,
                runner.cancelled.is_set,
            )
            summary["fixes"]["applied_files"] = applied
            write_json(run_dir / "replacement-plan.json", summary["fixes"])
            verification = []
            runner.reporter.start_phase("Verify dependencies", len(prepared))
            summary["phase"] = "verification"
            for item, value in bounded_map(
                lambda context: analyzer.prepare(context, verification=True),
                [item.context for item in prepared],
                args.jobs,
                runner,
            ):
                runner.reporter.advance(failed=isinstance(value, Exception))
                if isinstance(value, Exception):
                    summary["coverage_gaps"].append(
                        {
                            "file": str(item.file),
                            "reason": f"Post-fix dependency scan failed: {value}",
                        }
                    )
                else:
                    verification.append(value)
            runner.reporter.stop_phase()
            verified_dependencies = {
                path_key(path) for item in verification for path in item.dependencies
            }
            summary["unreached_headers"] = [
                str(path)
                for key, path in headers.items()
                if key not in verified_dependencies
            ]
            summary["header_discovery_complete"] = (
                len(verification) == len(prepared) and not runner.cancelled.is_set()
            )
            seen.clear()
            summary["verification"] = collect(verification, verification=True)
            diagnostics = deduplicate(
                scoped(
                    [
                        record
                        for result in summary["verification"]
                        for record in result["diagnostics"]
                    ],
                    scope,
                )
            )
            summary["diagnostics"] = diagnostics
            status, code = outcome(
                summary["verification"],
                summary["coverage_gaps"],
                runner.cancelled.is_set(),
                args.fail_on,
                diagnostics,
            )
    elif args.fix or args.export_fixes:
        summary["fixes"] = {
            "applied_files": [],
            "reason": "Analysis incomplete or cancelled; replacement batch not applied",
        }
    summary["status"] = status
    return code


def engine_root() -> Path:
    """Prefer the editable source checkout, then a wheel user's working tree."""
    for start in (Path(__file__).resolve().parent, Path.cwd()):
        for candidate in (start, *start.parents):
            if (candidate / ".oxytidy.json").is_file():
                return candidate
    raise ToolError(
        "Cannot find Oxygen Engine (.oxytidy.json). Install this checkout in editable mode or run from an engine checkout."
    )


def main(argv: list[str], *, root: Path | None = None) -> int:
    args = parse_args(argv)
    root_selection = (
        "explicit --project-root"
        if args.project_root
        else "caller-provided root"
        if root
        else "automatic checkout discovery"
    )
    root = (
        Path(args.project_root).resolve()
        if args.project_root
        else (root or engine_root()).resolve()
    )
    if not root.is_dir():
        raise ToolError(
            f"Project root is not an existing directory: {root}. Select the engine checkout with --project-root PATH."
        )
    started = time.monotonic()
    parent = Path(args.log_dir).resolve() if args.log_dir else root / "out/clang-tidy"
    parent.mkdir(parents=True, exist_ok=True)
    run_dir = Path(
        tempfile.mkdtemp(
            prefix=datetime.now(tz=timezone.utc).strftime("run-%Y%m%d-%H%M%S-"),
            dir=parent,
        )
    )
    runner = Runner(args.timeout)
    previous_signals = {}
    for name in ("SIGINT", "SIGBREAK"):
        if hasattr(signal, name):
            sig = getattr(signal, name)
            previous_signals[sig] = signal.signal(
                sig, lambda *_: runner.cancelled.set()
            )
    summary = {
        "schema": 2,
        "status": "setup_failed",
        "phase": "setup",
        "analysis_started": False,
        "project_root": str(root),
        "project_root_selection": root_selection,
        "python_interpreter": sys.executable,
        "python_prefix": sys.prefix,
        "tool_package": str(Path(__file__).resolve().parent),
        "working_directory": str(Path.cwd()),
        "cache_dir": str(
            Path(args.cache_dir).resolve()
            if args.cache_dir
            else root / "out/clang-tidy/cache"
        ),
        "run_dir": str(run_dir),
        "options": vars(args),
    }
    runner.reporter.start(summary)
    try:
        code = run(args, root, runner, run_dir, summary)
    except (ToolError, OSError, ValueError) as error:
        summary["error"] = str(error)
        summary["status"] = (
            "cancelled"
            if runner.cancelled.is_set()
            else "setup_failed"
            if summary["phase"] == "setup"
            else "incomplete"
        )
        code = 130 if runner.cancelled.is_set() else 2
    finally:
        for sig, previous in previous_signals.items():
            signal.signal(sig, previous)
        summary["elapsed_seconds"] = time.monotonic() - started
        results = summary.get("results", [])
        summary["counts"] = {
            "selected": summary.get("selected_count", 0),
            "completed": sum(r["status"] == "completed" for r in results),
            "failed": sum(r["status"] != "completed" for r in results),
            "reused": sum(r["reused"] for r in results),
            "executed": sum(not r["reused"] for r in results),
            "unique_findings": len(summary.get("diagnostics", []))
            if summary["analysis_started"]
            else None,
            "levels": dict(
                Counter(record["level"] for record in summary.get("diagnostics", []))
            )
            if summary["analysis_started"]
            else None,
        }
        write_json(run_dir / "summary.json", summary)
        runner.reporter.stop_phase()
        gaps = summary.get("coverage_gaps", [])
        if gaps:
            runner.reporter.section("Analysis gaps", [])
            for gap in gaps[:10]:
                runner.reporter.rows(
                    [
                        ("File", Path(gap["file"]) if gap.get("file") else "scope"),
                        ("Reason", gap["reason"]),
                    ]
                )
            if len(gaps) > 10:
                runner.reporter.message(
                    f"{len(gaps) - 10} further gaps are recorded in the report.", "dim"
                )
        runner.reporter.finish(summary)
    return code
