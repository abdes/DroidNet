from __future__ import annotations

import shlex
import subprocess
from pathlib import Path
from typing import Iterable, List

from ..task_registry import TraversalContext, task
from ..tfm import discover_target_frameworks


def _strip_project_options(tokens: List[str]) -> List[str]:
    """Remove any user-provided project overrides to avoid conflicts."""
    cleaned: List[str] = []
    skip_next = False
    for token in tokens:
        if skip_next:
            skip_next = False
            continue
        lower = token.lower()
        if lower in ("--project", "-p"):
            skip_next = True
            continue
        if lower.startswith("--project=") or lower.startswith("--project:"):
            continue
        if lower.startswith("-p=") or lower.startswith("-p:"):
            continue
        cleaned.append(token)
    return cleaned


def _has_flag(tokens: Iterable[str], names: Iterable[str]) -> bool:
    lowered = [name.lower() for name in names]
    for token in tokens:
        tl = token.lower()
        for name in lowered:
            if tl == name:
                return True
            if name.startswith("--") and (tl.startswith(name + "=") or tl.startswith(name + ":")):
                return True
            if name == "-f" and (tl == "-f" or tl.startswith("-f")):
                return True
            if name == "-c" and (tl == "-c" or tl.startswith("-c")):
                return True
    return False


@task(
    "Invoke-Tests",
    aliases=["invoke-tests", "tests"],
    description="Run dotnet tests for *.Tests projects.",
)
def invoke_tests(project: Path, context: TraversalContext) -> None:
    name = project.stem
    if not name.endswith(".Tests"):
        context.logger.debug("Skipping non-test project %s", project)
        return

    tokens = _strip_project_options(list(context.forwarded_tokens))

    cmd: List[str] = [
        "dotnet",
        "run",
        "--project",
        str(project),
    ]

    if not _has_flag(tokens, ["--framework", "-f"]):
        try:
            frameworks = discover_target_frameworks(project)
        except RuntimeError as exc:
            context.logger.warning("%s", exc)
            frameworks = []
        if frameworks:
            cmd.extend(["--framework", frameworks[0]])

    if not _has_flag(tokens, ["--verbosity", "-v"]):
        cmd.extend(["--verbosity", "minimal"])

    cmd.extend(tokens)

    app_args = list(context.extra_arguments)

    exec_args = cmd.copy()
    if app_args:
        exec_args.append("--")
        exec_args.extend(app_args)

    preview = " ".join(shlex.quote(arg) for arg in exec_args)
    if context.console:
        context.console.print(f"[bold cyan]Invoke-Tests[/]: {preview}")
    else:
        print(f"Invoke-Tests: {preview}")

    if context.dry_run:
        return

    # Run the process with a timeout so we don't hang forever; if the
    # process takes more than 5 minutes (300 seconds) we'll stop it and
    # return to the traversal loop.
    try:
        result = subprocess.run(exec_args, check=False, timeout=300)
    except subprocess.TimeoutExpired:
        msg = f"dotnet run timed out after 300 seconds for {project}"
        context.logger.warning("%s", msg)
        if context.console:
            context.console.print(f"[bold yellow]Invoke-Tests[/]: {msg}")
        else:
            print(msg)
        # Do not raise; return to allow the traversal to continue.
        return

    if result.returncode != 0:
        msg = f"dotnet run failed with exit code {result.returncode} for {project}"
        context.logger.warning("%s", msg)
        if context.console:
            context.console.print(f"[bold red]Invoke-Tests[/]: {msg}")
        else:
            print(msg)
        # Do not raise; return to allow the traversal to continue.
        return
