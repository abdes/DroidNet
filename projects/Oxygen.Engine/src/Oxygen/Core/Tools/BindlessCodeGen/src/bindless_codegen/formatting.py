# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#
# pyright: strict

"""Formatting and writing shared by every generator output."""

from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path


def format_cpp(content: str, path: str) -> str:
    """Format C++ with the engine style, as oxyformat does, so that generated
    bytes compare equal to what the formatting hook would leave behind."""
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
    result = subprocess.run(
        [formatter, f"--style=file:{style}", f"--assume-filename={path}"],
        input=content,
        text=True,
        encoding="utf-8",
        capture_output=True,
        check=True,
    )
    return result.stdout


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
