"""Concise formatting results, with plain, complete paths when redirected."""

from pathlib import Path

from oxytools.presentation import failure, field, heading, outcome, quantity
from rich.console import Console
from rich.text import Text


class Reporter:
    def __init__(
        self, console: Console | None = None, output: Console | None = None
    ) -> None:
        self.console = console or Console(stderr=True, highlight=False, markup=False)
        self.output = output or console or Console(highlight=False, markup=False)
        self.root = Path.cwd()
        self.fix = False
        self._listed = False

    def start(
        self, root: Path, paths: list[Path], *, fix: bool, all_project: bool
    ) -> None:
        self.root, self.fix = root, fix
        scope = (
            "all project C++ files"
            if all_project
            else ", ".join(self.path(path) for path in paths)
        )
        heading(self.console, "oxyformat", "format" if fix else "check", scope)
        if Path.cwd().resolve() != root:
            field(self.console, "Project", root.as_posix())

    def path(self, path: Path) -> str:
        return (
            path.relative_to(self.root).as_posix()
            if path.is_relative_to(self.root)
            else path.as_posix()
        )

    def file(self, path: Path) -> None:
        label = "Formatted" if self.fix else "Needs formatting"
        if self.output.is_terminal:
            if not self._listed:
                self.output.print(
                    Text(
                        "\n" + label, style="bold green" if self.fix else "bold yellow"
                    )
                )
                self._listed = True
            self.output.print(Text("  " + self.path(path), style="cyan"))
        else:
            self.output.print(Text(f"{label}: {path}"), soft_wrap=True)

    def error(self, path: Path, message: str, *, cancelled: bool = False) -> None:
        label = "Cancelled" if cancelled else "Failed"
        name = self.path(path) if self.console.is_terminal else str(path)
        failure(self.console, f"{label}: {name}", message)

    def failure(self, message: str, *, started: bool) -> None:
        failure(self.console, "Run failed" if started else "Setup failed", message)

    def finish(
        self, counts: dict, selected: int, elapsed: float, *, cancelled: bool
    ) -> None:
        needed, changed, failed = (
            counts.get(key, 0) for key in ("needed", "changed", "failed")
        )
        checked = needed + changed + counts.get("unchanged", 0)
        details = []
        if cancelled:
            label, style = "Cancelled", "yellow"
            details.append(f"{checked} of {selected} checked")
        elif failed:
            label, style = "Incomplete", "red"
            details.extend([f"{failed} failed", f"{quantity(checked, 'file')} checked"])
        elif not selected:
            label, style = "Nothing to format", "dim"
        elif needed:
            label, style = "Formatting needed", "yellow"
            details.append(
                quantity(needed, "file")
                if needed == selected
                else f"{needed} of {selected} files"
            )
        elif changed:
            label, style = "Formatted", "green"
            details.extend([quantity(changed, "file"), f"{selected} checked"])
        else:
            label, style = "Clean", "green"
            details.append(f"{quantity(checked, 'file')} checked")
        if failed or cancelled:
            if changed:
                details.append(f"{changed} formatted")
            if needed:
                details.append(f"{needed} need formatting")
        if counts.get("skipped"):
            details.append(f"{counts['skipped']} skipped")
        details.append(f"{elapsed:.2f}s")
        self.console.print()
        outcome(self.console, label, details, style)
        if cancelled and changed:
            self.console.print(Text("Completed formatting is retained.", style="dim"))
