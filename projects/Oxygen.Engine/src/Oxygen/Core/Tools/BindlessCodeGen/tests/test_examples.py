"""Ensure example validation cannot silently skip malformed or current inputs."""

import importlib.util
from pathlib import Path
import shutil

import pytest
import yaml

TOOL = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "validate_bindless_examples", TOOL / "examples/run_validate_examples.py"
)
validator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(validator)


def test_all_checked_in_examples_are_validated(capsys):
    examples = sorted((TOOL / "examples").glob("*.yaml"))
    assert examples
    assert validator.validate_examples(TOOL / "examples") == 0
    output = capsys.readouterr().out
    for example in examples:
        assert f"-> {example.name}" in output
    assert output.count("OK: validated") == len(examples)


@pytest.mark.parametrize("document", ["", "{}", "[]", "unrelated: true", "abi: ["])
def test_invalid_yaml_cannot_be_skipped(tmp_path, capsys, document):
    (tmp_path / "bad.yaml").write_text(document, encoding="utf-8")
    assert validator.validate_examples(tmp_path) == 2
    output = capsys.readouterr().out
    assert "bad.yaml" in output
    assert "failed validation" in output
    assert "OK: validated" not in output


def test_valid_v2_is_executed_after_failures(tmp_path, capsys):
    (tmp_path / "a_bad.yaml").write_text("{}", encoding="utf-8")
    (tmp_path / "b_bad.yaml").write_text("[]", encoding="utf-8")
    shutil.copyfile(TOOL / "examples/bindless_basic.yaml", tmp_path / "z_valid.yaml")
    assert validator.validate_examples(tmp_path) == 2
    output = capsys.readouterr().out
    assert "2/3 example(s) failed validation" in output
    assert "OK: validated" in output
    # Dry-run validation must not leave generated outputs beside its inputs.
    assert {p.name for p in tmp_path.iterdir()} == {
        "a_bad.yaml",
        "b_bad.yaml",
        "z_valid.yaml",
    }


def test_backend_capacity_error_is_rejected(tmp_path, capsys):
    document = yaml.safe_load((TOOL / "examples/bindless_basic.yaml").read_text())
    document["backends"]["d3d12"]["strategy"]["heaps"][0]["capacity"] = 1
    (tmp_path / "bad_capacity.yaml").write_text(
        yaml.safe_dump(document), encoding="utf-8"
    )
    assert validator.validate_examples(tmp_path) == 2
    assert "exceeds heap" in capsys.readouterr().out


def test_empty_inventory_is_rejected(tmp_path, capsys):
    assert validator.validate_examples(tmp_path) == 2
    assert "no YAML files" in capsys.readouterr().out


def test_missing_schema_is_rejected(tmp_path, capsys):
    shutil.copyfile(TOOL / "examples/bindless_basic.yaml", tmp_path / "valid.yaml")
    assert (
        validator.validate_examples(tmp_path, schema_path=tmp_path / "missing.json")
        == 2
    )
    assert "schema not found" in capsys.readouterr().out
