# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

"""Tests for the CLI dry-run functionality."""

import yaml
from bindless_codegen import cli, generator


def create_test_yaml(path, content=None):
    """Create a test YAML file with valid BindingSlots content."""
    if content is None:
        content = {
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

    with open(path, "w", encoding="utf-8") as f:
        yaml.dump(content, f)


def test_dry_run_no_files_created(tmp_path, capsys):
    input_yaml = tmp_path / "input.yaml"
    create_test_yaml(input_yaml)

    out_cpp = tmp_path / "output.h"
    out_hlsl = tmp_path / "output.hlsl"

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

    assert not out_cpp.exists()
    assert not out_hlsl.exists()

    captured = capsys.readouterr()
    assert "[DRY RUN]" in captured.err
    assert "Planned outputs:" in captured.err
    assert "C++ header:" in captured.err
    assert "HLSL layout:" in captured.err
    assert "Validation successful" in captured.err


def test_normal_run_creates_files(tmp_path):
    input_yaml = tmp_path / "input.yaml"
    create_test_yaml(input_yaml)

    out_cpp = tmp_path / "output.h"
    out_hlsl = tmp_path / "output.hlsl"

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

    assert out_cpp.exists()
    assert out_hlsl.exists()

    cpp_content = out_cpp.read_text(encoding="utf-8")
    hlsl_content = out_hlsl.read_text(encoding="utf-8")
    assert "#pragma once" in cpp_content
    assert "#ifndef OXYGEN_BINDLESS_LAYOUT_HLSL" in hlsl_content
    assert "kTestDomainDomainBase" in cpp_content
    assert "K_TEST_DOMAIN_DOMAIN_BASE" in hlsl_content


def test_dry_run_with_invalid_input(tmp_path):
    input_yaml = tmp_path / "invalid.yaml"
    invalid_content = {"defaults": {"invalid_index": 4294967295}}
    create_test_yaml(input_yaml, invalid_content)

    out_cpp = tmp_path / "output.h"
    out_hlsl = tmp_path / "output.hlsl"

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
    except (ValueError, SystemExit):
        pass

    assert not out_cpp.exists()
    assert not out_hlsl.exists()


def test_generator_dry_run_function(tmp_path, capsys):
    input_yaml = tmp_path / "input.yaml"
    create_test_yaml(input_yaml)

    out_cpp = tmp_path / "output.h"
    out_hlsl = tmp_path / "output.hlsl"

    result = generator.generate(
        str(input_yaml), str(out_cpp), str(out_hlsl), dry_run=True
    )
    assert result is False
    assert not out_cpp.exists()
    assert not out_hlsl.exists()

    captured = capsys.readouterr()
    assert "[DRY RUN]" in captured.err
    # Paths are printed base-relative, so only the filenames appear
    assert "output.h" in captured.err
    assert "output.hlsl" in captured.err


def test_generator_normal_run_function(tmp_path):
    input_yaml = tmp_path / "input.yaml"
    create_test_yaml(input_yaml)

    out_cpp = tmp_path / "output.h"
    out_hlsl = tmp_path / "output.hlsl"

    result = generator.generate(
        str(input_yaml), str(out_cpp), str(out_hlsl), dry_run=False
    )
    assert result is True
    assert out_cpp.exists()
    assert out_hlsl.exists()
