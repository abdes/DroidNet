#!/usr/bin/env python
"""Run bindless generator dry-run against all examples in this folder.

Exits with code 0 if all examples validate, non-zero on first failure.
"""
import sys
import os
import yaml
import argparse
from pathlib import Path

root = Path(__file__).resolve().parent
examples = sorted(root.glob("*.yaml"))


# --- simple color + symbol helpers -------------------------------------------------
def _supports_color(stream) -> bool:
    if os.environ.get("NO_COLOR") is not None:
        return False
    try:
        return stream.isatty()
    except Exception:
        return False


_USE_COLOR = _supports_color(sys.stdout)
_C_RESET = "\033[0m"
_C_DIM = "\033[2m"
_C_BOLD = "\033[1m"
_C_RED = "\033[31m"
_C_GREEN = "\033[32m"
_C_YELLOW = "\033[33m"
_C_BLUE = "\033[34m"


def _wrap(s: str, color: str | None) -> str:
    if not _USE_COLOR or not color:
        return s
    return f"{color}{s}{_C_RESET}"


ARROW = _wrap("->", _C_BLUE)
CHECK = _wrap("✓", _C_GREEN)
CROSS = _wrap("✗", _C_RED)
SKIP = _wrap("-", _C_YELLOW)


if not examples:
    print(_wrap("No example YAML files found.", _C_YELLOW))
    sys.exit(0)

print(_wrap("Bindless Examples Validation", _C_BOLD))

# CLI args: -v to increase tool verbosity; default is quiet
parser = argparse.ArgumentParser(add_help=False)
parser.add_argument("-v", action="store_true", dest="verbose")
args, _unknown = parser.parse_known_args()

# Import generator as a proper package to satisfy relative imports
import importlib

pkg_root = root.parent / "src"
sys.path.insert(0, str(pkg_root))
try:
    mod = importlib.import_module("bindless_codegen.generator")
    try:
        rep_mod = importlib.import_module("bindless_codegen.reporting")
        Reporter = getattr(rep_mod, "Reporter")
    except Exception:
        Reporter = None  # type: ignore[assignment]
except Exception as e:
    print(
        _wrap(
            f"Failed to import bindless codegen package from {pkg_root}: {e}",
            _C_RED,
        )
    )
    sys.exit(3)

failures = []
for ex in examples:
    print(f"{ARROW} {ex.name}")
    try:
        with open(ex, "r", encoding="utf-8") as f:
            doc = yaml.safe_load(f) or {}
        if not isinstance(doc, dict) or (
            "domains" not in doc and "root_signature" not in doc
        ):
            print(f"   {SKIP} skipping (not a BindingSlots doc)")
            continue
        # Dry-run: treat absence of exception as success; filenames are derived
        # from the provided base prefix.
        rep = (
            Reporter(verbosity=(2 if args.verbose else 0)) if Reporter else None
        )
        mod.generate(
            str(ex),
            None,
            None,
            dry_run=True,
            out_base="Generated.",
            reporter=rep,
        )
        print(f"{CHECK} validated")
    except Exception as e:
        print(_wrap(f"   {CROSS} {e}", _C_RED))
        failures.append(ex.name)
        break

if failures:
    print(
        _wrap(
            f"{len(failures)} example(s) failed validation: {failures}",
            _C_RED,
        )
    )
    sys.exit(2)
print(_wrap("All examples validated successfully.", _C_GREEN))
sys.exit(0)
