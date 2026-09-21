"""Find token spans without treating comments or quoted text as code."""

from __future__ import annotations

import re
from collections.abc import Iterator

from oxytools.common import ToolError
from oxytools.lexical import REGIONS


def spans(
    text: str, symbol: str, *, regions: bool = False
) -> Iterator[tuple[int, int]]:
    pattern = re.compile(r"(?<!\w)" + re.escape(symbol) + r"(?!\w)")
    cursor = 0
    for region in REGIONS.finditer(text):
        if regions:
            yield from (
                (match.start(), match.end())
                for match in pattern.finditer(text, region.start(), region.end())
            )
        else:
            yield from (
                (match.start(), match.end())
                for match in pattern.finditer(text, cursor, region.start())
            )
        cursor = region.end()
    if not regions:
        # Unclosed comments/quotes cannot be interpreted safely by this lexer.
        tail = text[cursor:]
        if "/*" in tail or '"' in tail or "'" in tail:
            raise ToolError("Unterminated comment or string literal")
        yield from (
            (match.start(), match.end()) for match in pattern.finditer(text, cursor)
        )


def byte_span(text: str, start: int, end: int) -> tuple[int, int]:
    return len(text[:start].encode("utf-8")), len(text[:end].encode("utf-8"))


def replacement_name(old: str, new: str) -> str:
    if "::" in old and new.isidentifier():
        return old.rsplit("::", 1)[0] + "::" + new
    return new
