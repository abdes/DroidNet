# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

"""Command line of SceneSchemaCodeGen, run by the Scene build."""

# pyright: strict

from __future__ import annotations

import argparse
import sys
from collections.abc import Sequence
from pathlib import Path

from . import changes, cpp, model


def write_if_changed(path: Path, text: str) -> bool:
    """Writes `text` with LF endings unless the file already holds it."""
    data = text.encode("utf-8")
    if path.is_file() and path.read_bytes() == data:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return True


def main(argv: Sequence[str] | None = None) -> int:
    """Validates the schema and generates its C++ headers."""
    parser = argparse.ArgumentParser(prog="scene_schema_codegen")
    parser.add_argument(
        "--schema", required=True, type=Path, help="scene components YAML file"
    )
    parser.add_argument(
        "--json-schema",
        required=True,
        type=Path,
        help="JSON Schema that validates the YAML file",
    )
    parser.add_argument(
        "--cpp-out", required=True, type=Path, help="C++ header of the ids to write"
    )
    parser.add_argument(
        "--changes-out",
        required=True,
        type=Path,
        help="C++ header of the change encoder and appliers to write",
    )
    args = parser.parse_args(argv)
    schema_path: Path = args.schema
    json_schema: Path = args.json_schema
    cpp_out: Path = args.cpp_out
    changes_out: Path = args.changes_out

    try:
        schema = model.load(schema_path, json_schema)
        outputs = (
            (cpp_out, cpp.render(schema, schema_path.name)),
            (changes_out, changes.render(schema, schema_path.name)),
        )
    except model.SchemaError as error:
        print(f"[SceneSchemaCodeGen] {schema_path}: {error}", file=sys.stderr)
        return 1
    for path, text in outputs:
        changed = write_if_changed(path, text)
        print(
            f"[SceneSchemaCodeGen] {'Wrote' if changed else 'Unchanged'} "
            f"{path.name}: {len(schema.components)} components, "
            f"{len(schema.overrides())} changeable properties"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
