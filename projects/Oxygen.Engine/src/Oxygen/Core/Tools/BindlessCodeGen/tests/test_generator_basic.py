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
          version: "1.0.0"
        defaults:
          invalid_index: 4294967295
        domains:
          - id: test
            name: TestDomain
            kind: SRV
            register: t0
            space: space0
            root_table: Table0
            domain_base: 1
            capacity: 10
        root_signature:
          - type: descriptor_table
            name: Table0
            index: 0
            visibility: ALL
            ranges:
              - range_type: SRV
                domain: [test]
                base_shader_register: t0
                register_space: space0
                num_descriptors: 10
    """
        )
    )

    out_cpp = tmp_path / "BindingSlots.h"
    out_hlsl = tmp_path / "BindingSlots.hlsl"
    changed = generate(str(yaml), str(out_cpp), str(out_hlsl))
    assert out_cpp.exists()
    assert out_hlsl.exists()
    assert changed
