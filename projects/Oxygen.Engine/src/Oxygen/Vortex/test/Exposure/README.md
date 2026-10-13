# Native exposure tests

These sources build the `Oxygen.Vortex.Exposure.Tests` correctness executable.
Performance workloads live in [Vortex/Benchmarks](../../Benchmarks/README.md)
and build `Oxygen.Vortex.Exposure.Benchmarks`. Test suite names and filters are
preserved across the separation.

## Layout

- `*_test.cpp` files own one correctness responsibility: metering, adaptation,
  transitions, sharing, frame domains, HDR qualification, producer bounds,
  conversion, environment rendering or resource lifetime.
- `Fixtures/ExposureGpuFixture.*` provides controlled inputs and GPU readback.
  Service and lifecycle helpers have separate implementation files.
- `Fixtures/ExposureLightingFixture.*` provides the controlled rendered scene.
  Shared-view lifecycle and auxiliary handoff helpers are separate.
- `Fixtures/ExposureTestGraphics.*` contains the test backend, fault injection
  and allocation observations. `ExposureTestEngine.h` contains the engine mock;
  `ExposureTestTags.*` owns private-token test access.
- `oxygen-vortex-exposure-test-support` compiles these fixture implementations
  once per configuration and supplies them to both executables. It owns the
  workspace definition and the separate tone-bounds shader-probe dependency.

## Editing rules

Keep fixture headers limited to declarations, state and templates that must be
visible to callers. Define ordinary methods out of line. Add a focused scenario
helper when setup or ownership logic obscures an assertion; reuse existing
helpers before adding another. Keep independent numerical oracles independent
of the production implementation.

Use braces for control statements, trailing commas in multiline enum definitions
and initializer lists, no trailing commas in single-line constructs, descriptive
names and explicit resource lifetimes. Normally
construct defaulted input records and assign the fields needed by the test. Fix
actionable clang-tidy findings; retain only specific, locally documented
exceptions. Keep test data, assertions and tolerances easy to review. Do not
include implementation `.cpp` files, create an umbrella implementation header,
or combine unrelated domains to reduce the file count.

## Running

Ordinary correctness tests run without enabling disabled tests:

```powershell
cmake --build out/build-ninja --config Debug --target Oxygen.Vortex.Exposure.Tests --parallel 12
./out/build-ninja/bin/Debug/Oxygen.Vortex.Exposure.Tests.exe --gtest_filter=-*DISABLED_*
```

Run GPU cases serially. The correctness executable contains no benchmark cases.
Performance and memory experiments remain disabled by default in their own
executable and require their existing explicit filters and environment settings.
