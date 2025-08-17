# BindingSlots examples

This folder contains small, focused `BindingSlots` YAML examples for the
BindlessCodeGen generator. Each example demonstrates a specific layout or
feature of the schema and the generator's semantic checks.

## Goals

- Provide concise, copy-pasteable examples for common binding patterns
- Serve as quick unit test inputs for the generator's `dry_run` validation
- Document how to validate examples locally and in CI

## Examples included

- `basic_minimal.yaml` — Minimal CBV example with a single CBV root parameter.
- `bindless_basic.yaml` — Unbounded SRV descriptor table mapped from one domain.
- `cbv_array_example_small.yaml` — CBV array mapped to a domain (capacity
  check).
- `multi_domain_range.yaml` — Descriptor_table range backed by multiple domains.
- `uav_with_counter.yaml` — UAV domain with `uav_counter_register` and bounded
  range.
- `uav_example.yaml` — UAV example without an unbounded range (counter present).
- `cbv_array_example.yaml` — Larger CBV array example.
- `multi_space_srvs.yaml` — SRV domains placed in different register spaces.
- `root_constants_and_tables.yaml` — CBV root constants with SRV descriptor
  table.
- `sampler_table.yaml` — Dedicated sampler descriptor table example.

## Validation (local)

1. Activate your Python environment that contains the project dependencies
   (e.g., `pyyaml`, `jsonschema`).

2. From the `BindlessCodeGen` directory run the examples validator script:

```powershell
cd 'f:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Core/Tools/BindlessCodeGen'
python -m examples.run_validate_examples
```

Or run the helper directly from the examples folder:

```powershell
cd 'f:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Core/Tools/BindlessCodeGen/examples'
python run_validate_examples.py
```

This performs a `dry_run` for each YAML file and reports the first failing
example (non-zero exit). A successful run prints `All examples validated
successfully.` and exits 0.

## Validation (CI / CTest)

The examples folder is wired into the CMake tree and exposes a CTest named
`BindlessExamplesValidate` when `BUILD_TESTING` is enabled. The test runs the
`run_validate_examples.py` script and fails if any example does.

To run from your build tree:

```powershell
# From your build directory (after configure)
ctest -R BindlessExamplesValidate -V
```

## Contributing

- Keep examples small and focused: one concept per file.
- When adding an example that exercises an edge case, include a one-line
  rationale in the YAML `meta.description` field.
- If an example intentionally demonstrates invalid input for tests, prefix the
  filename with `invalid_` and ensure tests expect failure.

## Questions or additions

If you'd like additional examples (e.g., multi-root-signature layouts,
platform-specific register tokens, or migration examples showing old vs new SSoT
styles), tell me which scenario to add and I'll create them.
