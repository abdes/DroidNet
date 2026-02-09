"""Utility helpers for file discovery and gitignore handling."""

from typing import List
import logging

logger = logging.getLogger("codemod.utils")


def find_files(includes: List[str], excludes: List[str]):
    """Return list of candidate file paths based on include/exclude globs.

    This is a placeholder; real implementation would expand globs and respect .gitignore.
    """
    logger.debug("find_files includes=%s excludes=%s", includes, excludes)
    return []


def is_gitignored(path: str) -> bool:
    """Return True if the path should be ignored according to gitignore.

    Placeholder: real implementation should parse .gitignore and use git check-ignore.
    """
    return False
