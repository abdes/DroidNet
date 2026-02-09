import logging
import json
import os
from typing import List, Tuple

from ..patcher import Edit

logger = logging.getLogger("codemod.drivers.json")


class JsonDriver:
    """Driver for JSON/YAML data files."""

    SUPPORTED_EXTENSIONS = {".json", ".scene"}

    def __init__(self, ctx):
        self.ctx = ctx

    def generate_edits(
        self, files: List[str], progress_callback=None
    ) -> Tuple[List[Edit], List[Edit]]:
        safe_edits = []
        review_edits = []

        json_files = [
            f
            for f in files
            if os.path.splitext(f)[1].lower() in self.SUPPORTED_EXTENSIONS
        ]

        for file_path in json_files:
            if progress_callback:
                try:
                    progress_callback(file_path, True, None, None)
                except Exception:
                    pass
            logger.info("Processing Data file (JSON): %s", file_path)
            try:
                with open(file_path, "r", encoding="utf-8") as f:
                    content = f.read()

                # Pattern to match "from_symbol" as a key or string value
                pattern = f'"{self.ctx.args.from_sym}"'

                if pattern in content:
                    lines = content.splitlines(keepends=True)
                    for i, line in enumerate(lines):
                        if pattern in line:
                            start_col = (
                                line.find(pattern) + 1
                            )  # Skip opening quote
                            edit = Edit(
                                file_path=file_path,
                                original_text=self.ctx.args.from_sym,
                                replacement_text=self.ctx.args.to_sym,
                                start_line=i,
                                end_line=i,
                                start_col=start_col,
                                end_col=start_col + len(self.ctx.args.from_sym),
                            )
                            safe_edits.append(edit)
            except Exception as e:
                logger.error("Error processing JSON %s: %s", file_path, e)
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
