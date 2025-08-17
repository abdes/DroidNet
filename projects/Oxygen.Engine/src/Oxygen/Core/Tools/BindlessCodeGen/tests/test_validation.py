# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

import json
from pathlib import Path
import pytest

from bindless_codegen import generator


def write_schema(path, schema_obj):
    path.write_text(json.dumps(schema_obj), encoding="utf-8")


def create_minimal_valid_document():
    """Create a minimal valid BindingSlots document."""
    return {
        "meta": {"version": "1.0.0"},
        "defaults": {"invalid_index": 4294967295},
        "domains": [
            {
                "id": "test_domain",
                "name": "TestDomain",
                "kind": "SRV",
                "register": "t0",
                "space": "space0",
                "root_table": "Table0",
                "domain_base": 0,
                "capacity": 100,
            }
        ],
        "root_signature": [
            {
                "type": "descriptor_table",
                "name": "Table0",
                "index": 0,
                "visibility": "ALL",
                "ranges": [
                    {
                        "range_type": "SRV",
                        "domain": ["test_domain"],
                        "base_shader_register": "t0",
                        "register_space": "space0",
                        "num_descriptors": 100,
                    }
                ],
            }
        ],
    }


def create_full_example_document():
    """Create a full example document with all optional fields."""
    return {
        "meta": {
            "version": "1.0.0",
            "description": "Test bindless slot mapping",
        },
        "defaults": {"invalid_index": 4294967295},
        "domains": [
            {
                "id": "textures",
                "name": "Textures",
                "kind": "SRV",
                "register": "t0",
                "space": "space0",
                "root_table": "GfxTable",
                "heap_index": 1,
                "domain_base": 1000,
                "capacity": 4096,
                "comment": "Texture SRV array",
            },
            {
                "id": "samplers",
                "name": "Samplers",
                "kind": "SAMPLER",
                "register": "s0",
                "space": "space0",
                "root_table": "GfxTable",
                "domain_base": 0,
                "capacity": 256,
                "comment": "Sampler array",
            },
        ],
        "symbols": {
            "kInvalidBindlessIndex": {
                "value": "invalid_index",
                "comment": "invalid sentinel",
            },
            "TextureBaseSlot": {
                "domain": "textures",
                "comment": "Base texture slot",
            },
        },
        "root_signature": [
            {
                "type": "descriptor_table",
                "name": "GfxTable",
                "index": 0,
                "visibility": ["ALL"],
                "ranges": [
                    {
                        "range_type": "SRV",
                        "domain": ["textures"],
                        "base_shader_register": "t0",
                        "register_space": "space0",
                        "num_descriptors": 4096,
                    },
                    {
                        "range_type": "SAMPLER",
                        "domain": ["samplers"],
                        "base_shader_register": "s0",
                        "register_space": "space0",
                        "num_descriptors": 256,
                    },
                ],
            }
        ],
    }


class TestValidationWithActualSchema:
    """Test validation using the actual Spec.schema.json file."""

    @pytest.fixture
    def actual_schema_path(self):
        """Get path to the actual schema file."""
        # Schema is located at src/Oxygen/Core/Bindless/Spec.schema.json
        return (
            Path(__file__).resolve().parents[3]
            / "Bindless"
            / "Spec.schema.json"
        )

    def test_validate_with_actual_schema_success(self, actual_schema_path):
        """Test validation success with actual schema and minimal valid document."""
        if not actual_schema_path.exists():
            pytest.skip("Actual schema file not found")

        doc = create_minimal_valid_document()
        assert generator.validate_input(doc, actual_schema_path) is True

    def test_validate_with_actual_schema_full_example(self, actual_schema_path):
        """Test validation success with actual schema and full example document."""
        if not actual_schema_path.exists():
            pytest.skip("Actual schema file not found")

        doc = create_full_example_document()
        assert generator.validate_input(doc, actual_schema_path) is True

    def test_validate_missing_required_fields(self, actual_schema_path):
        """Test validation failure when required fields are missing."""
        if not actual_schema_path.exists():
            pytest.skip("Actual schema file not found")

        # Missing meta
        doc = {
            "defaults": {"invalid_index": 4294967295},
            "domains": [],
            "root_signature": [],
        }
        with pytest.raises(ValueError, match="validation failed.*meta"):
            generator.validate_input(doc, actual_schema_path)

        # Missing defaults
        doc = {
            "meta": {"version": "1.0.0"},
            "domains": [],
            "root_signature": [],
        }
        with pytest.raises(ValueError, match="validation failed.*defaults"):
            generator.validate_input(doc, actual_schema_path)

        # Missing domains
        doc = {
            "meta": {"version": "1.0.0"},
            "defaults": {"invalid_index": 4294967295},
            "root_signature": [],
        }
        with pytest.raises(ValueError, match="validation failed.*domains"):
            generator.validate_input(doc, actual_schema_path)
        # Missing root_signature
        doc = {
            "meta": {"version": "1.0.0"},
            "defaults": {"invalid_index": 4294967295},
            "domains": [],
        }
        with pytest.raises(
            ValueError, match="validation failed.*root_signature"
        ):
            generator.validate_input(doc, actual_schema_path)

    def test_validate_invalid_domain_fields(self, actual_schema_path):
        """Test validation failure for invalid domain fields."""
        if not actual_schema_path.exists():
            pytest.skip("Actual schema file not found")

        base_doc = create_minimal_valid_document()

        # Invalid kind enum value
        doc = base_doc.copy()
        doc["domains"][0]["kind"] = "invalid_kind"
        with pytest.raises(ValueError, match="validation failed"):
            generator.validate_input(doc, actual_schema_path)

        # Missing required domain field
        doc = base_doc.copy()
        del doc["domains"][0]["domain_base"]
        with pytest.raises(ValueError, match="validation failed"):
            generator.validate_input(doc, actual_schema_path)

        # Invalid id pattern
        doc = base_doc.copy()
        doc["domains"][0]["id"] = "123invalid"  # Can't start with number
        with pytest.raises(ValueError, match="validation failed"):
            generator.validate_input(doc, actual_schema_path)

        # Negative capacity
        doc = base_doc.copy()
        doc["domains"][0]["capacity"] = -1
        with pytest.raises(ValueError, match="validation failed"):
            generator.validate_input(doc, actual_schema_path)

    def test_validate_invalid_symbols(self, actual_schema_path):
        """Test validation failure for invalid symbols."""
        if not actual_schema_path.exists():
            pytest.skip("Actual schema file not found")

        doc = create_full_example_document()

        # Add invalid symbol with unknown property
        doc["symbols"]["InvalidSymbol"] = {"unknown_property": "value"}
        with pytest.raises(ValueError, match="validation failed"):
            generator.validate_input(doc, actual_schema_path)


class TestValidationEdgeCases:
    """Test edge cases and error conditions."""

    def test_validate_no_schema_file(self, tmp_path):
        """Test validation when schema file doesn't exist (should pass silently)."""
        nonexistent_schema = tmp_path / "nonexistent.json"
        doc = {"any": "data"}

        # Should return True when schema doesn't exist
        assert generator.validate_input(doc, nonexistent_schema) is True

    def test_validate_default_schema_path(self, monkeypatch, tmp_path):
        """Test validation with default schema path resolution."""
        # Create a temporary module structure to mock the path resolution
        src_dir = tmp_path / "src" / "bindless_codegen"
        src_dir.mkdir(parents=True)

        # Create a mock generator module in the temp location
        mock_generator_file = src_dir / "generator.py"
        mock_generator_file.write_text("# mock file")

        # Create schema at one of the expected locations relative to generator.py (parent of src)
        schema_path = tmp_path / "src" / "Spec.schema.json"
        schema = {
            "type": "object",
            "required": ["test_field"],
            "properties": {"test_field": {"type": "string"}},
        }
        write_schema(schema_path, schema)

        # Monkey patch __file__ in the generator module
        monkeypatch.setattr(generator, "__file__", str(mock_generator_file))

        doc = {"test_field": "value"}
        # Should work when schema exists at default location
        assert generator.validate_input(doc) is True

    def test_validate_corrupted_schema_file(self, tmp_path):
        """Test validation with corrupted schema file."""
        schema_path = tmp_path / "corrupt.json"
        schema_path.write_text("{ invalid json", encoding="utf-8")

        doc = {"any": "data"}
        with pytest.raises(
            RuntimeError, match="Failed to load/validate schema"
        ):
            generator.validate_input(doc, schema_path)

    def test_validate_missing_jsonschema_module(self, monkeypatch, tmp_path):
        """Test validation when jsonschema module is not available."""
        # Mock jsonschema as None in the schema module used by generator
        monkeypatch.setattr(generator.schema_mod, "jsonschema", None)

        schema_path = tmp_path / "test.json"
        write_schema(schema_path, {"type": "object"})

        doc = {"any": "data"}
        with pytest.raises(
            RuntimeError, match="jsonschema.*package is required"
        ):
            generator.validate_input(doc, schema_path)


class TestValidationWithCustomSchemas:
    """Test validation with custom test schemas for specific scenarios."""

    def test_validate_success_minimal(self, tmp_path):
        """Test validation success with minimal custom schema."""
        schema = {
            "type": "object",
            "required": ["domains"],
            "properties": {"domains": {"type": "array"}},
        }
        schema_path = tmp_path / "minimal.json"
        write_schema(schema_path, schema)

        doc = {"domains": []}
        assert generator.validate_input(doc, schema_path) is True

    def test_validate_failure_type_mismatch(self, tmp_path):
        """Test validation failure with type mismatch."""
        schema = {
            "type": "object",
            "required": ["number_field"],
            "properties": {"number_field": {"type": "integer"}},
        }
        schema_path = tmp_path / "types.json"
        write_schema(schema_path, schema)

        # String instead of integer
        doc = {"number_field": "not_a_number"}
        with pytest.raises(ValueError, match="validation failed"):
            generator.validate_input(doc, schema_path)

    def test_validate_error_message_formatting(self, tmp_path):
        """Test that validation error messages are properly formatted."""
        schema = {
            "type": "object",
            "required": ["nested"],
            "properties": {
                "nested": {"type": "object", "required": ["required_field"]}
            },
        }
        schema_path = tmp_path / "nested.json"
        write_schema(schema_path, schema)

        # Missing nested.required_field
        doc = {"nested": {}}
        with pytest.raises(ValueError) as exc_info:
            generator.validate_input(doc, schema_path)

        error_msg = str(exc_info.value)
        assert "validation failed" in error_msg
        assert "nested" in error_msg
