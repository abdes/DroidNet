import difflib
import logging
import time
from typing import List, Dict, Tuple, Optional
from dataclasses import dataclass

logger = logging.getLogger("codemod.patcher")

@dataclass
class Edit:
    file_path: str
    original_text: str
    replacement_text: str
    # Line indices (0-indexed)
    start_line: int
    end_line: int
    # Column indices (0-indexed inside the line)
    start_col: Optional[int] = None
    end_col: Optional[int] = None

class PatchGenerator:
    """Generates git-compatible unified diff patches from a collection of edits."""

    def write_patch(self, patch_file: str, edits: List[Edit]):
        """
        Groups edits by file and writes a single patch file.
        Uses difflib to generate unified diff hunks.
        """
        if not edits:
            logger.warning("No edits to write to patch: %s", patch_file)
            return

        # Group edits by file
        file_edits: Dict[str, List[Edit]] = {}
        for edit in edits:
            file_edits.setdefault(edit.file_path, []).append(edit)

        patch_content = []

        for file_path, edits_for_file in file_edits.items():
            try:
                with open(file_path, 'r', encoding='utf-8') as f:
                    lines = f.readlines()
            except Exception as e:
                logger.error("Failed to read file %s: %s", file_path, e)
                continue

            # Apply edits to the lines buffer
            new_lines = list(lines)

            # Group edits by line for easier processing if column info is present
            # For now, sort all edits by position descending
            def edit_key(e):
                return (e.start_line, e.start_col or 0)

            sorted_edits = sorted(edits_for_file, key=edit_key, reverse=True)

            for edit in sorted_edits:
                target_line_idx = edit.start_line
                if target_line_idx >= len(new_lines):
                    continue

                if edit.start_col is not None and edit.end_col is not None:
                    # Partial line replacement
                    line = new_lines[target_line_idx]
                    prefix = line[:edit.start_col]
                    suffix = line[edit.end_col:]
                    new_lines[target_line_idx] = prefix + edit.replacement_text + suffix
                else:
                    # Full line range replacement
                    replacement_lines = edit.replacement_text.splitlines(keepends=True)
                    if replacement_lines and not replacement_lines[-1].endswith('\n'):
                         if (len(lines) > edit.end_line or lines[edit.end_line-1].endswith('\n')):
                            replacement_lines[-1] += '\n'
                    new_lines[edit.start_line:edit.end_line] = replacement_lines

            # Generate diff
            diff = difflib.unified_diff(
                lines,
                new_lines,
                fromfile=f"a/{file_path}",
                tofile=f"b/{file_path}",
                lineterm='\n'
            )

            patch_content.extend(list(diff))

        if not patch_content:
            logger.info("No changes detected even after processing %d edits", len(edits))
            return

        try:
            with open(patch_file, 'w', encoding='utf-8') as f:
                f.writelines(patch_content)
            logger.info("Successfully wrote patch to %s", patch_file)
        except Exception as e:
            logger.error("Failed to write patch file %s: %s", patch_file, e)
            raise
