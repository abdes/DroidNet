# Vortex exposure benchmarks

`Oxygen.Vortex.Exposure.Benchmarks` contains the eight existing GoogleTest-based
performance and memory experiments. It is available on Windows when
`OXYGEN_BUILD_TESTS` is enabled, following the module's test/benchmark convention.
Workload names, filters, environment controls and output formats are unchanged;
all eight cases retain their `DISABLED_` prefix and require explicit selection.

## Layout

- `*_bench.cpp` files register the workloads.
- `ExposureBaseline*` and `ExposureBenchmarkFixture.*` own the baseline recipes,
  rendering, events and results.
- `Test/Support/CpuTimingCapture.*` owns the shared bounded CPU observer. The
  `oxygen::vortex-test-support` module compiles it once per configuration;
  both correctness tests and benchmarks depend on this test infrastructure.
  Production targets do not link it.
- `ExposureAllocation*` owns allocation setup, measurement and lifecycle checks.
- The shared `oxygen-vortex-exposure-test-support` library supplies the native
  fixtures from `Test/Exposure/Fixtures`; fixture sources are compiled once per
  configuration. The correctness executable is
  `Oxygen.Vortex.Exposure.Tests`.

## Running

Build and inspect the available workload names from the engine directory:

```powershell
cmake --build out/build-ninja --config Release --target Oxygen.Vortex.Exposure.Benchmarks --parallel 12
./out/build-ninja/bin/Release/Oxygen.Vortex.Exposure.Benchmarks.exe --gtest_list_tests
```

Set the environment controls required by the selected recipe, then run exactly
that workload:

```powershell
./out/build-ninja/bin/Release/Oxygen.Vortex.Exposure.Benchmarks.exe -v=OFF --gtest_also_run_disabled_tests --gtest_filter=ExposureIndoorOutdoorBenchmarkTest.DISABLED_ReleaseIndoorOutdoorBaseline
```

The [exposure inventory](../../../../design/vortex/IMPLEMENTATION_STATUS.md#321-slice-51-performance-qualification-and-correction)
owns the recipe controls and measurement requirements. Run GPU workloads
serially. Do not enable every disabled workload as an ordinary test suite or
rerun timing experiments merely to validate a directory/target split. Debug
builds support discovery; Release-only workloads retain their configuration
guards. Existing historical evidence keeps the executable/source paths used
when it was collected.
