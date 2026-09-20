# Native exposure tests

These sources build the existing `Oxygen.Vortex.ExposureGpu.Tests` executable.
Test suite names, command-line filters and opt-in benchmark names are preserved.

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
- `Benchmarks/` contains the eight existing disabled, opt-in experiments.
  Timing and allocation accounting have separate implementations. Allocation
  setup, measurement and lifecycle sequencing are separate responsibilities.

## Editing rules

Keep fixture headers limited to declarations, state and templates that must be
visible to callers. Define ordinary methods out of line. Add a focused scenario
helper when setup or ownership logic obscures an assertion; reuse existing
helpers before adding another. Keep independent numerical oracles independent
of the production implementation.

Use braces for control statements, trailing commas in non-empty braced
initializer lists, descriptive names and explicit resource lifetimes. Normally
construct defaulted input records and assign the fields needed by the test. Fix
actionable clang-tidy findings; retain only specific, locally documented
exceptions. Keep test data, assertions and tolerances easy to review. Do not
include implementation `.cpp` files, create an umbrella implementation header,
or combine unrelated domains to reduce the file count.

## Running

Ordinary correctness tests run without enabling disabled tests:

```powershell
cmake --build out/build-ninja --config Debug --target Oxygen.Vortex.ExposureGpu.Tests --parallel 12
./out/build-ninja/bin/Debug/Oxygen.Vortex.ExposureGpu.Tests.exe --gtest_filter=-*DISABLED_*
```

Run GPU cases serially. Performance and memory experiments remain disabled by
default and require their existing explicit filters and environment settings.
Do not run them as part of a structural test-source change.
