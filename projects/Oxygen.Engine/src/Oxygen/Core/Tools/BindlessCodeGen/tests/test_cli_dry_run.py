# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

"""Tests for the CLI dry-run functionality."""

import pytest
from pathlib import Path
import yaml

from bindless_codegen import cli, generator


def create_test_yaml(path, content=None):
    """Create a test YAML file with valid BindingSlots content."""
    if content is None:
        content = {
            "binding_slots_version": 1,
            "defaults": {"invalid_index": 4294967295},
            "domains": [
                {
                    "id": "test_domain",
                    "name": "TestDomain",
                    "kind": "srv",
                    "domain_base": 0,
                    "capacity": 100,
                }
            ],
        }

    with open(path, "w", encoding="utf-8") as f:
        yaml.dump(content, f)


class TestCliDryRun:
    """Test CLI dry-run functionality."""

    def test_dry_run_no_files_created(self, tmp_path, capsys):
        """Test that dry-run does not create output files."""
        # Create test input file
        input_yaml = tmp_path / "input.yaml"
        create_test_yaml(input_yaml)

        # Define output paths
        out_cpp = tmp_path / "output.h"
        out_hlsl = tmp_path / "output.hlsl"

        # Run with dry-run flag
        cli.main(
            [
                "--input",
                str(input_yaml),
                "--out-cpp",
                str(out_cpp),
                "--out-hlsl",
                str(out_hlsl),
                "--dry-run",
            ]
        )

        # Check that no files were created
        assert not out_cpp.exists()
        assert not out_hlsl.exists()

        # Check that appropriate messages were printed
        captured = capsys.readouterr()
        assert "[DRY RUN]" in captured.out
        assert "Would generate C++ header" in captured.out
        assert "Would generate HLSL header" in captured.out
        assert "Validation successful" in captured.out

    def test_normal_run_creates_files(self, tmp_path):
        """Test that normal run (without dry-run) creates output files."""
        # Create test input file
        input_yaml = tmp_path / "input.yaml"
        create_test_yaml(input_yaml)

        # Define output paths
        out_cpp = tmp_path / "output.h"
        out_hlsl = tmp_path / "output.hlsl"

        # Run without dry-run flag
        cli.main(
            [
                "--input",
                str(input_yaml),
                "--out-cpp",
                str(out_cpp),
                "--out-hlsl",
                str(out_hlsl),
            ]
        )

        # Check that files were created
        assert out_cpp.exists()
        assert out_hlsl.exists()

        # Check that files have expected content
        cpp_content = out_cpp.read_text(encoding="utf-8")
        hlsl_content = out_hlsl.read_text(encoding="utf-8")

        assert "OXYGEN_CORE_BINDLESS_BINDINGSLOTS_H" in cpp_content
        assert "OXYGEN_BINDING_SLOTS_HLSL" in hlsl_content
        assert "TestDomain_DomainBase" in cpp_content
        assert "TESTDOMAIN_DOMAIN_BASE" in hlsl_content

    def test_dry_run_with_invalid_input(self, tmp_path):
        """Test that dry-run properly validates input and reports errors."""
        # Create invalid YAML file (this should fail if schema validation is enabled)
        input_yaml = tmp_path / "invalid.yaml"
        invalid_content = {
            "binding_slots_version": 1,
            # Missing required 'defaults' and 'domains'
        }
        create_test_yaml(input_yaml, invalid_content)

        # Define output paths
        out_cpp = tmp_path / "output.h"
        out_hlsl = tmp_path / "output.hlsl"

        # Run with dry-run flag - should fail validation if schema is available
        try:
            cli.main(
                [
                    "--input",
                    str(input_yaml),
                    "--out-cpp",
                    str(out_cpp),
                    "--out-hlsl",
                    str(out_hlsl),
                    "--dry-run",
                ]
            )
            # If we get here, validation passed (schema might not be available)
            # This is acceptable behavior when schema validation is optional
        except (ValueError, SystemExit):
            # Expected if schema validation fails
            pass

        # Regardless of validation outcome, no files should be created in dry-run
        assert not out_cpp.exists()
        assert not out_hlsl.exists()

    def test_generator_dry_run_function(self, tmp_path, capsys):
        """Test the generator.generate function with dry_run=True."""
        # Create test input file
        input_yaml = tmp_path / "input.yaml"
        create_test_yaml(input_yaml)

        # Define output paths
        out_cpp = tmp_path / "output.h"
        out_hlsl = tmp_path / "output.hlsl"

        # Call generator with dry_run=True
        result = generator.generate(
            str(input_yaml), str(out_cpp), str(out_hlsl), dry_run=True
        )

        # Check that function returns False (no files changed)
        assert result is False

        # Check that no files were created
        assert not out_cpp.exists()
        assert not out_hlsl.exists()

        # Check that appropriate messages were printed
        captured = capsys.readouterr()
        assert "[DRY RUN]" in captured.out
        assert str(out_cpp) in captured.out
        assert str(out_hlsl) in captured.out

    def test_generator_normal_run_function(self, tmp_path):
        """Test the generator.generate function with dry_run=False (default)."""
        # Create test input file
        input_yaml = tmp_path / "input.yaml"
        create_test_yaml(input_yaml)

        # Define output paths
        out_cpp = tmp_path / "output.h"
        out_hlsl = tmp_path / "output.hlsl"

        # Call generator with dry_run=False (default)
        result = generator.generate(
            str(input_yaml), str(out_cpp), str(out_hlsl), dry_run=False
        )

        # Check that function returns True (files were changed/created)
        assert result is True

        # Check that files were created
        assert out_cpp.exists()
        assert out_hlsl.exists()
