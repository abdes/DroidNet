import logging
import re
import os
from typing import List, Tuple

from ..patcher import Edit

logger = logging.getLogger("codemod.drivers.text")


class TextDriver:
    """Fallback regex driver for markdown, docs, etc."""

    SUPPORTED_EXTENSIONS = {".md", ".txt", ".doc"}

    def __init__(self, ctx):
        self.ctx = ctx

    def generate_edits(
        self, files: List[str], progress_callback=None
    ) -> Tuple[List[Edit], List[Edit]]:
        # Only run in aggressive mode
        if getattr(self.ctx.args, "mode", "safe") != "aggressive":
            return [], []

        safe_edits = []
        review_edits = []

        text_files = [
            f
            for f in files
            if os.path.splitext(f)[1].lower() in self.SUPPORTED_EXTENSIONS
        ]
        pattern = re.compile(rf"\b{re.escape(self.ctx.args.from_sym)}\b")

        for file_path in text_files:
            if progress_callback:
                try:
                    progress_callback(file_path, True, None, None)
                except Exception:
                    pass
            logger.info("Processing Text file: %s", file_path)
            try:
                with open(file_path, "r", encoding="utf-8") as f:
                    lines = f.readlines()

                for i, line in enumerate(lines):
                    if pattern.search(line):
                        edit = Edit(
                            file_path=file_path,
                            original_text=self.ctx.args.from_sym,
                            replacement_text=self.ctx.args.to_sym,
                            start_line=i,
                            end_line=i + 1,
                        )
                        review_edits.append(edit)
            except Exception as e:
                logger.error("Error processing Text %s: %s", file_path, e)
            finally:
                if progress_callback:
                    try:
                        file_safe = sum(
                            1 for e in safe_edits if e.file_path == file_path
                        )
                        file_unsafe = sum(
                            1 for e in review_edits if e.file_path == file_path
                        )
                        progress_callback(
                            file_path, False, file_safe, file_unsafe
                        )
                    except Exception:
                        pass

        return safe_edits, review_edits
