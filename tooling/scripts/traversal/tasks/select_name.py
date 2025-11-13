from __future__ import annotations

from pathlib import Path

from ..task_registry import TraversalContext, task


@task("Select-Name", description="Print the project name without extension.")
def select_name(project: Path, context: TraversalContext) -> None:
    name = project.stem
    if context.console:
        context.console.print(name)
    else:
        print(name)
