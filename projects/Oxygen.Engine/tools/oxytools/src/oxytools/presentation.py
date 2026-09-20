"""Small presentation primitives shared by the two command-line tools."""

from rich.console import Console
from rich.text import Text

LABEL_WIDTH = 12


def field(console: Console, label: str, value: str, style: str = "cyan") -> None:
    line = Text(f"{label:<{LABEL_WIDTH}}  ", style="dim")
    line.append(value, style=style)
    console.print(line, soft_wrap=not console.is_terminal)


def heading(
    console: Console, tool: str, mode: str, scope: str, context: str = ""
) -> None:
    title = Text(tool, style="bold cyan")
    title.append(f"  {mode}", style="dim")
    if context:
        title.append(f"  |  {context}", style="dim")
    console.print(title, soft_wrap=not console.is_terminal)
    field(console, "Scope", scope)


def outcome(console: Console, label: str, details: list[str], style: str) -> None:
    line = Text(label, style=f"bold {style}")
    if details:
        line.append("  " + " | ".join(details), style="dim")
    console.print(line, soft_wrap=not console.is_terminal)


def failure(console: Console, label: str, message: str, detail: str = "") -> None:
    console.print(Text(label, style="bold red"))
    for line in message.splitlines():
        console.print(Text("  " + line), soft_wrap=not console.is_terminal)
    if detail:
        console.print(Text("  " + detail, style="dim"))


def quantity(count: int, noun: str) -> str:
    return f"{count} {noun}{'' if count == 1 else 's'}"
