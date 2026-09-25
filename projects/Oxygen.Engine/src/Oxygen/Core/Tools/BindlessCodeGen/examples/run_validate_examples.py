#!/usr/bin/env python
"""Validate every example through the provisioned BindlessCodeGen package."""

import argparse
from pathlib import Path

from bindless_codegen.generator import generate
from bindless_codegen.reporting import Reporter

EXAMPLES_DIR = Path(__file__).resolve().parent
SCHEMA_PATH = EXAMPLES_DIR.parents[2] / "Meta" / "Bindless.schema.json"


def validate_examples(
    directory: Path, *, schema_path: Path = SCHEMA_PATH, verbose: bool = False
) -> int:
    """Return nonzero for missing inputs or any invalid example; never skip YAML."""
    if not schema_path.is_file():
        print(f"Cannot validate examples: schema not found: {schema_path}")
        return 2
    examples = sorted(directory.glob("*.yaml"))
    if not examples:
        print(f"Cannot validate examples: no YAML files found in {directory}")
        return 2

    print(f"Bindless Examples Validation ({len(examples)} files)")
    failures = []
    for example in examples:
        print(f"-> {example.name}")
        try:
            generate(
                str(example),
                dry_run=True,
                schema_path=str(schema_path),
                out_base=str(directory / "Generated."),
                reporter=Reporter(verbosity=2 if verbose else 0),
            )
        except Exception as error:
            # Report every bad input, rather than hiding the rest behind the first.
            print(f"   FAIL: {error}")
            failures.append(example.name)
        else:
            print("   OK: validated")

    if failures:
        print(
            f"{len(failures)}/{len(examples)} example(s) failed validation: {failures}"
        )
        return 2
    print(f"All {len(examples)} examples validated successfully.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="Show generator progress"
    )
    args = parser.parse_args()
    return validate_examples(EXAMPLES_DIR, verbose=args.verbose)


if __name__ == "__main__":
    raise SystemExit(main())
