"""Terminal presentation, kept separate from analysis and machine-readable results."""

from __future__ import annotations

import re
from pathlib import Path

from rich import box
from rich.console import Console, Group, RenderableType
from rich.panel import Panel
from rich.progress import (
    BarColumn,
    MofNCompleteColumn,
    Progress,
    SpinnerColumn,
    TextColumn,
    TimeElapsedColumn,
)
from rich.table import Table
from rich.text import Text


class Reporter:
    def __init__(
        self, console: Console | None = None, output: Console | None = None
    ) -> None:
        self.console = console or Console(stderr=True, highlight=False, markup=False)
        self.output = output or console or Console(highlight=False, markup=False)
        self.aliases: dict[str, Path] = {}
        self.progress: Progress | None = None
        self.task = None
        self._pending: list[RenderableType] = []
        self._buffered = False

    def path(self, path: Path, *, exclude: str | None = None) -> str:
        path = path.resolve()
        for name, root in sorted(
            self.aliases.items(), key=lambda item: len(str(item[1])), reverse=True
        ):
            if name == exclude:
                continue
            if path.is_relative_to(root):
                relative = path.relative_to(root).as_posix()
                return name if relative == "." else f"{name}/{relative}"
        if path.is_relative_to(Path.home()):
            return "~/" + path.relative_to(Path.home()).as_posix()
        return path.as_posix()

    def compact(self, text: str) -> str:
        # Presentation only: compilation paths and report data remain untouched.
        for name, root in sorted(
            self.aliases.items(), key=lambda item: len(str(item[1])), reverse=True
        ):
            for prefix in {str(root), root.as_posix()}:
                text = text.replace(prefix + "\\", name + "/").replace(
                    prefix + "/", name + "/"
                )
                text = re.sub(
                    re.escape(prefix) + r"""(?=$|[\s"'),:])""",
                    lambda _, name=name: name,
                    text,
                )
        return text

    def message(self, text: str, style: str = "") -> None:
        self.console.print(Text(self.compact(text), style=style))

    def _emit(self, renderable: RenderableType) -> None:
        if self._buffered:
            self._pending.append(renderable)
        else:
            self.console.print(renderable)

    def flush_context(self) -> None:
        self._buffered = False
        if self._pending:
            self.console.print(Group(*self._pending))
            self._pending.clear()

    def section(self, title: str, rows: list[tuple[str, str | Path | Text]]) -> None:
        self._emit(Text(""))
        self._emit(Text(title, style="bold"))
        self.rows(rows)

    def rows(self, rows: list[tuple[str, str | Path | Text]]) -> None:
        table = Table.grid(padding=(0, 2), expand=False)
        table.add_column(style="dim", width=16, no_wrap=True)
        table.add_column(overflow="fold")
        for label, value in rows:
            if isinstance(value, Path):
                value = Text(self.path(value), style="cyan", overflow="fold")
            elif not isinstance(value, Text):
                value = Text(self.compact(str(value)), overflow="fold")
            table.add_row(label, value)
        self._emit(table)

    def start(self, summary: dict) -> None:
        options = summary["options"]
        project = Path(summary["project_root"])
        package = Path(summary["tool_package"])
        tool = next(
            (path for path in package.parents if (path / ".oxytools.json").is_file()),
            package,
        )
        self.aliases["@project"] = project
        if tool != project:
            self.aliases["@tool"] = tool
        self.aliases["@python"] = Path(summary["python_prefix"])
        self.aliases["@run"] = Path(summary["run_dir"])
        mode = (
            "list files"
            if options["list_files"]
            else "fix + verify"
            if options["fix"]
            else "export fixes"
            if options["export_fixes"]
            else "report only"
        )
        title = Text("oxytidy", style="bold cyan")
        title.append(f"  {mode}  |  {options['configuration']}", style="dim")
        self.console.print(title)
        self._buffered = True
        self.rows(
            [
                (
                    "Scope",
                    ", ".join(options["paths"])
                    if options["paths"]
                    else "all project sources",
                ),
                (
                    "Run",
                    f"{options['jobs']} workers | tests {'included' if options['include_tests'] else 'excluded'} | fail-on {options['fail_on']}",
                ),
                (
                    "Limits",
                    f"timeout {str(options['timeout']) + 's' if options['timeout'] else 'none'} | cap {options['max_files'] or 'none'} | format {'on' if options['format'] else 'off'}",
                ),
            ]
        )
        self.section(
            "Paths",
            [
                (name, Text(self.path(path, exclude=name), style="cyan"))
                for name, path in self.aliases.items()
            ],
        )
        self.rows(
            [
                ("Root choice", summary["project_root_selection"]),
                ("Python", Path(summary["python_interpreter"])),
                ("Tool", package),
                ("Working dir", Path(summary["working_directory"])),
                (
                    "Cache",
                    f"{'forced fresh' if options['force'] else 'reuse on' if options['incremental'] else 'off'} | {self.path(Path(summary['cache_dir']))}",
                ),
            ]
        )
        if options["export_fixes"]:
            self.rows([("Export", Path(options["export_fixes"]).resolve())])
        self.section(
            "Configuration",
            [
                ("Checks", options["checks"] or "project configuration"),
                ("Source mapping", "none"),
            ],
        )

    def ownership(self, policy: Path, reason: str) -> None:
        self.rows([("Ownership", policy), ("Policy choice", reason)])

    def tool(self, name: str, path: str, version: str) -> None:
        version_line = next(
            (
                line.strip()
                for line in version.splitlines()
                if "version" in line.lower()
            ),
            version.splitlines()[0],
        )
        self.rows([(name, Path(path)), ("Version", version_line)])

    def configuration(self, source: Path, files: list[Path], snapshot: Path) -> None:
        rows: list[tuple[str, str | Path | Text]] = [("Source", source)]
        rows.extend(
            ("Config" if i == 0 else "Parent config", path)
            for i, path in enumerate(files)
        )
        if not files:
            rows.append(("Config", "LLVM defaults"))
        rows.extend([("Inheritance", "resolved by LLVM"), ("Snapshot", snapshot)])
        self.section("Check configuration", rows)

    def start_phase(self, phase: str, total: int) -> None:
        self.flush_context()
        self.stop_phase()
        animated = self.console.is_terminal and not self.console.is_dumb_terminal
        self.progress = Progress(
            SpinnerColumn(),
            TextColumn("{task.description}"),
            BarColumn(),
            MofNCompleteColumn(),
            TimeElapsedColumn(),
            console=self.console,
            transient=True,
            refresh_per_second=5,
            disable=not animated,
            redirect_stdout=False,
            redirect_stderr=False,
        )
        self.task = self.progress.add_task(phase, total=total, failures=0)
        self.progress.start()
        if not animated:
            self.message(f"\n{phase}  {total} context(s)", "bold")

    def advance(self, *, failed: bool = False) -> None:
        if self.progress:
            task = self.progress.tasks[self.task]
            self.progress.update(
                self.task, advance=1, failures=task.fields["failures"] + int(failed)
            )

    def stop_phase(self) -> None:
        if self.progress:
            task = self.progress.tasks[self.task]
            self.progress.stop()
            self.progress = None
            self.message(
                f"{task.description}  {int(task.completed)}/{int(task.total)} contexts | {task.fields['failures']} failed | {task.elapsed or 0:.1f}s",
                "dim",
            )

    def diagnostics(self, records: list[dict]) -> None:
        # Use Rich's lifecycle to pause the live display while emitting a batch
        # on stdout. Neither stream is redirected into the other.
        live = self.progress if self.progress and not self.progress.disable else None
        if live:
            live.stop()
        try:
            for record in records:
                self._diagnostic(record)
        finally:
            if live:
                live.start()

    def _diagnostic(self, record: dict) -> None:
        color = "red" if record["level"] == "error" else "yellow"
        name = (
            self.path(Path(record["file"]))
            if self.output.is_terminal
            else record["file"]
        )
        location = f"{name}:{record['line']}:{record['column']}"
        title = Text(location, style="cyan")
        title.append(f"  {record['level']}", style=f"bold {color}")
        title.append(f"  {record['check']}", style="dim")
        self.output.print(title, soft_wrap=not self.output.is_terminal)
        self.output.print(
            Text("  " + record["message"]), soft_wrap=not self.output.is_terminal
        )
        for note in record["notes"]:
            name = (
                self.path(Path(note["file"]))
                if note["file"] and self.output.is_terminal
                else note["file"]
            )
            self.output.print(
                Text(
                    f"  note: {name}:{note['line']}:{note['column']} {note['message']}",
                    style="dim",
                ),
                soft_wrap=not self.output.is_terminal,
            )

    def listed_file(
        self, path: Path, identity: str, reasons: list[str], scheduled: bool
    ) -> None:
        name = self.path(path) if self.output.is_terminal else str(path)
        self.output.print(
            Text(
                f"{name} [{identity}] {'scheduled' if scheduled else 'omitted by cap'}"
            ),
            soft_wrap=not self.output.is_terminal,
        )
        self.output.print(Text(f"  selected by: {', '.join(reasons)}", style="dim"))

    def failure(self, title: str, error: str, subtitle: str = "") -> None:
        self.stop_phase()
        first, _, rest = self.compact(error).partition("\n")
        body = [Text(first, style="bold")]
        if rest:
            body.extend([Text(""), Text(rest)])
        if self.console.is_terminal:
            self.console.print(
                Panel(
                    Group(*body),
                    title=Text(title, style="bold red"),
                    subtitle=Text(subtitle, style="dim"),
                    border_style="red",
                    box=box.ROUNDED,
                    expand=False,
                )
            )
        else:
            self.console.print()
            self.console.print(Text(title, style="bold red"))
            for text in body:
                self.console.print(text)
            if subtitle:
                self.console.print(Text(subtitle))

    def finish(self, summary: dict) -> None:
        self.stop_phase()
        status = summary["status"]
        if summary.get("error"):
            self.failure(
                "Setup failed" if status == "setup_failed" else "Run failed",
                summary["error"],
                "Analysis did not start."
                if not summary["analysis_started"]
                else "Results are partial.",
            )
            if self._buffered:
                self.console.print(Text("\nRun context", style="bold"))
            self.flush_context()
        elif not summary["analysis_started"]:
            self.flush_context()
            if status == "listed":
                self.message(
                    "\nCoverage listing complete. Analysis was not requested.", "green"
                )
            elif status == "no_analysis":
                self.message(
                    "\nNo header consumers in discovery scope. Analysis did not run.",
                    "yellow",
                )
            else:
                self.message(
                    "\n"
                    + (
                        "Run cancelled."
                        if status == "cancelled"
                        else "Prerequisite/coverage checks failed."
                    )
                    + " Analysis did not start.",
                    "bold red",
                )
        else:
            self.flush_context()
            labels = {
                "clean": "Analyzed contexts clean"
                if summary.get("unreached_headers")
                else "Clean",
                "findings": "Findings",
                "incomplete": "Incomplete - results are partial",
                "cancelled": "Cancelled - results are partial",
            }
            self.console.print()
            self.console.print(
                Text(
                    labels[status],
                    style="bold green" if status == "clean" else "bold yellow",
                )
            )
            counts = summary["counts"]
            self.rows(
                [
                    (
                        "Findings",
                        f"{counts['unique_findings']} unique | {counts['levels'].get('warning', 0)} warnings | {counts['levels'].get('error', 0)} errors",
                    ),
                    (
                        "Contexts",
                        f"{counts['executed']} executed | {counts['reused']} reused | {summary['elapsed_seconds']:.1f}s",
                    ),
                ]
            )
        headers = summary.get("unreached_headers", [])
        if headers:
            reason = (
                "module coverage gap; no source/test consumer in discovery scope"
                if summary.get("header_discovery_complete")
                else "reachability is unresolved because discovery did not complete"
            )
            self.section(
                "Header coverage",
                [
                    ("Not reached", f"{len(headers)} project header(s)"),
                    ("Reason", reason),
                ],
            )
            self.rows([("Header", Path(path)) for path in headers[:10]])
            if len(headers) > 10:
                self.message(
                    f"{len(headers) - 10} further headers are listed in the report.",
                    "dim",
                )
        self.rows([("Report", Path(summary["run_dir"]) / "summary.json")])
