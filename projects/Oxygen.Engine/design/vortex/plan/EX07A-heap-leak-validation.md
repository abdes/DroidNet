# Per-frame diagnostics heap leak

The EX07A shutdown investigation identified CPU heap leaks in the shared
`SceneRenderer::RenderCurrentView` diagnostics construction. CDB matched all nine
surviving allocations in a one-frame VortexBasic run to conditional
`std::vector<std::string>` member initializers. Each allocation was a 16-byte
MSVC `std::_Container_proxy`. VortexBasic leaked 27 blocks in three frames;
LightBench leaked 2,700 blocks (43,200 bytes) in 300 frames.

## Reproduction and fix

MSVC 19.51.36257 x64, toolset directory `14.51.36231`, reproduces the defect
without the engine using `/std:c++latest /EHsc /Od /MDd /Z7`. The standalone
[reproducer](../../../out/build-ninja/analysis/vortex/exposure-lightbench/ex07a/conditional-vector-repro.cpp)
measures CRT heap differences across three iterations of each construction:

```text
mode=0 normal_blocks=3 normal_bytes=48
mode=1 normal_blocks=0 normal_bytes=0
mode=2 normal_blocks=0 normal_bytes=0
```

Mode 0 initializes an aggregate vector member with
`enabled ? std::vector<std::string>{"output"} : std::vector<std::string>{}`.
Mode 1 selects `std::initializer_list<std::string>` operands instead. Mode 2
uses a named aggregate and conditionally populates its vector. Both condition
outcomes are exercised. The precise compiler construction/destruction defect
has not been isolated; the allocation/lifetime failure is reproduced.

The production fix uses mode 1 for all nine conditional vector initializers.
Each vector is constructed directly from the selected list, retaining owning
strings and the existing diagnostic contents. No leak reporting or iterator
checks are disabled. Other compiler versions and Release configurations have
not been qualified for this compiler defect.

## Validation

- Debug SceneRendererDeferredCore: 64 tests passed.
- Debug DiagnosticsFrameLedger: 5 tests passed.
- Patched Debug VortexBasic: normal exit, no CRT leak dump after 3 and 300 frames.
- Patched Debug LightBench: normal exit, no CRT leak dump after 300 frames.
- Oxytidy ran on the changed C++ file: 88 warnings. A baseline analysis of the
  committed source through a Clang virtual-file overlay reports the identical
  88 check/message pairs, with zero added warnings and no coverage gaps. This
  checkpoint does not claim whole-file lint cleanliness. No suppressions were
  added; `heap-tidy-comparison.json` records the comparison.

The 300-frame runs used CDB with the normal D3D12 backend and debug layer:

```powershell
& 'C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe' -g -G `
  -logo <debugger-log> <Debug-demo-executable> `
  --frames 300 --fps 60 --vsync=false --debug-layer=true --aftermath=false
```

Evidence under `out/build-ninja/analysis/vortex/exposure-lightbench/ex07a/`:
`leaked-allocation-stacks.txt`, `vortexbasic-3-before.cdb.log`,
`lightbench-cdb-shutdown.log`, `vortexbasic-3-after.cdb.log`,
`vortexbasic-300-after.cdb.log`, `lightbench-300-after.cdb.log`, corresponding
stdout logs with runtime exit code zero, `heap-scenerenderer-debug.json`,
`heap-ledger-debug.json`, and `tidy-heap/`.

This closes the observed frame-scaled CPU leak. It does not close the remaining
EX07A lighting ABI migration or establish GPU resource lifetime correctness.
