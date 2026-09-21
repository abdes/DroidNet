"""Required LLVM release family for formatting and static analysis."""

from __future__ import annotations

import re

from .common import ToolError

REQUIRED_LLVM_MAJOR = 23


def require_version(tool: str, version: str) -> None:
    match = re.search(r"\bversion\s+(\d+)(?=[.\s]|$)", version)
    if not match or int(match[1]) != REQUIRED_LLVM_MAJOR:
        found = version.strip() or "empty version output"
        raise ToolError(f"{tool} {REQUIRED_LLVM_MAJOR}.x is required; found {found}")
