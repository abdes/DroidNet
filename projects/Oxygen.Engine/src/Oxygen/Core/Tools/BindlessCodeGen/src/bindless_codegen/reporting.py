# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

"""Lightweight, colorized reporter with verbosity levels.

Levels (numeric, higher means more verbose):
  0 = quiet (errors only)
  1 = normal (info + warnings + errors)
  2 = verbose (adds progress details)
  3 = debug (adds debug traces)

Color control:
  mode = "auto" (default): enable colors when stderr is a TTY
  mode = "always": force-enable colors
  mode = "never": disable colors
"""

from __future__ import annotations

import os
import sys
from dataclasses import dataclass
from typing import Any


def _supports_color(stream) -> bool:
    if os.environ.get("NO_COLOR") is not None:
        return False
    try:
        return stream.isatty()
    except Exception:
        return False


@dataclass
class Reporter:
    verbosity: int = 1
    color_mode: str = "auto"  # auto|always|never

    def __post_init__(self):
        self._use_color = self._decide_color()
        # ANSI codes
        self._C_RESET = "\033[0m"
        self._C_DIM = "\033[2m"
        self._C_BOLD = "\033[1m"
        self._C_RED = "\033[31m"
        self._C_GREEN = "\033[32m"
        self._C_YELLOW = "\033[33m"
        self._C_BLUE = "\033[34m"

    def _decide_color(self) -> bool:
        mode = (self.color_mode or "auto").lower()
        if mode == "never":
            return False
        if mode == "always":
            return True
        return _supports_color(sys.stderr)

    # Formatting helpers
    def _wrap(self, s: str, color: str | None) -> str:
        if not self._use_color or not color:
            return s
        return f"{color}{s}{self._C_RESET}"

    def _hdr(self, label: str, color: str) -> str:
        return self._wrap(f"[{label}]", color)

    # Public API
    def error(self, msg: str, *args: Any) -> None:
        s = msg % args if args else msg
        sys.stderr.write(self._hdr("ERROR", self._C_RED) + f" {s}\n")
        sys.stderr.flush()

    def warn(self, msg: str, *args: Any) -> None:
        if self.verbosity >= 0:  # visible at all levels except <0
            s = msg % args if args else msg
            sys.stderr.write(self._hdr("WARN", self._C_YELLOW) + f" {s}\n")
            sys.stderr.flush()

    def info(self, msg: str, *args: Any) -> None:
        if self.verbosity >= 1:
            s = msg % args if args else msg
            sys.stderr.write(self._hdr("INFO", self._C_GREEN) + f" {s}\n")
            sys.stderr.flush()

    def progress(self, msg: str, *args: Any) -> None:
        if self.verbosity >= 2:
            s = msg % args if args else msg
            sys.stderr.write(self._hdr("..", self._C_BLUE) + f" {s}\n")
            sys.stderr.flush()

    def debug(self, msg: str, *args: Any) -> None:
        if self.verbosity >= 3:
            s = msg % args if args else msg
            sys.stderr.write(self._hdr("DBG", self._C_DIM) + f" {s}\n")
            sys.stderr.flush()
