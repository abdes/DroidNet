# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

"""Reads the Unicode Character Database files the rules need."""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path
from typing import TypeAlias

_VERSION = re.compile(r"^# [A-Za-z]+-(\d+\.\d+\.\d+)\.txt\s*$")

CodePointRange: TypeAlias = tuple[int, int]
FoldingPair: TypeAlias = tuple[int, int]


@dataclass(frozen=True)
class UnicodeData:
    """The tables generated from one Unicode version."""

    version: str
    assigned: tuple[CodePointRange, ...]
    simple_case_folding: tuple[FoldingPair, ...]


def _version_of(text: str, name: str) -> str:
    match = _VERSION.match(text.splitlines()[0]) if text else None
    if match is None:
        raise ValueError(f"{name} does not start with its version line")
    return match.group(1)


def _data_lines(text: str) -> list[list[str]]:
    rows: list[list[str]] = []
    for line in text.splitlines():
        content = line.split("#", 1)[0].strip()
        if content:
            rows.append([field.strip() for field in content.split(";")])
    return rows


def parse_assigned(text: str) -> tuple[CodePointRange, ...]:
    """Returns the assigned code points of DerivedAge.txt as merged ranges."""
    ranges: list[CodePointRange] = []
    for fields in _data_lines(text):
        first, _, last = fields[0].partition("..")
        ranges.append((int(first, 16), int(last or first, 16)))
    ranges.sort()
    merged: list[CodePointRange] = []
    for first, last in ranges:
        if merged and first <= merged[-1][1] + 1:
            merged[-1] = (merged[-1][0], max(merged[-1][1], last))
        else:
            merged.append((first, last))
    return tuple(merged)


def parse_simple_case_folding(text: str) -> tuple[FoldingPair, ...]:
    """Returns the C and S mappings of CaseFolding.txt, sorted by code point."""
    pairs: dict[int, int] = {}
    for fields in _data_lines(text):
        code, status, mapping = fields[0], fields[1], fields[2]
        if status in ("C", "S"):
            source = int(code, 16)
            if source in pairs:
                raise ValueError(f"CaseFolding.txt maps U+{code} twice")
            pairs[source] = int(mapping, 16)
    return tuple(sorted(pairs.items()))


def load(directory: Path) -> UnicodeData:
    """Reads the database in a folder named after its version."""
    texts = {
        name: (directory / name).read_text(encoding="utf-8")
        for name in ("DerivedAge.txt", "CaseFolding.txt")
    }
    versions = {_version_of(text, name) for name, text in texts.items()}
    if versions != {directory.name}:
        raise ValueError(
            f"{directory} holds Unicode {sorted(versions)}, not {directory.name}"
        )
    return UnicodeData(
        version=directory.name,
        assigned=parse_assigned(texts["DerivedAge.txt"]),
        simple_case_folding=parse_simple_case_folding(texts["CaseFolding.txt"]),
    )
