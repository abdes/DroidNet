"""Normalize Oxygen include delimiters and locate literal include directives."""

from __future__ import annotations

import re
from bisect import bisect_right

from .lexical import REGIONS

_REGIONS = re.compile(REGIONS.pattern.encode())
_SPLICE = re.compile(rb"\\\r?\n")
_FORMAT_SWITCH = re.compile(rb"(?:/\*|//)[ \t]*clang-format[ \t]+(off|on)\b")
_INCLUDE = re.compile(rb'#[ \t]*include[ \t]+(?P<header><[^<>\r\n]+>|"[^"\r\n]+")')


def prepare_includes(content: bytes) -> tuple[bytes, list[tuple[int, int]]]:
    # Preprocessor directives belong to logical lines. Keep a sparse offset map
    # so spliced comments/macros are recognized without rewriting source bytes.
    parts = []
    cuts = []
    removed = []
    cursor = total = 0
    for splice in _SPLICE.finditer(content):
        parts.append(content[cursor : splice.start()])
        cuts.append(splice.start() - total)
        total += splice.end() - splice.start()
        removed.append(total)
        cursor = splice.end()
    parts.append(content[cursor:])
    logical = b"".join(parts)
    disabled = []
    disabled_start = None

    def mask_region(match: re.Match) -> bytes:
        nonlocal disabled_start
        switch = _FORMAT_SWITCH.match(match.group())
        if switch:
            if switch[1] == b"off" and disabled_start is None:
                disabled_start = match.start()
            elif switch[1] == b"on" and disabled_start is not None:
                disabled.append((disabled_start, match.end()))
                disabled_start = None
        return re.sub(rb"[^\n]", b" ", match.group())

    masked = _REGIONS.sub(mask_region, logical)
    if disabled_start is not None:
        disabled.append((disabled_start, len(logical)))

    def original_offset(offset: int) -> int:
        index = bisect_right(cuts, offset) - 1
        return offset + (removed[index] if index >= 0 else 0)

    normalized = bytearray(content)
    ranges = []
    for directive in _INCLUDE.finditer(logical):
        start = directive.start()
        if any(begin <= start < end for begin, end in disabled):
            continue
        line_start = logical.rfind(b"\n", 0, start) + 1
        prefix = masked[line_start:start]
        if line_start == 0:
            prefix = prefix.removeprefix(b"\xef\xbb\xbf")
        if masked[start : start + 1] != b"#" or prefix.strip():
            continue
        begin = original_offset(start)
        end = original_offset(directive.end())
        ranges.append((begin, end - begin))
        if directive.group("header").startswith(b'"Oxygen/'):
            normalized[original_offset(directive.start("header"))] = ord("<")
            normalized[original_offset(directive.end("header") - 1)] = ord(">")
    return bytes(normalized), ranges
