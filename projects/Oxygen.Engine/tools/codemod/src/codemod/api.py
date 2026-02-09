from abc import ABC, abstractmethod
from argparse import ArgumentParser
from dataclasses import dataclass
from typing import List, Optional
import argparse

from .project import ProjectInfo
from .patcher import Edit


@dataclass
class RefactoringContext:
    """Context passed to a refactoring command."""

    project_info: ProjectInfo
    args: argparse.Namespace
    # Common options that might be accessed by drivers/refactorings
    dry_run: bool = False
    output_safe_patch: Optional[str] = None
    output_review_patch: Optional[str] = None
    includes: List[str] = None
    excludes: List[str] = None
    # Optional progress callback: signature (file_path, running: bool, safe: int|None, unsafe: int|None)
    progress_cb: Optional[callable] = None


class Refactoring(ABC):
    """Base class for all refactoring commands."""

    @property
    @abstractmethod
    def name(self) -> str:
        """The command name for the CLI (e.g., 'rename')."""
        pass

    @property
    @abstractmethod
    def description(self) -> str:
        """Description shown in CLI help."""
        pass

    @abstractmethod
    def register_arguments(self, parser: ArgumentParser) -> None:
        """Register command-specific arguments."""
        pass

    @abstractmethod
    def run(self, context: RefactoringContext) -> None:
        """Execute the refactoring."""
        pass
