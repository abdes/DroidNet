# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

from bindless_codegen.generator import generate
import textwrap


def test_generate_tmp(tmp_path):
    yaml = tmp_path / "slots.yaml"
    yaml.write_text(
        textwrap.dedent(
            """\
        meta:
          version: "2.0.0"
        defaults:
          invalid_index: 4294967295
        abi:
          index_spaces:
            - id: srv_uav_cbv
          domains:
            - id: test
              name: TestDomain
              index_space: srv_uav_cbv
              shader_index_base: 1
              capacity: 10
              shader_access_class: buffer_srv
              view_types: [StructuredBuffer_SRV]
        backends:
          d3d12:
            strategy:
              heaps:
                - id: "CBV_SRV_UAV:gpu"
                  type: CBV_SRV_UAV
                  shader_visible: true
                  capacity: 128
                  base_index: 0
                  allow_growth: false
              tables:
                - id: Table0
                  descriptor_kind: SRV
                  heap: "CBV_SRV_UAV:gpu"
                  shader_register: t0
                  register_space: space0
                  descriptor_count: 64
              domain_realizations:
                - domain: test
                  table: Table0
                  heap_local_base: 1
            root_signature:
              - type: descriptor_table
                id: Table0
                table: Table0
                index: 0
                visibility: ALL
          vulkan:
            strategy:
              descriptor_sets:
                - id: bindless_main
                  set: 0
              bindings:
                - id: buffers_binding
                  set: bindless_main
                  binding: 0
                  descriptor_type: STORAGE_BUFFER
                  descriptor_count: 64
              domain_realizations:
                - domain: test
                  binding: buffers_binding
                  array_element_base: 1
            pipeline_layout:
              - type: descriptor_set
                id: BindlessMain
                set_ref: bindless_main
    """
        )
    )

    out_cpp = tmp_path / "BindingSlots.h"
    out_hlsl = tmp_path / "BindingSlots.hlsl"
    changed = generate(str(yaml), str(out_cpp), str(out_hlsl))
    assert out_cpp.exists()
    assert out_hlsl.exists()
    assert changed
