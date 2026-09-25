# Vortex benchmarks

## Many-light qualification and timing

`Oxygen.Vortex.Lighting.Benchmarks` owns the deterministic many-light presets,
complete-list image reference and separate Release timing runs. The accepted
[D register](../../../../design/vortex/milestones/exposure/EX07/EX07D/validation.md),
[final E comparisons](../../../../design/vortex/milestones/exposure/EX07/EX07E/validation.md)
and [F acceptance](../../../../design/vortex/milestones/exposure/EX07/EX07F/validation.md)
are the durable results. Reuse unchanged records; stage closure alone does not
justify a new capture.

When a relevant implementation/input change actually requires qualification,
run from the engine root using the existing selected Ninja tree:

```powershell
cmake --build --preset oxygen-ninja-release --target Oxygen.Vortex.Lighting.Benchmarks --parallel 8
python tools/vortex/RunManyLightBaseline.py --build-tree build-ninja --output out/analysis/many-light-candidate --cases sparse-1024 --families deferred forward --qualify-only
```

Omitting `--qualify-only` also collects timing after the runner's CPU/GPU load
preflight. Use a new output directory when identities change. `--resume` reuses
only matching frozen binaries, shaders and recipes. `--qualified-from` requires
the same identities and collects new timing; it is not a general evidence-reuse
option. Prerequisites are Python with numpy, Pillow and jsonschema.

Native throughput uses `build-ninja` Release (Tracy OFF). Attribution uses
`build-tracy-ninja` Release (Tracy ON), with a matching `--tracy-capture` executable
when a trace is required. Keep these measurements separate; do not use Debug
timing or create another build tree. Run GPU jobs serially and allow ordinary
desktop use. New baseline acceptance still requires the user's visual approval.

## Exposure benchmarks

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

The [exposure inventory](../../../../design/vortex/milestones/exposure/EX05.1/README.md#tasks-and-outcome)
owns the recipe controls and measurement requirements. Run GPU workloads
serially. Do not enable every disabled workload as an ordinary test suite or
rerun timing experiments merely to validate a directory/target split. Debug
builds support discovery; Release-only workloads retain their configuration
guards. Existing historical evidence keeps the executable/source paths used
when it was collected.
