# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

"""BindlessCodeGen package

This package provides a small command-line tool and library used to
generate canonical bindless binding slot headers for both C++ and HLSL
from a single YAML source-of-truth.

Public API is intentionally minimal; prefer importing the CLI entry
point from :mod:`bindless_codegen.cli` or the programmatic generator
from :mod:`bindless_codegen.generator`.
"""

from ._version import __version__  # noqa: F401

__all__ = ["__version__"]


def get_cli_module():
    """Lazily import and return the CLI module."""
    from . import cli as _cli

    return _cli


def get_generator_module():
    """Lazily import and return the generator module."""
    from . import generator as _g

    return _g
