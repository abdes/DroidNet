# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

import json
from pathlib import Path

import pytest

from bindless_codegen import generator

from spec_fixtures import (
    copy_document,
    create_full_example_document,
    create_minimal_valid_document,
)


def write_schema(path, schema_obj):
    path.write_text(json.dumps(schema_obj), encoding="utf-8")


class TestValidationWithActualSchema:
    @pytest.fixture
    def actual_schema_path(self):
        resolved = generator.find_schema(None, generator.__file__)
        if resolved is not None:
            return Path(resolved)
        return (
            Path(__file__).resolve().parents[3]
            / "Meta"
            / "Bindless.schema.json"
        )

    def test_validate_with_actual_schema_success(self, actual_schema_path):
        if not actual_schema_path.exists():
            pytest.skip("Actual schema file not found")
        doc = create_minimal_valid_document()
        assert generator.validate_input(doc, actual_schema_path) is True

    def test_validate_with_actual_schema_full_example(self, actual_schema_path):
        if not actual_schema_path.exists():
            pytest.skip("Actual schema file not found")
        doc = create_full_example_document()
        assert generator.validate_input(doc, actual_schema_path) is True

    def test_validate_missing_required_fields(self, actual_schema_path):
        if not actual_schema_path.exists():
            pytest.skip("Actual schema file not found")

        doc = {
            "defaults": {"invalid_index": 4294967295},
            "abi": {"domains": []},
            "backends": {},
        }
        with pytest.raises(ValueError, match="validation failed.*meta"):
            generator.validate_input(doc, actual_schema_path)

        doc = {
            "meta": {"version": "2.0.0"},
            "abi": {"domains": []},
            "backends": {},
        }
        with pytest.raises(ValueError, match="validation failed.*defaults"):
            generator.validate_input(doc, actual_schema_path)

        doc = {
            "meta": {"version": "2.0.0"},
            "defaults": {"invalid_index": 4294967295},
            "backends": {},
        }
        with pytest.raises(ValueError, match="validation failed.*abi"):
            generator.validate_input(doc, actual_schema_path)

        doc = {
            "meta": {"version": "2.0.0"},
            "defaults": {"invalid_index": 4294967295},
            "abi": {"domains": []},
        }
        with pytest.raises(ValueError, match="validation failed.*backends"):
            generator.validate_input(doc, actual_schema_path)

    def test_validate_invalid_abi_domain_fields(self, actual_schema_path):
        if not actual_schema_path.exists():
            pytest.skip("Actual schema file not found")

        base_doc = create_minimal_valid_document()

        doc = copy_document(base_doc)
        doc["abi"]["domains"][0]["shader_access_class"] = "invalid_access"
        with pytest.raises(ValueError, match="validation failed"):
            generator.validate_input(doc, actual_schema_path)

        doc = copy_document(base_doc)
        del doc["abi"]["domains"][0]["shader_index_base"]
        with pytest.raises(ValueError, match="validation failed"):
            generator.validate_input(doc, actual_schema_path)

        doc = copy_document(base_doc)
        doc["abi"]["domains"][0]["id"] = "123invalid"
        with pytest.raises(ValueError, match="validation failed"):
            generator.validate_input(doc, actual_schema_path)

        doc = copy_document(base_doc)
        doc["abi"]["domains"][0]["capacity"] = -1
        with pytest.raises(ValueError, match="validation failed"):
            generator.validate_input(doc, actual_schema_path)

    def test_validate_invalid_backend_fields(self, actual_schema_path):
        if not actual_schema_path.exists():
            pytest.skip("Actual schema file not found")

        doc = create_full_example_document()
        doc["backends"]["d3d12"]["strategy"]["tables"][0]["unknown_property"] = 1
        with pytest.raises(ValueError, match="validation failed"):
            generator.validate_input(doc, actual_schema_path)


class TestValidationEdgeCases:
    def test_validate_no_schema_file(self, tmp_path):
        nonexistent_schema = tmp_path / "nonexistent.json"
        doc = {"any": "data"}
        assert generator.validate_input(doc, nonexistent_schema) is True

    def test_validate_default_schema_path(self, monkeypatch, tmp_path):
        src_dir = tmp_path / "src" / "bindless_codegen"
        src_dir.mkdir(parents=True)

        mock_generator_file = src_dir / "generator.py"
        mock_generator_file.write_text("# mock file")

        schema_path = tmp_path / "src" / "Spec.schema.json"
        schema = {
            "type": "object",
            "required": ["test_field"],
            "properties": {"test_field": {"type": "string"}},
        }
        write_schema(schema_path, schema)

        monkeypatch.setattr(generator, "__file__", str(mock_generator_file))
        doc = {"test_field": "value"}
        assert generator.validate_input(doc) is True

    def test_validate_corrupted_schema_file(self, tmp_path):
        schema_path = tmp_path / "corrupt.json"
        schema_path.write_text("{ invalid json", encoding="utf-8")
        with pytest.raises(
            RuntimeError, match="Failed to load/validate schema"
        ):
            generator.validate_input({"any": "data"}, schema_path)

    def test_validate_missing_jsonschema_module(self, monkeypatch, tmp_path):
        monkeypatch.setattr(generator.schema_mod, "jsonschema", None)

        schema_path = tmp_path / "test.json"
        write_schema(schema_path, {"type": "object"})
        with pytest.raises(
            RuntimeError, match="jsonschema.*package is required"
        ):
            generator.validate_input({"any": "data"}, schema_path)


class TestValidationWithCustomSchemas:
    def test_validate_success_minimal(self, tmp_path):
        schema = {
            "type": "object",
            "required": ["abi"],
            "properties": {"abi": {"type": "object"}},
        }
        schema_path = tmp_path / "minimal.json"
        write_schema(schema_path, schema)
        assert generator.validate_input({"abi": {}}, schema_path) is True

    def test_validate_failure_type_mismatch(self, tmp_path):
        schema = {
            "type": "object",
            "required": ["number_field"],
            "properties": {"number_field": {"type": "integer"}},
        }
        schema_path = tmp_path / "types.json"
        write_schema(schema_path, schema)

        with pytest.raises(ValueError, match="validation failed"):
            generator.validate_input({"number_field": "not_a_number"}, schema_path)

    def test_validate_error_message_formatting(self, tmp_path):
        schema = {
            "type": "object",
            "required": ["nested"],
            "properties": {
                "nested": {"type": "object", "required": ["required_field"]}
            },
        }
        schema_path = tmp_path / "nested.json"
        write_schema(schema_path, schema)

        with pytest.raises(ValueError) as exc_info:
            generator.validate_input({"nested": {}}, schema_path)

        error_msg = str(exc_info.value)
        assert "validation failed" in error_msg
        assert "nested" in error_msg
