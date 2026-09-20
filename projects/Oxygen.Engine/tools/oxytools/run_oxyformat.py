"""Run this checkout's formatter using already installed dependencies."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "src"))

try:
    from oxyformat.cli import main

    raise SystemExit(main())
except ModuleNotFoundError as error:
    print(
        f"Missing dependency {error.name!r}; install this checkout's tools/oxytools "
        "package in the selected Python interpreter.",
        file=sys.stderr,
    )
    raise SystemExit(2) from error
