from __future__ import annotations

"""Snapshot helper utilities for PakGen tests.

Supports auto-updating snapshots when environment variable
PAKGEN_UPDATE_SNAPSHOTS is set to a truthy value ("1", "true", "yes").

Usage:
    from snapshot_helper import assert_matches_snapshot
    assert_matches_snapshot(actual_dict, 'plan_basic_deterministic.json')

Snapshots live in tests/_snapshots/.
"""
import json
import os
from pathlib import Path
from typing import Any, Mapping

_SNAPSHOT_DIR = Path(__file__).parent / "_snapshots"


def _is_truthy(val: str | None) -> bool:
    if val is None:
        return False
    return val.lower() in {"1", "true", "yes", "on", "update"}


def assert_matches_snapshot(
    actual: Mapping[str, Any], snapshot_name: str
) -> None:  # noqa: D401
    """Compare mapping against stored JSON snapshot, optionally updating it.

    Raises AssertionError on mismatch unless auto-update is enabled.
    """
    _SNAPSHOT_DIR.mkdir(parents=True, exist_ok=True)
    snapshot_path = _SNAPSHOT_DIR / snapshot_name
    update = _is_truthy(os.getenv("PAKGEN_UPDATE_SNAPSHOTS"))

    # Canonicalize JSON (sorted keys for deterministic file)
    def _dump(obj) -> str:
        return json.dumps(obj, indent=2, sort_keys=True)

    if update or not snapshot_path.exists():
        snapshot_path.write_text(_dump(actual), encoding="utf-8")
        # If updating, still assert True to integrate with test run
        return

    expected = json.loads(snapshot_path.read_text(encoding="utf-8"))
    if actual != expected:
        # Provide diff context (simple textual diff)
        import difflib

        diff = "\n".join(
            difflib.unified_diff(
                _dump(expected).splitlines(),
                _dump(actual).splitlines(),
                fromfile="expected",
                tofile="actual",
                lineterm="",
            )
        )
        raise AssertionError(f"Snapshot mismatch for {snapshot_name}\n{diff}")
