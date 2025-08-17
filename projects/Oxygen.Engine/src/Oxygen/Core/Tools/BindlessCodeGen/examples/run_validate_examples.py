#!/usr/bin/env python
"""Run bindless generator dry-run against all examples in this folder.

Exits with code 0 if all examples validate, non-zero on first failure.
"""
import sys
from pathlib import Path

root = Path(__file__).resolve().parent
examples = sorted(root.glob("*.yaml"))
if not examples:
    print("No example YAML files found.")
    sys.exit(0)

# import generator from package path relative to this file
import importlib.util

spec = importlib.util.spec_from_file_location(
    "bindless_codegen",
    str(root.parent.joinpath("src", "bindless_codegen", "generator.py")),
)
if spec is None or spec.loader is None:
    print("Failed to locate generator module at expected path.")
    sys.exit(3)
# At this point spec is guaranteed non-None; help static type checkers
assert spec is not None
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

failures = []
for ex in examples:
    print(f"Validating example: {ex.name}")
    try:
        ok = mod.generate(str(ex), "out.cpp", "out.hlsl", dry_run=True)
        if not ok:
            print(f"Generator returned failure for {ex.name}")
            failures.append(ex.name)
            break
    except Exception as e:
        print(f"Validation failed for {ex.name}: {e}")
        failures.append(ex.name)
        break

if failures:
    print(f"{len(failures)} example(s) failed validation: {failures}")
    sys.exit(2)
print("All examples validated successfully.")
sys.exit(0)
