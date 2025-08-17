# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

"""Command-line entry point for BindlessCodeGen.

This small CLI loads a YAML single-source-of-truth file describing bindless
domains and emits a C++ header and an HLSL header with canonical binding
slot constants. The tool is typically invoked from CMake during builds.
"""

import argparse
import sys
from .generator import generate


def main(argv=None):
    """Parse CLI args and run the generator.

    Args:
        argv: Optional list of arguments (defaults to sys.argv[1:]).
    """
    p = argparse.ArgumentParser(prog="bindless_codegen")
    p.add_argument(
        "--input",
        required=True,
        help="Path to the YAML source-of-truth describing binding domains",
    )
    p.add_argument(
        "--out-cpp",
        required=True,
        help="Output path for the generated C++ header (e.g. BindingSlots.h)",
    )
    p.add_argument(
        "--out-hlsl",
        required=True,
        help="Output path for the generated HLSL header (e.g. BindingSlots.hlsl)",
    )
    p.add_argument(
        "--dry-run",
        action="store_true",
        help="Validate input and process templates without writing output files",
    )
    p.add_argument(
        "--schema",
        required=False,
        help="Optional path to BindingSlots.schema.json to use for validation",
    )
    args = p.parse_args(argv)
    # Use explicit keyword args for clarity; generate enforces keyword-only options.
    generate(
        args.input,
        args.out_cpp,
        args.out_hlsl,
        dry_run=args.dry_run,
        schema_path=args.schema,
    )


if __name__ == "__main__":
    main()
