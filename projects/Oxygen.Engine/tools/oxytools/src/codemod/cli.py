"""One explicit rename command; previews and failures are always visible."""

from __future__ import annotations

import argparse
import os
from pathlib import Path

from oxytools.common import ToolError
from oxytools.presentation import heading, outcome, quantity
from rich.console import Console
from rich.text import Text

from .lexical import replacement_name


def _globals(parser, *, child=False):
    def default(value):
        return argparse.SUPPRESS if child else value

    parser.add_argument(
        "--root",
        default=default(None),
        help="Edit root; an explicit root is never widened",
    )
    parser.add_argument("--no-color", action="store_true", default=default(False))
    parser.add_argument(
        "-v",
        "--verbose",
        nargs="?",
        const=1,
        choices=(0, 1),
        type=int,
        default=default(None),
    )


def build_parser():
    parser = argparse.ArgumentParser(
        prog="codemod",
        description="Generate source-verified C++ rename patches and separate review proposals.",
    )
    _globals(parser)
    commands = parser.add_subparsers(dest="command", required=True)
    rename = commands.add_parser(
        "rename", help="Rename one symbol without editing source files"
    )
    _globals(rename, child=True)
    rename.add_argument("--from", dest="from_sym", required=True)
    rename.add_argument("--to", dest="to_sym", required=True)
    rename.add_argument(
        "--kind", choices=("member", "function", "class", "namespace", "variable")
    )
    rename.add_argument(
        "--at",
        metavar="FILE:LINE:COLUMN",
        help="Select a declaration when its name is ambiguous",
    )
    rename.add_argument(
        "--mode",
        choices=("safe", "aggressive"),
        default="safe",
        help="aggressive also proposes documentation/text edits",
    )
    rename.add_argument(
        "--update-strings",
        action="store_true",
        help="Also propose C++/HLSL comment and literal edits in the review patch",
    )
    rename.add_argument(
        "--include",
        action="append",
        default=[],
        help="Additional edit-scope glob (repeatable)",
    )
    rename.add_argument(
        "--exclude",
        action="append",
        default=[],
        help="Exclude files and compilation contexts (repeatable)",
    )
    rename.add_argument(
        "--configuration",
        default="Debug",
        help="Compilation configuration, or all for metadata-free databases",
    )
    rename.add_argument(
        "--build-dir",
        help="Compilation database directory, relative to the working directory",
    )
    rename.add_argument("--libclang-file", help="Explicit libclang shared library")
    rename.add_argument(
        "--dry-run",
        action="store_true",
        help="Print every proposed edit without writing patches",
    )
    rename.add_argument("--output-safe-patch", default="safe.patch")
    rename.add_argument("--output-review-patch", default="review.patch")
    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    if (
        not args.from_sym
        or not args.to_sym
        or replacement_name(args.from_sym, args.to_sym) == args.from_sym
    ):
        parser.error("--from and --to must be nonempty and different")
    plain = args.no_color or "NO_COLOR" in os.environ
    console = Console(
        stderr=True,
        highlight=False,
        markup=False,
        color_system=None if plain else "auto",
    )
    output = Console(
        highlight=False, markup=False, color_system=None if plain else "auto"
    )
    try:
        from .api import RefactoringContext
        from .patcher import Sources
        from .project import ProjectResolver
        from .refactorings.rename import RenameRefactoring

        if not args.dry_run:
            paths = [
                Path(args.output_safe_patch).resolve(),
                Path(args.output_review_patch).resolve(),
            ]
            if paths[0] == paths[1]:
                raise ToolError(
                    "Safe and review patches must have different output paths"
                )
            if any(path.exists() or path.is_symlink() for path in paths):
                raise ToolError("Patch outputs already exist; choose new output paths")
        project = ProjectResolver.resolve(
            args.root or Path.cwd(), explicit=args.root is not None
        )
        heading(
            console,
            "codemod",
            "preview" if args.dry_run else "rename",
            str(project.root_dir),
            f"{args.from_sym} -> {args.to_sym}",
        )

        def trace(message, debug=False):
            if args.verbose is not None and (not debug or args.verbose == 1):
                console.print(
                    Text(message, style="dim"), soft_wrap=not console.is_terminal
                )

        context = RefactoringContext(project, args, Sources(), trace)
        safe, review = RenameRefactoring().run(context)
        for label, edits in (("safe", safe), ("review", review)):
            if args.dry_run:
                for edit in edits:
                    line, column = context.sources.location(edit)
                    output.print(
                        Text(
                            f"{label}: {edit.file_path}:{line}:{column}: {edit.original_text!r} -> {edit.replacement_text!r}"
                        ),
                        soft_wrap=not output.is_terminal,
                    )
                    if edit.reason:
                        output.print(
                            Text("  " + edit.reason, style="dim"),
                            soft_wrap=not output.is_terminal,
                        )
            elif edits:
                path = (
                    args.output_safe_patch
                    if label == "safe"
                    else args.output_review_patch
                )
                output.print(
                    Text(f"{label.capitalize()} patch: {Path(path).resolve()}"),
                    soft_wrap=not output.is_terminal,
                )
        outcome(
            console,
            "Preview"
            if args.dry_run
            else "Patches ready"
            if safe or review
            else "No changes",
            [
                quantity(len(edits), label)
                for edits, label in (
                    (safe, "verified C++ edit"),
                    (review, "review proposal"),
                )
                if edits
            ],
            "yellow" if review else "green",
        )
        if safe and review and not args.dry_run:
            console.print(
                Text(
                    "Review proposals are based on the safe patch; apply the safe patch first.",
                    style="dim",
                )
            )
        return 0
    except ModuleNotFoundError as error:
        console.print(
            Text(
                f"Missing optional dependency {error.name!r}. Install tools/oxytools[codemod] in this interpreter.",
                style="bold red",
            )
        )
        return 2
    except (OSError, ValueError, ToolError) as error:
        console.print(
            Text(f"Rename failed: {error}", style="bold red"),
            soft_wrap=not console.is_terminal,
        )
        return 2
    except KeyboardInterrupt:
        console.print(
            Text("Cancelled. Source files were not modified.", style="yellow")
        )
        return 130
