# ===----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===----------------------------------------------------------------------===#

"""One-command PakGen YAML -> PAK build helper.

Run from the RenderScene directory.

Example:
    F:/projects/.venv/Scripts/python.exe make_pak.py cube_scene_spec.yaml

Outputs:
    pak/<name>.pak
    pak/<name>.manifest.json
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import TYPE_CHECKING, Callable

if TYPE_CHECKING:
    from pakgen.api import BuildOptions as _BuildOptions
    from pakgen.api import BuildResult as _BuildResult


def _workspace_root_from_here() -> Path:
    # Examples/RenderScene/make_pak.py -> Examples/RenderScene -> Examples -> repo root
    return Path(__file__).resolve().parents[2]


def _import_pakgen_api(
    repo_root: Path,
) -> "tuple[type[_BuildOptions], Callable[[_BuildOptions], _BuildResult]]":
    pakgen_src = (
        repo_root / "src" / "Oxygen" / "Content" / "Tools" / "PakGen" / "src"
    ).resolve()
    sys.path.insert(0, str(pakgen_src))
    from pakgen.api import BuildOptions, build_pak  # type: ignore

    return BuildOptions, build_pak


def main(argv: list[str] | None = None) -> int:
    argv = sys.argv[1:] if argv is None else argv

    parser = argparse.ArgumentParser(
        description="Generate an Oxygen PAK from a PakGen YAML spec in the current directory",
    )
    parser.add_argument(
        "input",
        nargs="?",
        type=Path,
        help=(
            "Input file (relative to current directory): a PakGen *.yaml/*.yml spec. "
            "If omitted, uses the only *.yaml/*.yml in cwd."
        ),
    )
    parser.add_argument(
        "-a",
        "--all",
        action="store_true",
        help=(
            "Build all specs in bulk: build all *.yaml/*.yml specs in the current directory."
        ),
    )
    parser.add_argument(
        "--deterministic",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Build deterministic PAK layout (default: on).",
    )
    args = parser.parse_args(argv)

    cwd = Path.cwd()

    def build_one(input_path: Path) -> None:
        input_path = input_path.resolve()
        if not input_path.exists():
            raise SystemExit(f"Input file not found: {input_path}")

        out_dir = cwd / "pak"
        out_dir.mkdir(parents=True, exist_ok=True)

        suffix = input_path.suffix.lower()
        stem = input_path.stem
        pak_path = out_dir / f"{stem}.pak"
        manifest_path = out_dir / f"{stem}.manifest.json"

        if suffix in (".yaml", ".yml"):
            spec_path = input_path
        else:
            raise SystemExit(
                f"Unsupported input type: '{suffix}'. Expected *.yaml/*.yml"
            )

        # 2) YAML -> PAK
        repo_root = _workspace_root_from_here()
        BuildOptions, build_pak = _import_pakgen_api(repo_root)

        build_pak(
            BuildOptions(
                input_spec=spec_path,
                output_path=pak_path,
                manifest_path=manifest_path,
                deterministic=args.deterministic,
            )
        )

        print(f"Wrote: {pak_path}")
        print(f"Wrote: {manifest_path}")

    if args.all:
        if args.input is not None:
            raise SystemExit(
                "--all/-a cannot be used together with an input file argument"
            )

        yaml_specs = sorted(cwd.glob("*.yaml")) + sorted(cwd.glob("*.yml"))

        if not yaml_specs:
            raise SystemExit(
                "No inputs found for --all: expected *.yaml/*.yml in the current directory"
            )

        for path in yaml_specs:
            build_one(path)
        return 0

    input_path = args.input
    if input_path is None:
        candidates = sorted(cwd.glob("*.yaml")) + sorted(cwd.glob("*.yml"))
        if len(candidates) != 1:
            names = ", ".join(p.name for p in candidates)
            raise SystemExit(
                "Please pass an input file, or keep exactly one *.yaml/*.yml in this directory. "
                f"Found {len(candidates)}: [{names}]"
            )
        input_path = candidates[0]

    build_one(input_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
