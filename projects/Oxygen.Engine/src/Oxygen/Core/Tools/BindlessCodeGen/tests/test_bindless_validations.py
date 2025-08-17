import os
import tempfile
import json
import pytest

from bindless_codegen import generator


def write_yaml(tmpdir, content, name="BindingSlots.yaml"):
    path = tmpdir.join(name)
    path.write(content)
    return str(path)


def test_domain_overlap_detection(tmp_path):
    yaml = """
binding_slots_version: 1
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
binding_slots_version: 1
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
        domain: u
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
binding_slots_version: 1
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
