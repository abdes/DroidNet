# BindingSlots examples

This folder contains small, focused `BindingSlots` YAML examples for the
BindlessCodeGen generator. Each example demonstrates a specific layout or
feature of the schema and the generator's semantic checks.

## Goals

- Provide concise, copy-pasteable examples for common binding patterns
- Serve as quick unit test inputs for the generator's `dry_run` validation
- Document how to validate examples locally and in CI

## Examples included

  check).
  range.
  table.
  JSON and header.
 heaps_valid.yaml shows a simple two-heap setup using a unified capacity per heap; the runtime JSON also uses a single 'capacity' per heap (visibility is implied by the :cpu/:gpu suffix in the key).

## Validation (local)

1. Activate your Python environment that contains the project dependencies
   (e.g., `pyyaml`, `jsonschema`).

2. From the `BindlessCodeGen` directory run the examples validator script:

```powershell
# From the repository root
cd 'src/Oxygen/Core/Tools/BindlessCodeGen'
python -m examples.run_validate_examples
```

Use `-v` to show generator progress/logs (quiet by default):

```powershell
python -m examples.run_validate_examples -v
```

Or run the helper directly from the examples folder:

```powershell
# From the repository root
cd 'src/Oxygen/Core/Tools/BindlessCodeGen/examples'
python run_validate_examples.py
```

Tip: Output uses colors when the terminal supports it. Set `NO_COLOR=1` to
disable colors.

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
ctest --preset=test-windows -C Debug -R BindlessExamplesValidate -V
```

## Contributing

- Keep examples small and focused: one concept per file.
- When adding an example that exercises an edge case, include a one-line
  rationale in the YAML `meta.description` field.
- If an example intentionally demonstrates invalid input for tests, prefix the
  filename with `invalid_` and ensure tests expect failure.
