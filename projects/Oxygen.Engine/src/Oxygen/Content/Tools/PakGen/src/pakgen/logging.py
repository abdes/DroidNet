"""Logging utilities for PakGen.

Stdlib logging wrapper with optional rich support.
"""

from __future__ import annotations

import logging
import sys
from contextlib import contextmanager
from typing import Iterator, Optional
from .reporting import get_reporter, get_verbosity

try:  # optional rich
    from rich.logging import RichHandler  # type: ignore

    _HAS_RICH = True
except ImportError:  # pragma: no cover
    _HAS_RICH = False

_LOGGER_NAME = "pakgen"
_SECTION_PREFIX = "==>"
_STEP_PREFIX = "  ->"

__all__ = [
    "get_logger",
    "configure_logging",
    "section",
    "step",
]


def get_logger() -> logging.Logger:
    return logging.getLogger(_LOGGER_NAME)


def configure_logging(
    verbosity: int = 0, *, use_color: Optional[bool] = None
) -> None:
    logger = get_logger()
    level = logging.INFO
    if verbosity == 1:
        level = logging.DEBUG
    elif verbosity >= 2:
        level = logging.DEBUG
    logger.setLevel(level)

    for h in list(logger.handlers):  # pragma: no cover
        logger.removeHandler(h)

    if use_color is None:
        use_color = sys.stderr.isatty()

    handler: logging.Handler

    class _ReporterHandler(logging.Handler):
        def emit(self, record: logging.LogRecord) -> None:  # noqa: D401
            rep = get_reporter()
            msg = record.getMessage()
            lvl = record.levelno
            if lvl >= logging.ERROR:
                rep.error(msg)
            elif lvl >= logging.WARNING:
                rep.warning(msg)
            elif lvl >= logging.INFO:
                rep.status(msg)
            else:
                rep.verbose(msg)

    handler = _ReporterHandler()
    handler.setFormatter(logging.Formatter("%(message)s"))
    logger.addHandler(handler)
    logger.verbose_enabled = verbosity >= 2  # type: ignore[attr-defined]


def _log_verbose(msg: str, *, level: int = 1) -> None:  # pragma: no cover
    if get_verbosity() < level:
        return
    logger = get_logger()
    if getattr(logger, "verbose_enabled", False):  # type: ignore[attr-defined]
        logger.debug(msg)


def step(message: str) -> None:
    rep = get_reporter()
    rep.status(f"{_STEP_PREFIX} {message}")


@contextmanager
def section(title: str) -> Iterator[logging.Logger]:
    logger = get_logger()
    rep = get_reporter()
    rep.section(title)
    try:
        yield logger
    finally:
        _log_verbose(f"end section: {title}", level=1)
