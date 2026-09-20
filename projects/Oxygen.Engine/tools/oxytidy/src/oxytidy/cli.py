"""The single entry point for console, module, and shell launchers."""

from __future__ import annotations

import sys

from rich.console import Console

from .common import ToolError
from .reporting import Reporter
from .workflow import main as run


def main(argv: list[str] | None = None) -> int:
    """Run the Python CLI without interpreting arguments in shell launchers."""
    try:
        return run(sys.argv[1:] if argv is None else argv)
    except (ToolError, OSError) as error:
        # Root discovery and run-directory creation precede the durable report.
        Reporter(Console(stderr=True, highlight=False, markup=False)).failure(
            "Setup failed", str(error), "Analysis did not start."
        )
        return 2
