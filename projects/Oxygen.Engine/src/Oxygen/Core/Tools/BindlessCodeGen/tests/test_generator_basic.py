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
        binding_slots_version: 1
        defaults:
          invalid_index: 4294967295
        domains:
          - id: test
            name: Test
            kind: srv
            domain_base: 1
            capacity: 10
    """
        )
    )

    out_cpp = tmp_path / "BindingSlots.h"
    out_hlsl = tmp_path / "BindingSlots.hlsl"
    changed = generate(str(yaml), str(out_cpp), str(out_hlsl))
    assert out_cpp.exists()
    assert out_hlsl.exists()
    assert changed
