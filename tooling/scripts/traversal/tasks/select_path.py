from __future__ import annotations

from pathlib import Path

from ..task_registry import TraversalContext, task


@task("Select-Path", description="Print the absolute path to the project file.")
def select_path(project: Path, context: TraversalContext) -> None:
    path_text = str(project.resolve())
    if context.console:
        context.console.print(path_text)
    else:
        print(path_text)
