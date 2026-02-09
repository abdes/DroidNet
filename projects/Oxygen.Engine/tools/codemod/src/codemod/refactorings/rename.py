import logging
import os
from argparse import ArgumentParser
from typing import List, Dict

from ..api import Refactoring, RefactoringContext
from ..project import ProjectInfo
from ..scanner import SymbolScanner
from ..patcher import PatchGenerator, Edit

# Import drivers
from ..drivers.cpp_driver import CppDriver
from ..drivers.hlsl_driver import HlslDriver
from ..drivers.json_driver import JsonDriver
from ..drivers.text_driver import TextDriver

logger = logging.getLogger("codemod.refactorings.rename")


class RenameRefactoring(Refactoring):
    @property
    def name(self) -> str:
        return "rename"

    @property
    def description(self) -> str:
        return "Rename a symbol across the codebase (C++, HLSL, JSON, etc.)"

    def register_arguments(self, parser: ArgumentParser) -> None:
        parser.add_argument(
            "--from",
            dest="from_sym",
            required=True,
            help="Source symbol to rename",
        )
        parser.add_argument(
            "--to", dest="to_sym", required=True, help="Target symbol name"
        )
        parser.add_argument(
            "--kind",
            choices=["member", "function", "class", "namespace", "variable"],
            default=None,
            help="Symbol kind (C++ semantic hint)",
        )
        parser.add_argument(
            "--mode",
            choices=["safe", "aggressive"],
            default="safe",
            help="Renaming mode (safe=semantic only, aggressive=regex fallback)",
        )

    def run(self, context: RefactoringContext) -> None:
        from_symbol = context.args.from_sym
        to_symbol = context.args.to_sym
        kind = context.args.kind

        logger.info("Starting codemod: %s -> %s", from_symbol, to_symbol)

        # 1) Find candidate files using ripgrep
        scanner = SymbolScanner(
            context.project_info.root_dir,
            includes=context.includes,
            excludes=context.excludes,
        )
        candidate_files = scanner.scan(from_symbol)

        if not candidate_files:
            logger.info("No candidate files found. Nothing to do.")
            return

        # 2) Dispatch files to drivers
        drivers = [
            CppDriver(context),
            HlslDriver(context),
            JsonDriver(context),
            TextDriver(context),
        ]

        all_edits: List[Edit] = []
        review_edits: List[Edit] = []

        # Group files by extension to optimize driver dispatch
        ext_map: Dict[str, List[str]] = {}
        for f in candidate_files:
            ext = os.path.splitext(f)[1].lower()
            ext_map.setdefault(ext, []).append(f)

        for d in drivers:
            # Check if this driver supports any of the extensions we found
            supported_exts = getattr(d, "SUPPORTED_EXTENSIONS", set())
            relevant_files = []
            if not supported_exts:
                # If no specific extensions defined, give it all files (e.g. TextDriver)
                relevant_files = candidate_files
            else:
                for ext, files in ext_map.items():
                    if ext in supported_exts:
                        relevant_files.extend(files)

            if not relevant_files:
                continue

            logger.debug(
                "Running driver %s on %d files",
                d.__class__.__name__,
                len(relevant_files),
            )
            try:
                # Pass the central progress callback so drivers can report per-file progress
                edits, ambiguous = d.generate_edits(
                    relevant_files, progress_callback=context.progress_cb
                )
                all_edits.extend(edits)
                review_edits.extend(ambiguous)
            except NotImplementedError:
                logger.debug(
                    "Driver %s not implemented; skipping", d.__class__.__name__
                )
            except Exception as e:
                logger.error("Error in driver %s: %s", d.__class__.__name__, e)

        # 3) Produce patches
        patcher = PatchGenerator()

        if all_edits:
            if context.dry_run:
                logger.info(
                    "[DRY-RUN] Would write safe patch with %d edits",
                    len(all_edits),
                )
                for edit in all_edits:
                    logger.info(
                        "  %s:%d -> %s",
                        edit.file_path,
                        edit.start_line + 1,
                        edit.replacement_text.strip(),
                    )
            elif context.output_safe_patch:
                patcher.write_patch(context.output_safe_patch, all_edits)

        if review_edits:
            if context.dry_run:
                logger.info(
                    "[DRY-RUN] Would write review patch with %d edits",
                    len(review_edits),
                )
            elif context.output_review_patch:
                patcher.write_patch(context.output_review_patch, review_edits)

        logger.info(
            "Codemod commands complete: edits=%d ambiguous=%d",
            len(all_edits),
            len(review_edits),
        )
