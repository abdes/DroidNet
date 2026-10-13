# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#
# pyright: strict

"""Formatting and writing shared by every generator output.

Generated C++ must already be what the repository's formatting and tidy rules
leave behind, so that a build never produces a diff that the hooks would then
undo:

- clang-format lays the text out;
- `readability-trailing-comma` (policy `Append`) wants a trailing comma on every
  braced list that spans lines, and clang-format then puts each element on its
  own line. `append_trailing_commas` applies that rule, and the two are
  repeated until the text no longer changes;
- the literal rules (uppercase suffixes, a lowercase `0x` prefix) are checked,
  so that an emitter that violates them fails the build instead of leaving the
  fix to a later tidy run.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path

_MAX_PASSES = 5

# Tokens that matter to the brace classifier. Comments, strings and raw strings
# are single tokens so that their contents are never mistaken for code.
_TOKEN = re.compile(
    r"""
      (?P<line_comment>//[^\n]*)
    | (?P<block_comment>/\*.*?\*/)
    | (?P<directive>^[ \t]*\#(?:[^\n\\]|\\\n|\\.)*)
    | (?P<raw_string>(?:u8|u|U|L)?R"(?P<delim>[^(\s]{0,16})\(.*?\)(?P=delim)")
    | (?P<string>(?:u8|u|U|L)?"(?:[^"\\\n]|\\.)*")
    | (?P<char>'(?:[^'\\\n]|\\.)+')
    | (?P<number>\.?\d(?:[eEpP][+-]|[\w.'])*)
    | (?P<word>[A-Za-z_]\w*)
    | (?P<space>\s+)
    | (?P<other>.)
    """,
    re.VERBOSE | re.DOTALL | re.MULTILINE,
)

# A statement head starting with one of these opens a body, not a braced list.
_BODY_KEYWORDS = frozenset(
    {
        "struct",
        "class",
        "union",
        "enum",
        "namespace",
        "extern",
        "else",
        "do",
        "try",
    }
)


@dataclass
class _Token:
    kind: str
    text: str
    start: int


@dataclass
class _Brace:
    is_list: bool
    token_index: int
    last_significant: int  # index of the last significant token inside, or -1


def _tokenize(text: str) -> list[_Token]:
    return [
        _Token(match.lastgroup or "other", match.group(), match.start())
        for match in _TOKEN.finditer(text)
    ]


def _is_significant(token: _Token) -> bool:
    return token.kind not in ("space", "line_comment", "block_comment")


def append_trailing_commas(text: str) -> str:
    """Append a trailing comma to every braced initializer list that spans
    lines, as `readability-trailing-comma` does with policy `Append`."""
    tokens = _tokenize(text)
    stack: list[_Brace] = []
    parens = 0
    head: list[_Token] = []  # significant tokens since the last boundary
    insert_after: list[int] = []

    for index, token in enumerate(tokens):
        if not _is_significant(token):
            continue
        if token.kind == "directive":
            head.clear()
            continue
        if token.text == "(":
            parens += 1
        elif token.text == ")":
            parens -= 1

        if token.text == "{":
            parent_is_list = bool(stack) and stack[-1].is_list
            if parens > 0 or (head and head[-1].text == "="):
                is_list = True
            elif not head:
                is_list = parent_is_list
            elif any(t.text == "enum" for t in head):
                # The rule covers enumerator lists as well.
                is_list = True
            else:
                is_list = not any(t.text == "(" for t in head) and not any(
                    t.text in _BODY_KEYWORDS for t in head
                )
            if stack:
                stack[-1].last_significant = index
            stack.append(_Brace(is_list, index, -1))
            head.clear()
            continue

        if token.text == "}":
            brace = stack.pop() if stack else None
            if (
                brace is not None
                and brace.is_list
                and brace.last_significant >= 0
                and tokens[brace.last_significant].text != ","
                and "\n" in text[tokens[brace.token_index].start : token.start]
            ):
                insert_after.append(brace.last_significant)
            if stack:
                stack[-1].last_significant = index
            head.clear()
            continue

        if token.text == ";":
            head.clear()
        else:
            head.append(token)
        if stack:
            stack[-1].last_significant = index

    if not insert_after:
        return text
    pieces: list[str] = []
    cursor = 0
    for index in sorted(set(insert_after)):
        token = tokens[index]
        end = token.start + len(token.text)
        pieces.append(text[cursor:end])
        pieces.append(",")
        cursor = end
    pieces.append(text[cursor:])
    return "".join(pieces)


_LITERAL_PREFIX = re.compile(r"^0[xXbB]")
_INTEGER_SUFFIX = re.compile(
    r"^(?:0[xX][0-9A-Fa-f']+|0[bB][01']+|\d[\d']*)([uUlLzZ]*)$"
)
_FLOAT_SUFFIX = re.compile(
    r"^(?:\d[\d']*\.\d*|\.\d[\d']*|\d[\d']*)(?:[eE][+-]?\d+)?([fFlL]?)$"
)


def check_literals(text: str, path: str) -> None:
    """Fail when the text holds a literal the repository's tidy rules would
    rewrite: a lowercase suffix or an uppercase `0X`/`0B` prefix."""
    for token in _tokenize(text):
        if token.kind != "number":
            continue
        literal = token.text
        prefix = _LITERAL_PREFIX.match(literal)
        if prefix and prefix.group()[1] in "XB":
            raise ValueError(f"{path}: literal '{literal}' must use a lowercase prefix")
        match = _INTEGER_SUFFIX.match(literal)
        if match is None and not literal.lower().startswith(("0x", "0b")):
            match = _FLOAT_SUFFIX.match(literal)
        if match is not None and match.group(1) != match.group(1).upper():
            raise ValueError(
                f"{path}: literal '{literal}' must use an uppercase suffix"
            )


def _clang_format_command(path: str) -> list[str]:
    style = next(
        (
            parent / ".clang-format"
            for parent in Path(__file__).resolve().parents
            if (parent / ".clang-format").is_file()
        ),
        None,
    )
    if style is None:
        raise RuntimeError("BindlessCodeGen requires the engine .clang-format")
    formatter = shutil.which("clang-format")
    if formatter is None and os.name == "nt":
        candidate = (
            Path(os.environ.get("ProgramFiles", "C:/Program Files"))
            / "LLVM/bin/clang-format.exe"
        )
        formatter = str(candidate) if candidate.is_file() else None
    if formatter is None:
        raise RuntimeError("BindlessCodeGen requires clang-format on PATH")
    return [formatter, f"--style=file:{style}", f"--assume-filename={path}"]


def _clang_format(content: str, path: str) -> str:
    result = subprocess.run(
        _clang_format_command(path),
        input=content,
        text=True,
        encoding="utf-8",
        capture_output=True,
        check=True,
    )
    return result.stdout


def format_cpp(content: str, path: str) -> str:
    """Format C++ with the engine style, as oxyformat does, and apply the
    repository's trailing-comma rule, so that generated bytes compare equal to
    what the formatting hook and `clang-tidy --fix` would leave behind."""
    check_literals(content, path)
    text = _clang_format(content, path)
    for _ in range(_MAX_PASSES):
        fixed = append_trailing_commas(text)
        if fixed == text:
            return text
        text = _clang_format(fixed, path)
    raise RuntimeError(f"{path} does not reach a formatting fixed point")


def write_if_changed(path: Path, content: str) -> bool:
    """Write `content` with LF line endings; False when the file already
    holds exactly that content."""
    if path.is_file() and path.read_bytes() == content.encode("utf-8"):
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(content.encode("utf-8"))
    os.replace(temporary, path)
    return True
