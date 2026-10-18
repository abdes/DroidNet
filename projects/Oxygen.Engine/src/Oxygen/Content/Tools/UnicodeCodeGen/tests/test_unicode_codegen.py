# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

"""Tests of the database reader, the C++ rendering and the command line."""

from __future__ import annotations

from pathlib import Path

import pytest

from unicode_codegen import cli, cpp, ucd

_DATABASE = Path(__file__).resolve().parents[3] / "Unicode" / "15.1.0"


def test_assigned_ranges_are_merged_and_sorted() -> None:
    text = "# DerivedAge-15.1.0.txt\n0041..005A ; 1.1\n0030..0039 ; 1.1\n005B ; 1.1\n"
    assert ucd.parse_assigned(text) == ((0x30, 0x39), (0x41, 0x5B))


def test_simple_case_folding_keeps_common_and_simple_status() -> None:
    text = (
        "0041; C; 0061; # A\n"
        "00DF; F; 0073 0073; # sharp s\n"
        "1E9E; F; 0073 0073; # capital sharp s\n"
        "1E9E; S; 00DF; # capital sharp s\n"
        "0130; T; 0069; # dotted I\n"
    )
    assert ucd.parse_simple_case_folding(text) == ((0x41, 0x61), (0x1E9E, 0xDF))


def test_pinned_database_matches_its_folder() -> None:
    data = ucd.load(_DATABASE)
    assert data.version == "15.1.0"
    assigned = data.assigned
    assert any(first <= 0x2EBF0 <= last for first, last in assigned)
    assert not any(first <= 0x0378 <= last for first, last in assigned)
    folding = dict(data.simple_case_folding)
    assert folding[ord("A")] == ord("a")
    assert folding[0x1E9E] == 0xDF
    assert 0xDF not in folding


def test_mismatched_folder_is_refused(tmp_path: Path) -> None:
    folder = tmp_path / "16.0.0"
    folder.mkdir()
    for name in ("DerivedAge.txt", "CaseFolding.txt"):
        (folder / name).write_bytes((_DATABASE / name).read_bytes())
    with pytest.raises(ValueError):
        ucd.load(folder)


def test_header_is_written_only_when_it_changes(tmp_path: Path) -> None:
    out = tmp_path / "Generated.UnicodeTables.h"
    args = ["--ucd", str(_DATABASE), "--cpp-out", str(out)]
    assert cli.main(args) == 0
    first = out.read_bytes()
    stamp = out.stat().st_mtime_ns
    assert cli.main(args) == 0
    assert out.read_bytes() == first
    assert out.stat().st_mtime_ns == stamp
    assert b"\r\n" not in first
    assert b'kVersion = "15.1.0"' in first


def test_rendering_lists_every_entry() -> None:
    data = ucd.UnicodeData(
        version="15.1.0",
        assigned=((0x0, 0x377),),
        simple_case_folding=((0x41, 0x61), (0x42, 0x62)),
    )
    text = cpp.render(data, "test")
    assert "std::array<CodePointRange, 1>" in text
    assert "{ 0x000000, 0x000377 }," in text
    assert "std::array<CaseFoldingPair, 2>" in text
    assert "{ 0x000042, 0x000062 }," in text
