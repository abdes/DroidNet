"""One rename operation's source snapshots and explicit command options."""

from __future__ import annotations

import argparse
from collections.abc import Callable
from dataclasses import dataclass

from .patcher import Sources
from .project import ProjectInfo


@dataclass
class RefactoringContext:
    project_info: ProjectInfo
    args: argparse.Namespace
    sources: Sources
    trace: Callable[[str, bool], None] | None = None
