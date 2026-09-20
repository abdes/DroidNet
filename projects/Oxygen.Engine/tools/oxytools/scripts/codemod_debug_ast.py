"""Inspect a C++ AST using the same libclang loader as codemod."""

import argparse
from pathlib import Path

from codemod.drivers.cpp_driver import load_clang


def dump(cursor, indent=0):
    reference = cursor.referenced
    reference_usr = reference.get_usr() if reference else ""
    print(
        f"{'  ' * indent}{cursor.kind}: {cursor.spelling!r} at {cursor.location.line}:{cursor.location.column}; usr={cursor.get_usr()}; reference={reference_usr}"
    )
    for child in cursor.get_children():
        dump(child, indent + 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("file", type=Path)
    parser.add_argument("--libclang-file")
    parser.add_argument(
        "flags", nargs=argparse.REMAINDER, help="Compiler flags after --"
    )
    args = parser.parse_args()
    _, index = load_clang(args.libclang_file)
    flags = args.flags[1:] if args.flags[:1] == ["--"] else args.flags
    unit = index.parse(str(args.file.resolve()), args=flags)
    for diagnostic in unit.diagnostics:
        print(diagnostic)
    dump(unit.cursor)


if __name__ == "__main__":
    main()
