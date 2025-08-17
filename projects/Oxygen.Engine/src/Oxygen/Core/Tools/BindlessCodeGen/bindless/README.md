# BindingSlots examples

This folder contains small, illustrative `BindingSlots` YAML examples used by the
`BindlessCodeGen` generator. Each example is intentionally minimal and valid
against the stricter schema and generator validations in this branch. Use them
as a starting point for creating real binding specifications.

Files

- `basic_minimal.yaml` — Minimal CBV example with a single CBV root parameter
  (scene constants).

- `bindless_basic.yaml` — Simple bindless SRV example: one SRV domain mapped
  into a `descriptor_table` with `num_descriptors: unbounded`.

- `uav_with_counter.yaml` — UAV domain that declares `uav_counter_register` and
  a bounded descriptor range. This demonstrates the generator rule that UAV
  domains with counters cannot be referenced by `unbounded` ranges.

- `cbv_array_example_small.yaml` — CBV array parameter that maps to a domain
  and demonstrates `cbv_array_size` usage within domain capacity.

- `multi_domain_range.yaml` — Example of a descriptor_table range backed by
  multiple domains (materials + textures) where the base register aligns with
  the first domain.

Validation notes

- The schema enforces many structural constraints; the generator performs
  additional cross-document semantic checks that JSON Schema cannot express.

- Examples are valid with the current schema and should pass generator
  `dry_run` validation.

How to validate an example

1. Open a PowerShell prompt in the `BindlessCodeGen` folder:

```powershell
cd 'f:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Core/Tools/BindlessCodeGen'
pytest -q tests/test_bindless_validations.py
# or run the generator directly (python must be configured in your environment)
python -c "from bindless_codegen import generator; generator.generate('f:/projects/DroidNet/projects/Oxygen.Engine/examples/bindless_basic.yaml', 'out.cpp', 'out.hlsl', dry_run=True)"
```

If you want more examples or different patterns (per-platform registers/spaces,
different root layouts), tell me which scenario you want and I'll add it.
