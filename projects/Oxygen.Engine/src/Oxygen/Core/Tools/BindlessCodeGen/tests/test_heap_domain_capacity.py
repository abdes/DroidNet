# ===-----------------------------------------------------------------------===
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===

import pytest
import yaml

from bindless_codegen import generator

from spec_fixtures import create_full_example_document


def _write(tmp_path, doc):
    path = tmp_path / "spec.yaml"
    path.write_text(yaml.safe_dump(doc, sort_keys=False))
    return path


def test_domain_realization_exceeds_heap_capacity(tmp_path):
    doc = create_full_example_document()
    doc["backends"]["d3d12"]["strategy"]["heaps"][0]["capacity"] = 6000
    doc["backends"]["d3d12"]["strategy"]["domain_realizations"][1][
        "heap_local_base"
    ] = 5000

    path = _write(tmp_path, doc)
    with pytest.raises(ValueError, match="exceeds heap"):
        generator.generate(str(path), "out.cpp", "out.hlsl", dry_run=True)


def test_domain_realization_exceeds_fixed_table_descriptor_count(tmp_path):
    doc = create_full_example_document()
    doc["backends"]["d3d12"]["strategy"]["tables"][1]["descriptor_count"] = 128

    path = _write(tmp_path, doc)
    with pytest.raises(ValueError, match="exceeds table .* descriptor_count"):
        generator.generate(str(path), "out.cpp", "out.hlsl", dry_run=True)


def test_heap_local_overlap_between_domains(tmp_path):
    doc = create_full_example_document()
    doc["backends"]["d3d12"]["strategy"]["domain_realizations"][1][
        "heap_local_base"
    ] = 1000

    path = _write(tmp_path, doc)
    with pytest.raises(ValueError, match="heap-local realization overlap"):
        generator.generate(str(path), "out.cpp", "out.hlsl", dry_run=True)
