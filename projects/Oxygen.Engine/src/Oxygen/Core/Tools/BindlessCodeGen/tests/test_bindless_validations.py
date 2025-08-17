import os
import tempfile
import json
import pytest

from bindless_codegen import generator


def write_yaml(tmpdir, content, name="Spec.yaml"):
    path = tmpdir.join(name)
    path.write(content)
    return str(path)


def test_domain_overlap_detection(tmp_path):
    yaml = """
meta:
  version: "1.0.0"
defaults:
  invalid_index: 4294967295
domains:
  - id: a
    name: A
    kind: SRV
    register: t0
    space: space0
    root_table: Table0
    domain_base: 0
    capacity: 100
  - id: b
    name: B
    kind: SRV
    register: t1
    space: space0
    root_table: Table0
    domain_base: 50
    capacity: 100
root_signature:
  - type: descriptor_table
    name: Table0
    index: 0
    visibility: ALL
    ranges:
      - range_type: SRV
        domain: [a,b]
        base_shader_register: t0
        register_space: space0
        num_descriptors: 200
"""
    p = tmp_path / "bs.yaml"
    p.write_text(yaml)
    with pytest.raises(ValueError, match="overlap"):
        generator.generate(str(p), "out.cpp", "out.hlsl", dry_run=True)


def test_uav_counter_and_unbounded_forbidden(tmp_path):
    yaml = """
meta:
  version: "1.0.0"
defaults:
  invalid_index: 4294967295
domains:
  - id: u
    name: U
    kind: UAV
    register: u0
    space: space0
    root_table: TableU
    uav_counter_register: u1
    domain_base: 0
    capacity: 10
root_signature:
  - type: descriptor_table
    name: TableU
    index: 0
    visibility: ALL
    ranges:
      - range_type: UAV
        domain: [u]
        base_shader_register: u0
        register_space: space0
        num_descriptors: unbounded
"""
    p = tmp_path / "uav.yaml"
    p.write_text(yaml)
    with pytest.raises(ValueError, match="cannot be 'unbounded'"):
        generator.generate(str(p), "out.cpp", "out.hlsl", dry_run=True)


def test_cbv_array_size_exceeds_domain(tmp_path):
    yaml = """
meta:
  version: "1.0.0"
defaults:
  invalid_index: 4294967295
domains:
  - id: cbvdom
    name: CBVDomain
    kind: CBV
    register: b1
    space: space0
    root_table: CBVTable
    domain_base: 0
    capacity: 4
root_signature:
  - type: cbv
    name: CBVTable
    index: 2
    visibility: ALL
    shader_register: b1
    register_space: space0
    cbv_array_size: 8
"""
    p = tmp_path / "cbv.yaml"
    p.write_text(yaml)
    with pytest.raises(ValueError, match="cbv_array_size .* exceeds domain"):
        generator.generate(str(p), "out.cpp", "out.hlsl", dry_run=True)


def test_heap_ranges_overlap_detection(tmp_path):
    yaml = """
meta:
  version: "1.0.0"
defaults:
  invalid_index: 4294967295
domains:
  - id: tex
    name: Tex
    kind: SRV
    register: t0
    space: space0
    root_table: T
    domain_base: 0
    capacity: 1
root_signature:
  - type: descriptor_table
    name: T
    index: 0
    visibility: ALL
    ranges:
      - range_type: SRV
        domain: [tex]
        base_shader_register: t0
        register_space: space0
        num_descriptors: 1
heaps:
  - id: "CBV_SRV_UAV:gpu"
    type: CBV_SRV_UAV
    shader_visible: true
    capacity: 100
    base_index: 1000
    allow_growth: false
  - id: "SAMPLER:gpu"
    type: SAMPLER
    shader_visible: true
    capacity: 64
    base_index: 1050  # Overlaps with previous range [1000,1100)
    allow_growth: false
"""
    p = tmp_path / "overlap.yaml"
    p.write_text(yaml)
    with pytest.raises(ValueError, match="overlap"):
        generator.generate(str(p), "out.cpp", "out.hlsl", dry_run=True)


def test_heap_ranges_with_gaps_ok(tmp_path):
    yaml = """
meta:
  version: "1.0.0"
defaults:
  invalid_index: 4294967295
domains:
  - id: tex
    name: Tex
    kind: SRV
    register: t0
    space: space0
    root_table: T
    domain_base: 0
    capacity: 1
root_signature:
  - type: descriptor_table
    name: T
    index: 0
    visibility: ALL
    ranges:
      - range_type: SRV
        domain: [tex]
        base_shader_register: t0
        register_space: space0
        num_descriptors: 1
heaps:
  - id: "CBV_SRV_UAV:gpu"
    type: CBV_SRV_UAV
    shader_visible: true
    capacity: 100
    base_index: 1000
    allow_growth: false
  - id: "SAMPLER:gpu"
    type: SAMPLER
    shader_visible: true
    capacity: 64
    base_index: 1200  # Gap after previous range [1000,1100)
    allow_growth: false
"""
    p = tmp_path / "gaps.yaml"
    p.write_text(yaml)
    # Should not raise (gaps allowed)
    generator.generate(str(p), "out.cpp", "out.hlsl", dry_run=True)
