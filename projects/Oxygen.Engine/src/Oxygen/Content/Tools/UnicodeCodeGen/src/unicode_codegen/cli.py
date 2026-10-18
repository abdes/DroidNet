# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

"""Command line of UnicodeCodeGen, run by the Content build."""

from __future__ import annotations

import argparse
from collections.abc import Sequence
from pathlib import Path

from . import cpp, ucd


def write_if_changed(path: Path, text: str) -> bool:
    """Writes `text` with LF endings unless the file already holds it."""
    data = text.encode("utf-8")
    if path.is_file() and path.read_bytes() == data:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return True


def main(argv: Sequence[str] | None = None) -> int:
    """Generates the tables of one database folder."""
    parser = argparse.ArgumentParser(prog="unicode_codegen")
    parser.add_argument(
        "--ucd", required=True, type=Path, help="database folder, named by version"
    )
    parser.add_argument(
        "--cpp-out", required=True, type=Path, help="C++ header to write"
    )
    args = parser.parse_args(argv)
    ucd_dir: Path = args.ucd
    cpp_out: Path = args.cpp_out

    data = ucd.load(ucd_dir)
    source = f"Unicode {data.version} data"
    changed = write_if_changed(cpp_out, cpp.render(data, source))
    print(
        f"[UnicodeCodeGen] {'Wrote' if changed else 'Unchanged'} {cpp_out.name}: "
        f"{len(data.assigned)} assigned ranges, "
        f"{len(data.simple_case_folding)} case foldings"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
