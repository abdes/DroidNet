import logging
import re
import os
from typing import List, Tuple

from ..patcher import Edit

logger = logging.getLogger("codemod.drivers.hlsl")


class HlslDriver:
    """Regex-based driver for HLSL files."""

    SUPPORTED_EXTENSIONS = {".hlsl", ".hlsli"}

    def __init__(self, ctx):
        self.ctx = ctx

    def generate_edits(
        self, files: List[str], progress_callback=None
    ) -> Tuple[List[Edit], List[Edit]]:
        safe_edits = []
        review_edits = []

        hlsl_files = [
            f
            for f in files
            if os.path.splitext(f)[1].lower() in self.SUPPORTED_EXTENSIONS
        ]

        # Whole-word regex for the symbol
        # REQ17: Avoid accidental renames of partial tokens
        pattern = re.compile(rf"\b{re.escape(self.ctx.args.from_sym)}\b")

        for file_path in hlsl_files:
            if progress_callback:
                try:
                    progress_callback(file_path, True, None, None)
                except Exception:
                    pass
            logger.info("Processing HLSL file: %s", file_path)
            try:
                with open(file_path, "r", encoding="utf-8") as f:
                    lines = f.readlines()

                for i, line in enumerate(lines):
                    for match in pattern.finditer(line):
                        # REQ16: Avoid changing code inside string literals and comments (if possible)
                        # For now, simplistic approach: include in reviews if it looks like it's in a comment
                        is_likely_comment = (
                            "//" in line[: match.start()]
                            or "/*" in line[: match.start()]
                        )

                        edit = Edit(
                            file_path=file_path,
                            original_text=self.ctx.args.from_sym,
                            replacement_text=self.ctx.args.to_sym,
                            start_line=i,
                            end_line=i,
                            start_col=match.start(),
                            end_col=match.end(),
                        )

                        if is_likely_comment:
                            review_edits.append(edit)
                        else:
                            safe_edits.append(edit)
            except Exception as e:
                logger.error("Error processing HLSL %s: %s", file_path, e)
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
