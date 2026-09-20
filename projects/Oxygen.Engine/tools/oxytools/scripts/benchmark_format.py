"""Measure complete fresh-process checks without modifying source files."""

from __future__ import annotations

import argparse
import json
import statistics
import subprocess
import sys
import time
from pathlib import Path

from oxyformat.cli import find_root
from oxyformat.selection import select_files
from oxytools.ownership import Ownership


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", type=Path, default=find_root())
    parser.add_argument("--scope", default="src/Oxygen/Base")
    parser.add_argument("--repeats", type=int, default=5)
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error("--repeats must be positive")
    root = args.project_root.resolve()
    selection = select_files(Ownership.load(root), [root / args.scope])
    if selection.errors or not selection.files:
        parser.error(f"Scope must contain accessible C++ files: {selection.errors}")
    launcher = Path(__file__).resolve().parents[1] / "run_oxyformat.py"
    sizes = sorted(
        {min(size, len(selection.files)) for size in (1, 10, 50, len(selection.files))}
    )
    results = []
    for size in sizes:
        times = []
        for _ in range(args.repeats):
            started = time.perf_counter()
            result = subprocess.run(
                [
                    sys.executable,
                    str(launcher),
                    "--project-root",
                    str(root),
                    *map(str, selection.files[:size]),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            times.append(round((time.perf_counter() - started) * 1000, 2))
            if result.returncode not in (0, 1):
                raise RuntimeError(result.stderr or result.stdout)
        results.append(
            {
                "files": size,
                "first_ms": times[0],
                "median_ms": statistics.median(times),
                "max_ms": max(times),
                "samples_ms": times,
            }
        )
    print(
        json.dumps(
            {
                "python": sys.executable,
                "scope": args.scope,
                "cache": "none; fresh Python process per sample",
                "results": results,
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
