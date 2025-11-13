from __future__ import annotations

import argparse
import logging
import re
import sys
from pathlib import Path
from typing import List, Optional

from rich.console import Console
from rich.logging import RichHandler

from .traversal.arguments import parse_forwarded_arguments
from .traversal.runner import TraversalConfig, TraversalRunner
from .traversal.task_registry import get_global_registry


def build_parser() -> argparse.ArgumentParser:
    description = (
        "Discover *.csproj files and run registered tasks on each project.\n\n"
        "Examples:\n"
        "  python -m tooling.scripts.traverse Select-Path\n"
        "  python -m tooling.scripts.traverse Invoke-Tests --start ./projects --exclude-tests -- --framework net9.0\n"
        "  python -m tooling.scripts.traverse New-Package --PackageCertificateKeyFile F:/certs/test.pfx"
    )
    parser = argparse.ArgumentParser(
        prog="traverse",
        description=description,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("tasks", nargs="*", help="Task names to execute (case-insensitive)")
    parser.add_argument("--start", default=".", help="Traversal start directory (default: current working directory)")
    parser.add_argument("--exclude-tests", action="store_true", help="Skip directories named 'tests'")
    parser.add_argument("--exclude-samples", action="store_true", help="Skip directories named 'samples'")
    parser.add_argument("--exclude", dest="exclude", help="Regex to filter out project names")
    parser.add_argument("--dry-run", action="store_true", help="Preview actions without executing commands")
    parser.add_argument("--color", dest="color", action="store_true", default=None, help="Force rich-colored output")
    parser.add_argument("--no-color", dest="color", action="store_false", help="Disable colored output")
    parser.add_argument("--list-tasks", action="store_true", help="List available tasks and exit")
    parser.add_argument("--debug", action="store_true", help="Enable verbose debug logging")
    return parser


def configure_console(color_flag: Optional[bool]) -> Console:
    if color_flag is True:
        return Console()
    if color_flag is False:
        return Console(no_color=True)
    return Console(no_color=not sys.stdout.isatty())


def configure_logging(console: Console, debug: bool) -> logging.Logger:
    level = logging.DEBUG if debug else logging.INFO
    handler = RichHandler(console=console, show_time=False, show_path=False)
    logging.basicConfig(level=level, handlers=[handler], force=True)
    return logging.getLogger("traverse")


def resolve_start_path(value: str) -> Path:
    p = Path(value)
    if not p.is_absolute():
        return (Path.cwd() / p).resolve()
    return p.resolve()


def list_available_tasks(console: Console) -> None:
    registry = get_global_registry()
    registry.load_builtin()
    registry.load_local_packages(Path(__file__).resolve().parent / "traversal")
    registry.load_plugins()
    rows = []
    for task in sorted(registry.tasks(), key=lambda item: item.name.lower()):
        rows.append((task.name, task.description or ""))
    if not rows:
        console.print("No tasks registered.")
        return
    width = max(len(name) for name, _ in rows) + 2
    for name, desc in rows:
        console.print(f"[bold]{name.ljust(width)}[/] {desc}")


def main(argv: Optional[List[str]] = None) -> int:
    parser = build_parser()
    args, forwarded = parser.parse_known_args(argv)

    console = configure_console(args.color)

    if args.list_tasks:
        list_available_tasks(console)
        return 0

    if not args.tasks:
        parser.error("At least one task name must be provided")

    logger = configure_logging(console, args.debug)

    registry = get_global_registry()
    registry.load_builtin()
    registry.load_local_packages(Path(__file__).resolve().parent / "traversal")
    registry.load_plugins()

    resolved_tasks = []
    for task_name in args.tasks:
        try:
            resolved_tasks.append(registry.resolve(task_name))
        except KeyError as exc:
            parser.error(str(exc))

    extra_arguments: List[str] = []
    forwarded_tokens = list(forwarded)
    if "--" in forwarded_tokens:
        dash_index = forwarded_tokens.index("--")
        extra_arguments = forwarded_tokens[dash_index + 1 :]
        forwarded_tokens = forwarded_tokens[:dash_index]

    arguments, extras = parse_forwarded_arguments(forwarded_tokens)
    try:
        exclude_pattern = re.compile(args.exclude) if args.exclude else None
    except re.error as exc:
        parser.error(f"Invalid --exclude pattern: {exc}")

    runner = TraversalRunner(registry)
    config = TraversalConfig(
        start_location=resolve_start_path(args.start),
        tasks=resolved_tasks,
        exclude_tests=args.exclude_tests,
        exclude_samples=args.exclude_samples,
        exclude_pattern=exclude_pattern,
        dry_run=args.dry_run,
        forwarded_arguments=arguments,
        forwarded_tokens=forwarded_tokens,
        extra_arguments=extra_arguments,
        console=console,
        logger=logger,
    )

    if extras:
        logger.warning("Unrecognized tokens forwarded to tasks: %s", " ".join(extras))

    try:
        runner.run(config)
    except Exception as exc:  # pragma: no cover - surfaced to CLI
        if args.debug:
            raise
        console.print(f"[red]Error:[/] {exc}")
        return 1

    return 0


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())
