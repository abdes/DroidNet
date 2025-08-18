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
from .reporting import Reporter
from ._version import __version__


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
    # Preferred: a single base path; tool derives .h, .hlsl, .json, .heaps.d3d12.json
    p.add_argument(
        "--out-base",
        required=False,
        help="Output base path without extension (e.g. path/to/BindingSlots)",
    )
    # Legacy: explicit C++ and HLSL output paths; kept for backward compatibility
    p.add_argument(
        "--out-cpp",
        required=False,
        help="(Deprecated) Output path for the generated C++ header",
    )
    p.add_argument(
        "--out-hlsl",
        required=False,
        help="(Deprecated) Output path for the generated HLSL header",
    )
    p.add_argument(
        "--dry-run",
        action="store_true",
        help="Validate input and process templates without writing output files",
    )
    p.add_argument(
        "--schema",
        required=False,
        help="Optional path to Spec.schema.json to use for validation",
    )
    # Verbosity and color control
    p.add_argument(
        "-v",
        "--verbose",
        action="count",
        default=0,
        help="Increase verbosity (-v: verbose, -vv: debug)",
    )
    p.add_argument(
        "-q",
        "--quiet",
        action="count",
        default=0,
        help="Decrease verbosity (can be used multiple times)",
    )
    p.add_argument(
        "--color",
        choices=["auto", "always", "never"],
        default="auto",
        help="Color output mode",
    )
    p.add_argument(
        "--ts-strategy",
        choices=["preserve", "omit", "git-sha"],
        default="preserve",
        help="Timestamp strategy for generated files: preserve (keep existing), omit (no timestamps), or git-sha (embed git short sha)",
    )
    p.add_argument(
        "--version",
        action="store_true",
        help="Print version and exit",
    )
    args = p.parse_args(argv)
    if args.version:
        print(f"BindlessCodeGen {__version__}")
        return 0
    # Validate argument combinations
    if not args.out_base and not (args.out_cpp and args.out_hlsl):
        p.error(
            "Either --out-base or both --out-cpp and --out-hlsl must be provided"
        )
    if args.out_base and (args.out_cpp or args.out_hlsl):
        p.error(
            "Use either --out-base or the legacy --out-cpp/--out-hlsl, not both"
        )

    # Compute verbosity level: base 1, +1 per -v, -1 per -q, clamp [0..3]
    verbosity = max(0, min(3, 1 + int(args.verbose) - int(args.quiet)))
    rep = Reporter(verbosity=verbosity, color_mode=args.color)

    # Route to generator
    if args.out_base:
        generate(
            args.input,
            None,
            None,
            dry_run=args.dry_run,
            schema_path=args.schema,
            out_base=args.out_base,
            ts_strategy=args.ts_strategy,
            reporter=rep,
        )
    else:
        generate(
            args.input,
            args.out_cpp,
            args.out_hlsl,
            dry_run=args.dry_run,
            schema_path=args.schema,
            ts_strategy=args.ts_strategy,
            reporter=rep,
        )


if __name__ == "__main__":
    main()
