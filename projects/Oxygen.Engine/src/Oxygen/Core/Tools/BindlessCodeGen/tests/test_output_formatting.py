"""Generated output must remain stable across Windows builds and commit hooks."""

import json
import re
import shutil

from bindless_codegen._copy_helper import copy_single
from bindless_codegen.generator import _format_json, generate
from test_ts_strategy import _resolve_spec_yaml


def test_generation_is_byte_stable_and_lf_only(tmp_path, monkeypatch):
    prefix = str(tmp_path / "Generated.")
    monkeypatch.setattr("bindless_codegen.generator.time.strftime", lambda *_: "2026-01-01 00:00:00")
    assert generate(str(_resolve_spec_yaml()), None, None, out_base=prefix)
    before = {path: (path.read_bytes(), path.stat().st_mtime_ns) for path in tmp_path.iterdir()}
    assert all(b"\r" not in data and data.endswith(b"\n") for data, _ in before.values())
    monkeypatch.setattr("bindless_codegen.generator.time.strftime", lambda *_: "2026-01-02 00:00:00")
    assert not generate(str(_resolve_spec_yaml()), None, None, out_base=prefix)
    assert before == {path: (path.read_bytes(), path.stat().st_mtime_ns) for path in tmp_path.iterdir()}


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
    literal = re.search(r"kBindlessSourcePath\[\] = (.*);", header).group(1)
    assert json.loads(literal) == str(source.absolute())


def test_copy_repairs_crlf_in_existing_output(tmp_path):
    source, target = tmp_path / "source.h", tmp_path / "target.h"
    source.write_bytes(b"// Generated: 2026-01-01 00:00:00\nint value;\n")
    target.write_bytes(source.read_bytes().replace(b"\n", b"\r\n"))
    assert copy_single(source, target, False) == 0
    assert target.read_bytes() == source.read_bytes()
