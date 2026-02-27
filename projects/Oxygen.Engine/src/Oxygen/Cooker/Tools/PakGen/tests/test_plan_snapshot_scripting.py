"""Deterministic plan snapshot for scripting-enabled YAML spec."""

from pathlib import Path

from pakgen.api import plan_dry_run
from snapshot_helper import assert_matches_snapshot


def test_plan_snapshot_scripting_yaml():  # noqa: N802
    spec_path = Path(__file__).parent / "_golden" / "scripting_scene_spec.yaml"
    _plan, d = plan_dry_run(spec_path, deterministic=True)
    assert_matches_snapshot(d, "plan_scripting_scene_deterministic.json")
