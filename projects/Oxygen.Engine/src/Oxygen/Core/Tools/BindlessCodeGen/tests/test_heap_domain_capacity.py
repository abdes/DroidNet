# ===-----------------------------------------------------------------------===
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===

import textwrap
import pytest
from bindless_codegen import generator


def _write(tmp_path, txt: str):
    p = tmp_path / "spec.yaml"
    p.write_text(textwrap.dedent(txt))
    return p


def _minimal_rt(domains_block: str, heaps_block: str, mappings_block: str):
    return f"""
meta:
  version: "1.0.0"
defaults:
  invalid_index: 4294967295
{domains_block}
root_signature:
  - type: descriptor_table
    name: T
    index: 0
    visibility: ALL
    ranges:
      - range_type: SRV
        domain: [d0, d1]
        base_shader_register: t0
        register_space: space0
        num_descriptors: 999999
{heaps_block}
{mappings_block}
"""


def test_domain_exceeds_heap_capacity(tmp_path):
    domains = """
domains:
  - id: d0
    name: D0
    kind: SRV
    register: t0
    space: space0
    root_table: T
    domain_base: 0
    capacity: 80
  - id: d1
    name: D1
    kind: SRV
    register: t1
    space: space0
    root_table: T
    domain_base: 80
    capacity: 40
"""
    heaps = """
heaps:
  - id: "CBV_SRV_UAV:gpu"
    type: CBV_SRV_UAV
    shader_visible: true
    capacity: 100
    base_index: 0
    allow_growth: false
"""
    mappings = """
mappings:
  - domain: d0
    heap: "CBV_SRV_UAV:gpu"
    local_base: 0
  - domain: d1
    heap: "CBV_SRV_UAV:gpu"
    local_base: 70  # 70+40 = 110 > 100, should fail
"""
    p = _write(tmp_path, _minimal_rt(domains, heaps, mappings))
    with pytest.raises(ValueError, match="exceeds heap.*capacity"):
        generator.generate(str(p), "out.cpp", "out.hlsl", dry_run=True)


def test_sum_of_domains_exceeds_heap_capacity(tmp_path):
    domains = """
domains:
  - id: d0
    name: D0
    kind: SRV
    register: t0
    space: space0
    root_table: T
    domain_base: 0
    capacity: 60
  - id: d1
    name: D1
    kind: SRV
    register: t1
    space: space0
    root_table: T
    domain_base: 60
    capacity: 50
"""
    heaps = """
heaps:
  - id: "CBV_SRV_UAV:gpu"
    type: CBV_SRV_UAV
    shader_visible: true
    capacity: 100
    base_index: 0
    allow_growth: false
"""
    mappings = """
mappings:
  - domain: d0
    heap: "CBV_SRV_UAV:gpu"
    local_base: 0
  - domain: d1
    heap: "CBV_SRV_UAV:gpu"
    local_base: 50  # fits individually [50,100) but 60+50 > 100, should fail on sum
"""
    p = _write(tmp_path, _minimal_rt(domains, heaps, mappings))
    with pytest.raises(
        ValueError, match="Sum of capacities.*exceeds heap capacity"
    ):
        generator.generate(str(p), "out.cpp", "out.hlsl", dry_run=True)


def test_heap_local_overlap_between_domains(tmp_path):
    domains = """
domains:
  - id: d0
    name: D0
    kind: SRV
    register: t0
    space: space0
    root_table: T
    domain_base: 0
    capacity: 30
  - id: d1
    name: D1
    kind: SRV
    register: t1
    space: space0
    root_table: T
    domain_base: 30
    capacity: 30
"""
    heaps = """
heaps:
  - id: "CBV_SRV_UAV:gpu"
    type: CBV_SRV_UAV
    shader_visible: true
    capacity: 100
    base_index: 0
    allow_growth: false
"""
    mappings = """
mappings:
  - domain: d0
    heap: "CBV_SRV_UAV:gpu"
    local_base: 10
  - domain: d1
    heap: "CBV_SRV_UAV:gpu"
    local_base: 20  # overlaps [20,50) with [10,40)
"""
    p = _write(tmp_path, _minimal_rt(domains, heaps, mappings))
    with pytest.raises(ValueError, match="overlap"):
        generator.generate(str(p), "out.cpp", "out.hlsl", dry_run=True)
