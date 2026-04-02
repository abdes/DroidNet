# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

"""Tests for the CLI dry-run functionality."""

import yaml

from bindless_codegen import cli, generator

from spec_fixtures import create_minimal_valid_document


def create_test_yaml(path, content=None):
    if content is None:
        content = create_minimal_valid_document()
    with open(path, "w", encoding="utf-8") as stream:
        yaml.safe_dump(content, stream, sort_keys=False)


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
    assert "C++ ABI header:" in captured.err
    assert "HLSL ABI header:" in captured.err
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
    assert "#ifndef OXYGEN_BINDLESS_ABI_HLSL" in hlsl_content
    assert "kTestDomainShaderIndexBase" in cpp_content
    assert "K_TEST_DOMAIN_SHADER_INDEX_BASE" in hlsl_content


def test_dry_run_with_invalid_input(tmp_path):
    input_yaml = tmp_path / "invalid.yaml"
    create_test_yaml(input_yaml, {"defaults": {"invalid_index": 4294967295}})

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
