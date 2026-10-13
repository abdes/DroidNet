"""Generated output must remain stable across Windows builds and commit hooks."""

import json
import re
import shutil

import pytest

from bindless_codegen._copy_helper import copy_single
from bindless_codegen.formatting import append_trailing_commas, check_literals
from bindless_codegen.generator import _format_json, generate
from test_ts_strategy import _resolve_spec_yaml


def test_generation_is_byte_stable_and_lf_only(tmp_path, monkeypatch):
    prefix = str(tmp_path / "Generated.")
    monkeypatch.setattr(
        "bindless_codegen.generator.time.strftime", lambda *_: "2026-01-01 00:00:00"
    )
    assert generate(str(_resolve_spec_yaml()), None, None, out_base=prefix)
    before = {
        path: (path.read_bytes(), path.stat().st_mtime_ns)
        for path in tmp_path.iterdir()
    }
    assert all(
        b"\r" not in data and data.endswith(b"\n") for data, _ in before.values()
    )
    monkeypatch.setattr(
        "bindless_codegen.generator.time.strftime", lambda *_: "2026-01-02 00:00:00"
    )
    assert not generate(str(_resolve_spec_yaml()), None, None, out_base=prefix)
    assert before == {
        path: (path.read_bytes(), path.stat().st_mtime_ns)
        for path in tmp_path.iterdir()
    }


def test_scalar_arrays_are_compact_without_changing_string_contents():
    value = {"views": ["StructuredBuffer_SRV", "RawBuffer_SRV"], "comment": "[1,  2]"}
    output = _format_json(value)
    assert '"views": ["StructuredBuffer_SRV", "RawBuffer_SRV"]' in output
    assert json.loads(output) == value


def test_metadata_source_path_round_trips_outside_git(tmp_path):
    # Conan source archives have no Git root. Windows paths must remain valid
    # string literals, including the cache's backslash followed by 'oxygen'.
    folder = tmp_path / "oxygen cache"
    folder.mkdir()
    source = folder / "Bindless.yaml"
    shutil.copyfile(_resolve_spec_yaml(), source)
    assert generate(str(source), None, None, out_base=str(folder / "Generated."))
    header = (folder / "Generated.Meta.h").read_text(encoding="utf-8")
    # clang-format splits a long literal into adjacent ones.
    declaration = re.search(r"kBindlessSourcePath\s*=\s*([^;]*);", header)
    assert declaration is not None
    literals = re.findall(r'"(?:[^"\\]|\\.)*"', declaration.group(1))
    assert "".join(json.loads(literal) for literal in literals) == str(
        source.absolute()
    )


def test_trailing_comma_is_appended_to_multiline_braced_lists_only():
    source = (
        "std::array<float, 4> a { 1.0F, 2.0F,\n  3.0F, 4.0F };\n"
        "std::array<float, 4> b { 1.0F, 2.0F, 3.0F, 4.0F };\n"
        "Pair c { first,\n  second\n};\n"
        "Table t = { {\n  Entry { .x = 1U },\n} };\n"
        'const char j[] = R"OXJ(\n{ "k": [1,\n 2] }\n)OXJ";\n'
        "struct S {\n  int x;\n};\n"
        "void f()\n{\n  if (x) {\n    y();\n  } else {\n    z();\n  }\n}\n"
        "enum class E : int {\n  kA = 0\n};\n"
        "enum class F : int { kA = 0, kB = 1 };\n"
    )
    expected = (
        "std::array<float, 4> a { 1.0F, 2.0F,\n  3.0F, 4.0F, };\n"
        "std::array<float, 4> b { 1.0F, 2.0F, 3.0F, 4.0F };\n"
        "Pair c { first,\n  second,\n};\n"
        "Table t = { {\n  Entry { .x = 1U },\n}, };\n"
        'const char j[] = R"OXJ(\n{ "k": [1,\n 2] }\n)OXJ";\n'
        "struct S {\n  int x;\n};\n"
        "void f()\n{\n  if (x) {\n    y();\n  } else {\n    z();\n  }\n}\n"
        "enum class E : int {\n  kA = 0,\n};\n"
        "enum class F : int { kA = 0, kB = 1 };\n"
    )
    assert append_trailing_commas(source) == expected
    assert append_trailing_commas(expected) == expected


def test_literals_must_use_uppercase_suffixes_and_lowercase_prefixes():
    check_literals("auto a = 0xFFFFU; auto b = 1.5F; auto c = 5.8e-3F;", "ok.h")
    check_literals('auto d = "0xFFFFu"; // 1u\n', "text.h")
    for source in (
        "auto a = 0xFFFFu;",
        "auto a = 1u;",
        "auto a = 1.0f;",
        "auto a = 0XFF;",
    ):
        with pytest.raises(ValueError):
            check_literals(source, "bad.h")


def test_copy_repairs_crlf_in_existing_output(tmp_path):
    source, target = tmp_path / "source.h", tmp_path / "target.h"
    source.write_bytes(b"// Generated: 2026-01-01 00:00:00\nint value;\n")
    target.write_bytes(source.read_bytes().replace(b"\n", b"\r\n"))
    assert copy_single(source, target, False) == 0
    assert target.read_bytes() == source.read_bytes()
