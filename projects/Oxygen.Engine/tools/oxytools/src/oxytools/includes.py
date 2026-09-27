"""Normalize Oxygen include delimiters and locate literal include directives."""

from __future__ import annotations

import re
from bisect import bisect_right

from .common import ToolError
from .lexical import REGIONS

_REGIONS = re.compile(REGIONS.pattern.encode())
_SPLICE = re.compile(rb"\\\r?\n")
_FORMAT_SWITCH = re.compile(rb"(?:/\*|//)[ \t]*clang-format[ \t]+(off|on)\b")
_INCLUDE = re.compile(rb'#[ \t]*include[ \t]*(?P<header><[^<>\r\n]+>|"[^"\r\n]+")')
_CONDITIONAL = re.compile(
    rb"^[ \t]*(?:\xef\xbb\xbf)?#[ \t]*(if|ifdef|ifndef|elif|elifdef|elifndef|else|endif)\b[^\n]*",
    re.MULTILINE,
)
# Raw SDK headers that require architecture macros from Windows.h. Do not add
# self-contained headers such as strsafe.h merely because they belong to Win32.
_WINDOWS_LEAF_HEADERS = frozenset(
    {
        b"winnt.h",
        b"minwindef.h",
        b"windef.h",
        b"libloaderapi.h",
        b"minwinbase.h",
        b"winbase.h",
    }
)


def prepare_includes(
    content: bytes, *, respect_format_off: bool = True
) -> tuple[bytes, list[tuple[int, int]]]:
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
        if respect_format_off and any(begin <= start < end for begin, end in disabled):
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


def _include_records(content: bytes) -> list[tuple[bytes, tuple[int, ...]]]:
    logical = _SPLICE.sub(b"", content)
    _, ranges = prepare_includes(logical, respect_format_off=False)
    masked = _REGIONS.sub(lambda match: re.sub(rb"[^\n]", b" ", match.group()), logical)
    events = [
        (match.start(), match[1], None) for match in _CONDITIONAL.finditer(masked)
    ]
    for offset, length in ranges:
        directive = _INCLUDE.fullmatch(logical[offset : offset + length])
        if directive is not None:
            events.append((offset, b"include", directive.group("header")[1:-1].lower()))

    records = []
    branches = []
    branch_id = 0
    for _, kind, header in sorted(events):
        if kind in (b"if", b"ifdef", b"ifndef"):
            branch_id += 1
            branches.append(branch_id)
        elif kind in (b"else", b"elif", b"elifdef", b"elifndef") and branches:
            branch_id += 1
            branches[-1] = branch_id
        elif kind == b"endif" and branches:
            branches.pop()
        elif kind == b"include":
            records.append((header, tuple(branches)))
    return records


def validate_windows_bootstrap(original: bytes, updated: bytes) -> None:
    """Reject include rewrites that discard Windows SDK initialization."""
    before = _include_records(original)
    after = _include_records(updated)
    if before == after:
        return
    if sum(name == b"windows.h" for name, _ in after) < sum(
        name == b"windows.h" for name, _ in before
    ):
        raise ToolError(
            "Include cleanup cannot remove Windows.h: it initializes Windows SDK "
            "architecture macros. Retain it with '// IWYU pragma: keep'."
        )
    bootstrap_branches = []
    for header, branch in after:
        if header == b"windows.h":
            bootstrap_branches.append(branch)
        elif header in _WINDOWS_LEAF_HEADERS and not any(
            branch[: len(ancestor)] == ancestor for ancestor in bootstrap_branches
        ):
            raise ToolError(
                f"Include cleanup requires Windows.h before {header.decode()} "
                "in the same branch or an enclosing branch; Win32 leaf headers "
                "must not depend on incidental include order."
            )
