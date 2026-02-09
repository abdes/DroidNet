"""Command-line interface for the codemod tool."""

import argparse
import logging
import sys
import os
from typing import Optional

try:
    from rich.console import Console
    from rich.logging import RichHandler
except Exception:
    Console = None
    RichHandler = None

from .project import ProjectResolver
from .api import RefactoringContext
from .refactorings import RenameRefactoring

logger = logging.getLogger("codemod")


def build_parser():
    # Parent parser for arguments common to all refactoring jobs
    job_parser = argparse.ArgumentParser(add_help=False)
    job_parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show planned edits but do not write patches",
    )
    job_parser.add_argument(
        "--output-safe-patch", help="Path to write safe patch file"
    )
    job_parser.add_argument(
        "--output-review-patch", help="Path to write review patch file"
    )
    job_parser.add_argument(
        "--include",
        action="append",
        default=[],
        help="Include glob (repeatable)",
    )
    job_parser.add_argument(
        "--exclude",
        action="append",
        default=[],
        help="Exclude glob (repeatable)",
    )

    # Global parser for options that should be accepted before or after the subcommand
    global_parser = argparse.ArgumentParser(add_help=False)
    global_parser.add_argument(
        "--root", help="Root directory for discovery and processing"
    )
    global_parser.add_argument(
        "--no-color", action="store_true", help="Disable colored output"
    )
    global_parser.add_argument(
        "-v",
        "--verbose",
        type=int,
        choices=[0, 1],
        default=None,
        help="Logging level: 0=INFO,1=DEBUG (omit for WARNING+ERROR only)",
    )

    # Main parser
    parser = argparse.ArgumentParser(
        prog="codemod",
        description="Oxygen Engine Refactoring Tool",
        parents=[global_parser],
    )

    subparsers = parser.add_subparsers(
        dest="command", required=True, help="Refactoring command to run"
    )

    # Register refactorings here
    refactorings = [
        RenameRefactoring(),
    ]

    for r in refactorings:
        # Inherit common job args
        sub = subparsers.add_parser(
            r.name, help=r.description, parents=[job_parser, global_parser]
        )
        r.register_arguments(sub)
        sub.set_defaults(refactoring_impl=r)

    return parser


def main(argv=None):
    argv = argv if argv is not None else sys.argv[1:]
    parser = build_parser()
    args = parser.parse_args(argv)

    # Configure logging using rich if available and color not disabled
    if getattr(args, "verbose", None) is None:
        log_level = logging.WARNING
    else:
        log_level = (
            logging.DEBUG if getattr(args, "verbose", 0) >= 1 else logging.INFO
        )
    if Console and RichHandler and not getattr(args, "no_color", False):
        console = Console()
        handler = RichHandler(
            console=console, show_time=False, show_level=True, markup=True
        )
        logging.basicConfig(level=log_level, handlers=[handler])
    else:
        logging.basicConfig(level=log_level)
        console = None

    # Resolve project info
    start_path = args.root if args.root else os.getcwd()
    project_info = ProjectResolver.resolve(start_path)

    logger.info("Project root: %s", project_info.root_dir)
    if project_info.compilation_database_dir:
        logger.info("Compilation DB: %s", project_info.compilation_database_dir)

    # Create context
    ctx = RefactoringContext(
        project_info=project_info,
        args=args,
        dry_run=args.dry_run,
        output_safe_patch=args.output_safe_patch,
        output_review_patch=args.output_review_patch,
        includes=args.include,
        excludes=args.exclude,
    )

    # Setup progress callback using rich console if available
    if console:
        from rich.console import Console as _Console
        from rich.status import Status

        class ProgressDisplay:
            def __init__(self, console: _Console):
                self.console = console
                self._status: Optional[Status] = None

            def start(self, file_path: str):
                # Start or update status spinner
                if self._status:
                    self._status.stop()
                self._status = self.console.status(
                    f"{file_path}", spinner="dots"
                )
                self._status.start()

            def stop(self):
                if self._status:
                    try:
                        self._status.stop()
                    finally:
                        self._status = None

            def final_line(self, safe: int, unsafe: int, file_path: str):
                # Print final summary line; ensures it appears below logs/status
                self.console.print(f"({safe},{unsafe}) {file_path}")

        pd = ProgressDisplay(console)

        def progress_cb(
            file_path: str,
            running: bool,
            safe: Optional[int] = None,
            unsafe: Optional[int] = None,
        ):
            if running:
                pd.start(file_path)
            else:
                pd.stop()
                pd.final_line(safe or 0, unsafe or 0, file_path)

        ctx.progress_cb = progress_cb
    else:
        # If no-color explicitly requested, suppress live progress and only print final summary lines.
        if getattr(args, "no_color", False):

            def progress_cb(
                file_path: str,
                running: bool,
                safe: Optional[int] = None,
                unsafe: Optional[int] = None,
            ):
                if not running:
                    # Print only the final summary for the file; keep it on its own line.
                    sys.stdout.write(
                        f"({safe or 0},{unsafe or 0}) {file_path}\r\n"
                    )
                    sys.stdout.flush()

            ctx.progress_cb = progress_cb
        else:
            # Fallback plain text progress callback that prints spinner-like prefix updates
            import itertools, threading, time

            class SimpleSpinner:
                def __init__(self):
                    self._running = False
                    self._thread = None
                    self._chars = itertools.cycle(["|", "/", "-", "\\"])

                def start(self, text: str):
                    self._running = True

                    def _spin():
                        while self._running:
                            ch = next(self._chars)
                            sys.stdout.write(f"\r{ch} {text}")
                            sys.stdout.flush()
                            time.sleep(0.1)

                    self._thread = threading.Thread(target=_spin, daemon=True)
                    self._thread.start()

                def stop(self):
                    self._running = False
                    if self._thread:
                        self._thread.join(timeout=0.2)
                        self._thread = None
                    sys.stdout.write("\r")

            spinner = SimpleSpinner()

            def progress_cb(
                file_path: str,
                running: bool,
                safe: Optional[int] = None,
                unsafe: Optional[int] = None,
            ):
                if running:
                    spinner.start(file_path)
                else:
                    spinner.stop()
                    # Ensure newline and final summary
                    sys.stdout.write(
                        f"({safe or 0},{unsafe or 0}) {file_path}\n"
                    )
                    sys.stdout.flush()

            ctx.progress_cb = progress_cb

    try:
        # Run specific refactoring
        if hasattr(args, "refactoring_impl"):
            args.refactoring_impl.run(ctx)
        else:
            parser.print_help()
            return 1

    except Exception:
        logger.exception("Fatal error running codemod")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
