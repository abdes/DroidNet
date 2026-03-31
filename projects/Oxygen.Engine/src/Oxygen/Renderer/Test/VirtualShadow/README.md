# Virtual Shadow Map Tests

Tests for the full VSM pipeline, from CPU cache and address-space logic through all GPU rendering passes. The coverage is currently spread across twenty-one test programs:

- `VsmBasic`
- `VsmBeginFrame`
- `VsmVirtualAddressSpace`
- `VsmRemap`
- `VsmProjectionRecords`
- `VsmPageRequests`
- `VsmPageReuse`
- `VsmAvailablePages`
- `VsmPageMappings`
- `VsmHierarchicalFlags`
- `VsmMappedMips`
- `VsmPageInitialization`
- `VsmShadowRasterization`
- `VsmStaticDynamicMerge`
- `VsmShadowHzb`
- `VsmShadowProjection`
- `VsmFrameExtraction`
- `VsmCacheValidity`
- `VirtualShadows`
- `VirtualShadowGpuLifecycle`
- `Oxygen.Renderer.VirtualShadowSceneObserver.Tests`

Architecture reference: [`design/VirtualShadowMapArchitecture.md`](../../../../../design/VirtualShadowMapArchitecture.md)

## Philosophy

- Stage-owned suites are the primary functional regression gate. Stages 1-17 now have dedicated executables.
- Cross-cutting helper and unit suites are secondary contract coverage. They support stage suites, but they do not replace them.
- Stage suites must reuse the shared stage harnesses in `VirtualShadowStageCpuHarness.h` and `VirtualShadowStageGpuHarness.h` instead of rebuilding bespoke setup paths.
- Functional stage suites should prefer multi-page inputs, real geometry, real shadows, and assertions by behavior, virtual coordinate, or physical output rather than magic slot numbers.
- The dedicated Stage 1-17 executables now also reuse `VirtualShadowLiveSceneHarness.h` for live real-scene validation on real geometry, real light data, and multi-page directional or local layouts.
- One-page fixtures and direct slot assertions are acceptable only for narrow ABI checks, malformed-input checks, or other explicitly scoped negative tests.
- In `src/Oxygen/Renderer/Test/CMakeLists.txt`, use the concise `m_gtest_program(...)` names such as `VsmBeginFrame`, `VsmVirtualAddressSpace`, `VsmRemap`, `VsmProjectionRecords`, `VsmPageRequests`, `VsmPageReuse`, `VsmAvailablePages`, `VsmPageMappings`, `VsmHierarchicalFlags`, `VsmMappedMips`, `VsmPageInitialization`, `VsmShadowRasterization`, `VsmStaticDynamicMerge`, `VsmShadowHzb`, `VsmShadowProjection`, `VsmFrameExtraction`, `VsmCacheValidity`, `VirtualShadows`, and `VirtualShadowGpuLifecycle`. The macro generates the fully qualified build target and binary names.
- A passing helper suite is not evidence that a stage is complete. Completion claims require the dedicated stage suite, any required broader reruns, and explicit recorded evidence.
- When correctness is in doubt, the AI agent working in this repository must check the UE5 reference implementation through a new or recycled subagent rather than reasoning from memory or local intuition alone.

## Reference Material

- The local Unreal-reference navigation map lives at [`UE5-VSM-Source-Analysis.md`](./UE5-VSM-Source-Analysis.md).
- When in doubt about implementation or test correctness, the AI agent must use a new or recycled subagent to inspect the UE5 reference implementation, using that document as the navigation map.
- This is an owner-directed workflow rule for VSM work, not an optional last-resort step.
- Reference comparison informs implementation and test design; repository architecture and local validation evidence still decide completion claims.

---

## Test Executables

| CMake declaration | Generated target / binary | What it covers |
| ----------------- | ------------------------- | -------------- |
| `VsmBasic` | `Oxygen.Renderer.VsmBasic.Tests` / `bin/<Config>/Oxygen.Renderer.VsmBasic.Tests.exe` | Cross-cutting type contracts: copy semantics, enum string surfaces, DTO field round-trips, `IsValid`/`Validate` for page requests and remap keys |
| `VsmBeginFrame` | `Oxygen.Renderer.VsmBeginFrame.Tests` / `bin/<Config>/Oxygen.Renderer.VsmBeginFrame.Tests.exe` | Stage 1 only: seam type contracts, build-state machine, cache-data state transitions, pool compatibility, HZB availability, plus a dedicated live two-box directional real-scene validation when the D3D12 backend is available |
| `VsmVirtualAddressSpace` | `Oxygen.Renderer.VsmVirtualAddressSpace.Tests` / `bin/<Config>/Oxygen.Renderer.VsmVirtualAddressSpace.Tests.exe` | Stage 2 only: frame-local virtual layout allocation/publication contracts for directional clipmaps and local lights, plus a dedicated live two-box directional real-scene validation when the D3D12 backend is available |
| `VsmRemap` | `Oxygen.Renderer.VsmRemap.Tests` / `bin/<Config>/Oxygen.Renderer.VsmRemap.Tests.exe` | Stage 3 only: previous-driven remap construction, stable-key matching, clipmap reuse offsets, explicit reuse-rejection contracts, and a dedicated live two-box directional real-scene validation when the D3D12 backend is available |
| `VsmProjectionRecords` | `Oxygen.Renderer.VsmProjectionRecords.Tests` / `bin/<Config>/Oxygen.Renderer.VsmProjectionRecords.Tests.exe` | Stage 4 only: real-scene projection-record construction and publication contracts for multi-page directional clipmaps and paged local spot lights when the D3D12 backend is available |
| `VsmPageRequests` | `Oxygen.Renderer.VsmPageRequests.Tests` / `bin/<Config>/Oxygen.Renderer.VsmPageRequests.Tests.exe` | Stage 5 only: real-scene/live-shell directional request-flag validation plus dedicated real-depth multi-level local and directional clip-level request-flag validation |
| `VsmPageReuse` | `Oxygen.Renderer.VsmPageReuse.Tests` / `bin/<Config>/Oxygen.Renderer.VsmPageReuse.Tests.exe` | Stage 6 only: real-scene reuse validation for stable directional reuse, directional clipmap pan reuse, moved-caster invalidation seeding, and retained unreferenced paged-spotlight continuity |
| `VsmAvailablePages` | `Oxygen.Renderer.VsmAvailablePages.Tests` / `bin/<Config>/Oxygen.Renderer.VsmAvailablePages.Tests.exe` | Stage 7 only: real-scene available-page packing validation for stable directional reuse, clipmap pan release, and paged local-light continuity |
| `VsmPageMappings` | `Oxygen.Renderer.VsmPageMappings.Tests` / `bin/<Config>/Oxygen.Renderer.VsmPageMappings.Tests.exe` | Stage 8 only: real-scene fresh-page mapping validation for moved-caster invalidation, mixed reuse plus fresh local-light allocation, and same-recorder invalidation-seed handoff |
| `VsmHierarchicalFlags` | `Oxygen.Renderer.VsmHierarchicalFlags.Tests` / `bin/<Config>/Oxygen.Renderer.VsmHierarchicalFlags.Tests.exe` | Stage 9 only: real-scene hierarchical flag propagation validated against a CPU model built from actual Stage 8 outputs for directional clipmaps, mixed local layouts, and invalidated refresh cases |
| `VsmMappedMips` | `Oxygen.Renderer.VsmMappedMips.Tests` / `bin/<Config>/Oxygen.Renderer.VsmMappedMips.Tests.exe` | Stage 10 only: real-scene mapped-descendant propagation validated against a CPU model built from actual Stage 8 page tables and flags for directional clipmaps, mixed directional-plus-local layouts, mixed local layouts, reuse-only continuity, and invalidated refresh cases |
| `VsmPageInitialization` | `Oxygen.Renderer.VsmPageInitialization.Tests` / `bin/<Config>/Oxygen.Renderer.VsmPageInitialization.Tests.exe` | Stage 11 only: real-scene selective page initialization validation for stable cached frames, clipmap-pan fresh-page clears, and static-slice copy into invalidated dynamic pages |
| `VsmShadowRasterization` | `Oxygen.Renderer.VsmShadowRasterization.Tests` / `bin/<Config>/Oxygen.Renderer.VsmShadowRasterization.Tests.exe` | Stage 12 only: real-scene shadow-raster page-job and raster-output validation at the Stage 12 boundary, plus focused pass-level coverage for point-light routing, HZB culling, reveal forcing, and static-only slice routing |
| `VsmStaticDynamicMerge` | `Oxygen.Renderer.VsmStaticDynamicMerge.Tests` / `bin/<Config>/Oxygen.Renderer.VsmStaticDynamicMerge.Tests.exe` | Stage 13 only: real-scene static-into-dynamic merge validation for directional and local lights, including dynamic-only invalidation and static-invalidated continuity contracts |
| `VsmShadowHzb` | `Oxygen.Renderer.VsmShadowHzb.Tests` / `bin/<Config>/Oxygen.Renderer.VsmShadowHzb.Tests.exe` | Stage 14 only: real-scene shadow-space HZB validation from real Stage 13 inputs for directional and local scenes, including exact HZB mip-chain reconstruction, dirty-state folding, and preserved-HZB continuity across reused frames |
| `VsmShadowProjection` | `Oxygen.Renderer.VsmShadowProjection.Tests` / `bin/<Config>/Oxygen.Renderer.VsmShadowProjection.Tests.exe` | Stage 15 only: real Stage 1-14 directional and paged-local projection/composite validation, including isolated-vs-live-shell comparison and analytic floor-band regression probes |
| `VsmFrameExtraction` | `Oxygen.Renderer.VsmFrameExtraction.Tests` / `bin/<Config>/Oxygen.Renderer.VsmFrameExtraction.Tests.exe` | Stage 16 only: real live-shell extraction/finalization validation for reusable extracted snapshots, static continuity retention, paged local-light extraction, and next-frame finalization of queued readback |
| `VsmCacheValidity` | `Oxygen.Renderer.VsmCacheValidity.Tests` / `bin/<Config>/Oxygen.Renderer.VsmCacheValidity.Tests.exe` | Stage 17 only: real live-scene cache-validity validation for no-validity-before-extraction, post-extraction reuse enablement, invalidation clearing, and local-light reuse recovery |
| `VirtualShadows` | `Oxygen.Renderer.VirtualShadows.Tests` / `bin/<Config>/Oxygen.Renderer.VirtualShadows.Tests.exe` | CPU-only stage and cross-cutting logic beyond the dedicated early-stage executables: planner, invalidation, orchestration, helper contracts |
| `VirtualShadowGpuLifecycle` | `Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests` / `bin/<Config>/Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests.exe` | GPU-backed integration and supporting pass coverage beyond the dedicated stage executables: physical-pool lifecycle, cache-resource publication, propagation smoke, bridge/orchestration, and invalidation |
| `Oxygen.Renderer.VirtualShadowSceneObserver.Tests` | `Oxygen.Renderer.VirtualShadowSceneObserver.Tests` / `bin/<Config>/Oxygen.Renderer.VirtualShadowSceneObserver.Tests.exe` | Scene observer → cache manager invalidation integration |

**All manual testing uses the `build-ninja` tree only.** Visual Studio build trees are not used for running tests by hand.

Corrective status note on `2026-03-28`: the earlier dedicated-stage pass evidence in this README
was collected from `out/build-ninja`.

Later on `2026-03-28`, the shared local-light Stage 6-10 regressions were traced to a test-side
previous-frame seed bug: those suites were extracting the prior frame immediately after
`RunTwoBoxPageRequestBridge(...)`, before the GPU page-management stages had materialized the page
table and physical metadata needed for reuse. The shared harness now seeds those current-frame
reuse tests through `PrimeTwoBoxExtractedFrame(...)`, which runs a real prior frame through the
live shell and publishes an extracted cache frame before the Stage 6-10 current frame begins.

Recorded evidence after that fix in `out/build-ninja`:
- `Oxygen.Renderer.VsmBeginFrame.Tests.exe`: `33 tests from 4 test suites` passed
- `Oxygen.Renderer.VsmVirtualAddressSpace.Tests.exe`: `8 tests from 4 test suites` passed
- `Oxygen.Renderer.VsmRemap.Tests.exe`: `29 tests from 5 test suites` passed
- `Oxygen.Renderer.VsmProjectionRecords.Tests.exe`: `2 tests from 1 test suite` passed
- `Oxygen.Renderer.VsmPageRequests.Tests.exe`: `3 tests from 1 test suite` passed
- `Oxygen.Renderer.VsmPageReuse.Tests.exe`: `4 tests from 1 test suite` passed
- `Oxygen.Renderer.VsmAvailablePages.Tests.exe`: `3 tests from 1 test suite` passed
- `Oxygen.Renderer.VsmPageMappings.Tests.exe`: `3 tests from 1 test suite` passed
- `Oxygen.Renderer.VsmHierarchicalFlags.Tests.exe`: `3 tests from 1 test suite` passed
- `Oxygen.Renderer.VsmMappedMips.Tests.exe`: `5 tests from 1 test suite` passed
- `Oxygen.Renderer.VsmPageInitialization.Tests.exe`: `3 tests from 1 test suite` passed
- `Oxygen.Renderer.VsmShadowRasterization.Tests.exe`: `13 tests from 3 test suites` passed

Later on `2026-03-28`, another live-scene timing issue was fixed in the same harness: multi-frame
tests were reusing `Slot { 0 }` for every offscreen frame inside one process, which could make the
inline staging ring race the test and produce `RingBufferStaging.cpp:294` warnings. The shared
live-scene harness now rotates test offscreen frames across a 3-slot ring based on sequence
number. Recorded evidence after that fix in `out/build-ninja`:
- `Oxygen.Renderer.VsmPageReuse.Tests.exe`: `4 tests from 1 test suite` passed
- `Oxygen.Renderer.VsmAvailablePages.Tests.exe`: `3 tests from 1 test suite` passed
- `Oxygen.Renderer.VsmPageMappings.Tests.exe`: `3 tests from 1 test suite` passed
- `Oxygen.Renderer.VsmHierarchicalFlags.Tests.exe`: `3 tests from 1 test suite` passed
- `Oxygen.Renderer.VsmMappedMips.Tests.exe`: `5 tests from 1 test suite` passed
- `Oxygen.Renderer.VsmPageInitialization.Tests.exe`: `3 tests from 1 test suite` passed
- `Oxygen.Renderer.VsmAvailablePages.Tests.exe --gtest_filter=VsmAvailablePagePackingLiveSceneTest.StablePagedSpotLightScenePacksOnlyUnusedPagesAfterLocalReuse --gtest_repeat=10`: all `10/10` iterations passed without `-v`

Later on `2026-03-28`, the remaining `RingBufferStaging.cpp:294` warnings were traced to inline
staging ownership, not to Stage 7/13 logic. `Renderer::GetStagingProvider()` returns the inline
staging ring, and that ring must be retired only by `InlineTransfersCoordinator`. Registering the
same provider with the upload-coordinator retire path mixes unrelated fence timelines and produces
false partition-reuse warnings. The renderer now keeps `Renderer.InlineStaging` on the inline
retirement path only. Recorded evidence after that fix in `out/build-ninja`:
- `Oxygen.Renderer.VsmAvailablePages.Tests.exe`: `4 tests from 1 test suite` passed
- `Oxygen.Renderer.VsmAvailablePages.Tests.exe --gtest_filter=VsmAvailablePagePackingLiveSceneTest.StablePagedSpotLightSceneDoesNotWarnAboutInlineStagingPartitionReuse --gtest_repeat=5`: all `5/5` iterations passed
- `Oxygen.Renderer.VsmStaticDynamicMerge.Tests.exe`: `7 tests from 2 test suites` passed
- `Oxygen.Renderer.VsmAvailablePages.Tests.exe --gtest_repeat=3`: all `9/9` executions passed as combined-suite runs
- `Oxygen.Renderer.VsmHierarchicalFlags.Tests.exe --gtest_filter=VsmHierarchicalPageFlagsLiveSceneTest.AddedSpotLightsMatchCpuHierarchicalPropagationAcrossMixedLocalLayouts --gtest_repeat=10`: all `10/10` iterations passed without `-v`
- `Oxygen.Renderer.VsmPageInitialization.Tests.exe --gtest_repeat=3`: all `9/9` executions passed as combined-suite runs
- `Oxygen.Renderer.VsmShadowRasterization.Tests.exe --gtest_repeat=3`: all `39/39` executions passed as combined-suite runs

Corrective status note on `2026-03-28` for Stage 12: the first live Stage 12 refactor attempted to
assert dirty metadata and rasterized depth from `GetPreviousFrame()` after `ExecutePreparedViewShell(...)`.
That was the wrong boundary: extraction is later in the pipeline, so those assertions were reading
the extracted snapshot instead of the current-frame Stage 12 GPU outputs. The dedicated Stage 12
suite now stops at Stage 12 through `RunTwoBoxShadowRasterizationStage(...)` and reads current-frame
dirty flags, physical metadata, and shadow-depth samples directly. The current remaining gap is more
narrow: a fully stage-fed live static-recache producer chain is still not proven there, so static
slice routing remains directly covered by `VsmShadowRasterizerPassGpuTest.ExecuteRoutesStaticOnlyPagesIntoStaticSliceAndPublishesFeedback`.

Additional corrective note on `2026-03-28`: ASan later exposed a real engine-side offscreen-view
lifetime bug in that same Stage 12 slice. `Renderer::OffscreenFrameSession::SetCurrentView(...)`
was storing raw pointers to caller-owned `ResolvedView` temporaries, and
`VsmProjectionPass` could resume against dead stack memory in
`VsmShadowRasterizerPassGpuTest.ExecuteRasterizedDirectionalPagesProjectLocalizedShadowMaskInsteadOfPageWideDarkening`.
`Renderer::OffscreenFrameSession` now owns copies of the active resolved and prepared view
snapshots and rebinds those pointers after moves. Recorded evidence in `out/build-ninja` after that
fix:
- `Oxygen.Renderer.VsmShadowRasterization.Tests.exe`: `13 tests from 3 test suites` passed
- `Oxygen.Renderer.VsmShadowRasterization.Tests.exe --gtest_filter=VsmShadowRasterizerPassGpuTest.ExecuteRasterizedDirectionalPagesProjectLocalizedShadowMaskInsteadOfPageWideDarkening --gtest_repeat=5`: all `5/5` executions passed
- `Oxygen.Renderer.VsmShadowProjection.Tests.exe`: `4 tests from 1 test suite` passed
- `Oxygen.Renderer.VsmFrameExtraction.Tests.exe`: `4 tests from 1 test suite` passed

Additional corrective note on `2026-03-28` for Stage 16: the first dedicated extraction rewrite
compiled against the wrong directional-layout field and then exposed a real bridge/readback
lifetime defect in the shared readback subsystem. `ReadbackTracker::OnFrameStart(...)` was
retiring same-slot tickets before Stage 16 finalization consumed them, and completed tickets had
no owner-driven release path. The tracker now retains same-slot tracked tickets until the owning
readback object resets, and D3D12/headless readback objects explicitly forget completed tickets on
reset. Recorded evidence in `out/build-ninja` after the Stage 16 ownership split plus the
readback-lifetime fix:
- `Oxygen.Graphics.Common.ReadbackTracker.Tests.exe`: `13 tests from 1 test suite` passed
- `Oxygen.Graphics.Headless.ReadbackManager.Tests.exe`: `24 tests from 12 test suites` passed
- `Oxygen.Renderer.VsmFrameExtraction.Tests.exe`: `4 tests from 1 test suite` passed
- `Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests.exe`: `24 tests from 5 test suites` passed

Additional corrective note on `2026-03-28` for Stage 17: the old Stage 17 ownership was still a
synthetic CPU-only `VsmCacheValidityTest` inside `VirtualShadows`. Stage 17 now lives in the
dedicated `VsmCacheValidity` executable and uses real live-scene inputs only. The dedicated suite
proves the true Stage 17 boundaries: a real page-request bridge must not mark cache data valid
before extraction, a real Stage 16 extraction must enable next-frame reuse, explicit
`InvalidateAll(...)` must clear reuse validity until a fresh extraction completes, and the same
contract must hold for paged local lights. Recorded evidence in `out/build-ninja` after the Stage
17 ownership split:
- `Oxygen.Renderer.VsmCacheValidity.Tests.exe`: `4 tests from 1 test suite` passed
- `Oxygen.Renderer.VsmCacheValidity.Tests.exe --gtest_repeat=3`: all `12/12` executions passed
- `Oxygen.Renderer.VirtualShadows.Tests.exe`: `49 tests from 10 test suites` passed
- `Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests.exe`: `24 tests from 5 test suites` passed

This does not close the broader visual gap: a user-provided live renderer capture still shows
incorrect final floor-shadow continuity, so Stage 12 remains `in_progress` until the Stage 12→15
path is fully explained and validated.

`out/build-asan-vs` was not rerun after this correction and therefore remains unvalidated in this
README. Manual testing still uses `build-ninja` only.

---

## Build Commands

For programs declared with `m_gtest_program(...)`, CMake generates the fully qualified build target name shown below. Build a single target before running its tests:

```powershell
# Basic type-contract tests
cmake --build out/build-ninja --config Debug --target "Oxygen.Renderer.VsmBasic.Tests" --parallel 6

# Stage 1 tests
cmake --build out/build-ninja --config Debug --target "Oxygen.Renderer.VsmBeginFrame.Tests" --parallel 6

# Stage 2 tests
cmake --build out/build-ninja --config Debug --target "Oxygen.Renderer.VsmVirtualAddressSpace.Tests" --parallel 6

# Stage 3 tests
cmake --build out/build-ninja --config Debug --target "Oxygen.Renderer.VsmRemap.Tests" --parallel 6

# Stage 4 tests
cmake --build out/build-ninja --config Debug --target "Oxygen.Renderer.VsmProjectionRecords.Tests" --parallel 6

# Stage 5 tests
cmake --build out/build-ninja --config Debug --target "Oxygen.Renderer.VsmPageRequests.Tests" --parallel 6

# Stage 6 tests
cmake --build out/build-ninja --config Debug --target "Oxygen.Renderer.VsmPageReuse.Tests" --parallel 6

# Stage 7 tests
cmake --build out/build-ninja --config Debug --target "Oxygen.Renderer.VsmAvailablePages.Tests" --parallel 6

# Stage 8 tests
cmake --build out/build-ninja --config Debug --target "Oxygen.Renderer.VsmPageMappings.Tests" --parallel 6

# Stage 9 tests
cmake --build out/build-ninja --config Debug --target "Oxygen.Renderer.VsmHierarchicalFlags.Tests" --parallel 6

# Stage 10 tests
cmake --build out/build-ninja --config Debug --target "Oxygen.Renderer.VsmMappedMips.Tests" --parallel 6

# Stage 11 tests
cmake --build out/build-ninja --config Debug --target "Oxygen.Renderer.VsmPageInitialization.Tests" --parallel 6

# Stage 12 tests
cmake --build out/build-ninja --config Debug --target "Oxygen.Renderer.VsmShadowRasterization.Tests" --parallel 6

# Stage 13 tests
cmake --build out/build-ninja --config Debug --target "Oxygen.Renderer.VsmStaticDynamicMerge.Tests" --parallel 6

# Stage 14 tests
cmake --build out/build-ninja --config Debug --target "Oxygen.Renderer.VsmShadowHzb.Tests" --parallel 6

# Stage 15 tests
cmake --build out/build-ninja --config Debug --target "Oxygen.Renderer.VsmShadowProjection.Tests" --parallel 6

# Stage 16 tests
cmake --build out/build-ninja --config Debug --target "Oxygen.Renderer.VsmFrameExtraction.Tests" --parallel 6

# Stage 17 tests
cmake --build out/build-ninja --config Debug --target "Oxygen.Renderer.VsmCacheValidity.Tests" --parallel 6

# All remaining CPU tests
cmake --build out/build-ninja --config Debug --target "Oxygen.Renderer.VirtualShadows.Tests" --parallel 6

# GPU lifecycle tests (requires D3D12 backend)
cmake --build out/build-ninja --config Debug --target "Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests" --parallel 6

# Scene observer integration
cmake --build out/build-ninja --config Debug --target "Oxygen.Renderer.VirtualShadowSceneObserver.Tests" --parallel 6
```

For Release builds, substitute `Debug` with `Release` throughout. The executables land in `out/build-ninja/bin/<Config>/`.

---

## Running Tests

### Run an entire suite

```powershell
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VsmBasic.Tests.exe
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VsmBeginFrame.Tests.exe
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VsmVirtualAddressSpace.Tests.exe
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VsmRemap.Tests.exe
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VsmProjectionRecords.Tests.exe
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VsmPageRequests.Tests.exe
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VsmPageReuse.Tests.exe
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VsmAvailablePages.Tests.exe
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VsmPageMappings.Tests.exe
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VsmHierarchicalFlags.Tests.exe
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VsmMappedMips.Tests.exe
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VsmPageInitialization.Tests.exe
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VsmShadowRasterization.Tests.exe
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VsmStaticDynamicMerge.Tests.exe
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VsmShadowHzb.Tests.exe
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VsmShadowProjection.Tests.exe
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VsmFrameExtraction.Tests.exe
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VsmCacheValidity.Tests.exe
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VirtualShadows.Tests.exe
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests.exe
```

### Run a specific test suite

```powershell
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests.exe `
    --gtest_filter="VsmPageRequestLiveSceneTest.*"
```

### Run a single test with high logging verbosity

Oxygen uses loguru. The `-v` flag controls the global verbosity level (0 = INFO only, 9 = all DLOG_F traces):

```powershell
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests.exe `
    --gtest_filter="VsmShadowRendererBridgeGpuTest.ExecutePreparedViewShellMatchesAnalyticFloorShadowClassificationForTwoBoxes" `
    -v 9
```

Useful verbosity levels:

| Level | What you get |
| ----- | ----------- |
| `-v 0` | `INFO` and above only (default) |
| `-v 1` | `LOG_F(1, ...)` traces (pass-level lifecycle) |
| `-v 2` | `LOG_F(2, ...)` traces (per-page decisions, planner detail) |
| `-v 9` | Everything, including `DLOG_F` chatter |

### Run all VSM tests via CTest

```powershell
ctest --test-dir out/build-ninja -C Debug --output-on-failure `
    -R "Oxygen\.Renderer\.(VsmBasic|VsmBeginFrame|VsmVirtualAddressSpace|VsmRemap|VsmProjectionRecords|VsmPageRequests|VsmPageReuse|VsmAvailablePages|VsmPageMappings|VsmHierarchicalFlags|VsmMappedMips|VsmPageInitialization|VsmShadowRasterization|VsmStaticDynamicMerge|VsmShadowHzb|VsmShadowProjection|VsmFrameExtraction|VsmCacheValidity|VirtualShadows|VirtualShadowGpuLifecycle|VirtualShadowSceneObserver)\.Tests"
```

---

## Stage-to-Test-Suite Mapping

The numbered headings correspond to the pipeline stages defined in the architecture document. The dedicated stage suite named first in each section is the primary regression gate for that stage. Any additional suites listed in the same section are supporting contract coverage.

### Stage 1 — Begin Frame

Cache manager captures the seam; build-state and cache-data state machines are advanced; pool compatibility and HZB availability are evaluated.

All Stage 1 tests live in the dedicated `VsmBeginFrame` CMake program, which produces the `Oxygen.Renderer.VsmBeginFrame.Tests.exe` binary.

| Test suite | File | What it covers |
| ---------- | ---- | -------------- |
| `VsmBeginFrameTest` | `VsmBeginFrame_test.cpp` | Seam type contracts, cold-start, warm-start, force-invalidate, sticky-invalidate, Abort/Reset recovery, illegal API ordering, shape-compatibility contract |
| `VsmBeginFramePoolCompatibilityTest` | `VsmBeginFramePoolCompatibility_test.cpp` | Each of the six pool-snapshot fields (`pool_identity`, `page_size_texels`, `tile_capacity`, `slice_count`, `depth_format`, `slice_roles`) independently triggers `kInvalidated`; compatible pool preserves `kAvailable` |
| `VsmBeginFrameHzbAvailabilityTest` | `VsmBeginFrameHzbAvailability_test.cpp` | Cold start reports HZB unavailable; warm start after `PublishCurrentFrameHzbAvailability(true)` reports available; absent HZB pool suppresses availability even when previous frame had HZB |
| `VsmBeginFrameLiveSceneTest` | `VsmBeginFrameLiveScene_test.cpp` | Real two-box directional scene: extracted frame survives `OnFrameStart`, prepared-view state is rebuilt for the new frame, and the new Stage 1 boundary retains previous-frame virtual history without carrying stale prepared-view state |

### Stage 2 — Virtual Address Space Allocation

Each light is assigned virtual IDs; clipmap and local-light layouts are computed.

| Test suite | File | Executable |
| ---------- | ---- | --------- |
| `VsmVirtualAddressAllocationTest` | `VsmVirtualAddressAllocation_test.cpp` | VsmVirtualAddressSpace |
| `VsmVirtualAddressSpaceIdsTest` | `VsmVirtualAddressSpaceIds_test.cpp` | VsmVirtualAddressSpace |
| `VsmVirtualAddressSpaceLayoutsTest` | `VsmVirtualAddressSpaceLayouts_test.cpp` | VsmVirtualAddressSpace |
| `VsmVirtualAddressLiveSceneTest` | `VsmVirtualAddressLiveScene_test.cpp` | VsmVirtualAddressSpace |

### Stage 3 — Remap Construction

Previous-to-current virtual ID mappings are generated from real previous/current frame snapshots, including clipmap pan offsets and explicit reuse-rejection reasons.

This dedicated Stage 3 executable now includes owner-required live-scene geometry validation for a retained multi-page directional light.

| Test suite | File | Executable |
| ---------- | ---- | --------- |
| `VsmRemapConstructionTest` | `VsmRemapConstruction_test.cpp` | VsmRemap |
| `VsmLocalRemapContractTest` / `VsmDirectionalRemapContractTest` | `VsmVirtualRemapBuilder_test.cpp` | VsmRemap |
| `VsmClipmapReuseTest` | `VsmVirtualClipmapHelpers_test.cpp` | VsmRemap |
| `VsmRemapLiveSceneTest` | `VsmRemapLiveScene_test.cpp` | VsmRemap |

### Stage 4 — Projection Data Upload

Per-map projection records are built from the prepared scene plus the current virtual frame, published onto the committed current frame, and retained on extraction for later invalidation consumers.

| Test suite | File | Executable |
| ---------- | ---- | --------- |
| `VsmProjectionRecordPublicationLiveSceneTest` | `VsmProjectionRecordPublication_test.cpp` | VsmProjectionRecords |

### Stage 5 — Page Request Generation

Visible scene depth is sampled to determine which virtual pages are requested. The dedicated Stage 5 executable now owns the real-data functional proof for this boundary. CPU request-merging policy remains helper coverage in `VsmBasic` and does not count as Stage 5 completion evidence.

| Test suite | File | Executable |
| ---------- | ---- | --------- |
| `VsmPageRequestLiveSceneTest` | `VsmPageRequests_test.cpp` | VsmPageRequests |

### Stages 6–8 — Physical Page Allocation GPU Passes

Stage 6: reuse previous pages; Stage 7: pack available pages; Stage 8: allocate new mappings.

| Test suite | File | Executable |
| ---------- | ---- | --------- |
| `VsmPageReuseLiveSceneTest` | `VsmPageReuse_test.cpp` | VsmPageReuse |
| `VsmAvailablePagePackingLiveSceneTest` | `VsmAvailablePagePacking_test.cpp` | VsmAvailablePages |
| `VsmNewPageMappingLiveSceneTest` | `VsmNewPageMapping_test.cpp` | VsmPageMappings |
| `VsmPhysicalPagePoolGpuLifecycleTest` | `VsmPhysicalPagePoolGpuLifecycle_test.cpp` | VirtualShadowGpuLifecycle |
| `VsmCacheManagerGpuResourcesTest` | `VsmCacheManagerGpuResources_test.cpp` | VirtualShadowGpuLifecycle |
| `VsmPageAllocationPlannerTest` | `VsmPageAllocationPlanner_test.cpp` | VirtualShadows |

### Stages 9–11 — Flag Propagation and Page Initialization

Stage 9: hierarchical page flags; Stage 10: mapped-mip propagation; Stage 11: selective page initialization.

| Test suite | File | Executable |
| ---------- | ---- | --------- |
| `VsmHierarchicalPageFlagsLiveSceneTest` | `VsmHierarchicalPageFlags_test.cpp` | VsmHierarchicalFlags |
| `VsmMappedMipPropagationLiveSceneTest` | `VsmMappedMipPropagation_test.cpp` | VsmMappedMips |
| `VsmSelectivePageInitializationLiveSceneTest` | `VsmSelectivePageInitialization_test.cpp` | VsmPageInitialization |
| `VsmPageFlagPropagationGpuTest` | `VsmPageLifecyclePasses_test.cpp` | VirtualShadowGpuLifecycle |

### Stage 12 — Shadow Rasterization

GPU-driven per-page shadow depth rendering with culling.

| Test suite | File | Executable |
| ---------- | ---- | --------- |
| `VsmShadowRasterPageJobsLiveSceneTest` | `VsmShadowRasterJobs_test.cpp` | VsmShadowRasterization |
| `VsmShadowRasterizationLiveSceneTest` | `VsmShadowRasterizationLiveScene_test.cpp` | VsmShadowRasterization |
| `VsmShadowRasterizerPassGpuTest` | `VsmShadowRasterizerPass_test.cpp` | VsmShadowRasterization |

`VsmShadowRendererBridgeGpuTest` in `VirtualShadowGpuLifecycle` remains supporting downstream
integration coverage. It is not the Stage 12 ownership proof.

### Stage 13 — Static/Dynamic Merge

Dirty pages are composited from the static slice into the dynamic slice.

| Test suite | File | Executable |
| ---------- | ---- | --------- |
| `VsmStaticDynamicMergeLiveSceneTest` | `VsmStaticDynamicMergeLiveScene_test.cpp` | VsmStaticDynamicMerge |
| `VsmStaticDynamicMergeLocalLiveSceneTest` | `VsmStaticDynamicMergePass_test.cpp` | VsmStaticDynamicMerge |

`VirtualShadowGpuLifecycle` no longer owns Stage 13. Stage 13 proof now lives in the
dedicated `VsmStaticDynamicMerge` executable and covers:
- stable directional and local fully reused frames that must leave continuity intact
- dynamic-only invalidation where dirty pages must satisfy `dynamic_after = min(dynamic_before, static_before)`
- static-invalidated directional and local frames where dirty pages must leave the dynamic slice unchanged

### Stage 14 — HZB Update

Per-page shadow-space HZB mip chain is rebuilt from real Stage 13 shadow depth for selected
dynamic pages. Stage 14 clears the remaining rebuild-selection state (`dirty` and `view_uncached`)
after selection; invalidation flags are already consumed earlier when raster results are published.

| Test suite | File | Executable |
| ---------- | ---- | --------- |
| `VsmShadowHzbLiveSceneTest` | `VsmShadowHzb_test.cpp` | `VsmShadowHzb` |

### Stage 15 — Projection and Composite

Screen-space shadow factors are generated for directional and local lights.

| Test suite | File | Executable |
| ---------- | ---- | --------- |
| `VsmShadowProjectionLiveSceneTest` | `VsmShadowProjection_test.cpp` | VsmShadowProjection |

`VirtualShadowGpuLifecycle` no longer owns Stage 15 analytic/mask correctness. Stage 15 proof now
lives in the dedicated `VsmShadowProjection` executable; the lifecycle binary keeps only
supporting bridge/orchestration coverage.

### Stage 16 — Frame Extraction

The extracted ready frame is finalized and published for Stage 17 and next-frame reuse. Stage 16
must prove real extracted page-table, physical-page metadata, projection records, visible
primitive continuity, static feedback retention, and next-frame finalization of queued readback.

| Test suite | File | Executable |
| ---------- | ---- | --------- |
| `VsmFrameExtractionLiveSceneTest` | `VsmFrameExtraction_test.cpp` | VsmFrameExtraction |

### Stage 17 — Cache Validity

The extracted frame becomes globally reusable only after Stage 16 completes. Stage 17 must prove
that cache validity is not published early, that it enables next-frame reuse once extraction
finishes, that explicit invalidation clears reuse validity until a fresh extraction restores it,
and that the same contract holds for paged local lights.

| Test suite | File | Executable |
| ---------- | ---- | --------- |
| `VsmCacheValidityLiveSceneTest` | `VsmCacheValidity_test.cpp` | VsmCacheValidity |

### Cross-cutting — Basic Type Contracts

Tests the shared DTO and value-type layer in `VsmCacheManagerTypes.h`. Exercises types across all pipeline stages but has no runtime dependency on the cache manager or any backend.

All tests live in the dedicated `VsmBasic` CMake program, which produces the `Oxygen.Renderer.VsmBasic.Tests.exe` binary.

| Test suite | File | What it covers |
| ---------- | ---- | -------------- |
| `VsmCacheManagerTypesTest` | `VsmCacheManagerTypes_test.cpp` | Copy-constructibility of all cache-manager value types; `to_string` surface of every enum; config-must-not-mirror-seam domain-ownership contract; `IsValid`/`Validate` on `VsmPageRequest` and `VsmRemapKeyList`; field round-trip fidelity for `VsmPageAllocationPlan`, `VsmPageAllocationSnapshot`, `VsmLightCacheEntryState`, `VsmPhysicalPageMeta`, and related types |
| `VsmPageRequestPolicyTest` | `VsmPageRequestPolicy_test.cpp` | CPU-side request routing/merging helper coverage: fine/coarse request policy, light-grid pruning policy, malformed-projection rejection, and projection-routing edge cases. This is supporting helper coverage, not Stage 5 functional ownership. |
| `VsmVirtualAddressSpaceTypesTest` | `VsmVirtualAddressSpaceTypes_test.cpp` | Supporting type-contract coverage for virtual-layout value types and stable `VsmReuseRejectionReason` string surfaces; this is helper coverage, not Stage 2 functional proof |

### Cross-cutting — Shader ABI Contracts

These tests validate shared shader ABI bit layouts and value packing. They support the stage suites, but they are not Stage 4 functional proof.

| Test suite | File | Executable |
| ---------- | ---- | --------- |
| `VsmShaderTypesTest` | `VsmShaderTypes_test.cpp` | VirtualShadows |

### Cross-cutting — Physical Pool Contracts

These suites validate physical-pool layout, addressing, and compatibility contracts. They are foundational, but they are not Stage 2 virtual-address-allocation proof.

| Test suite | File | Executable |
| ---------- | ---- | --------- |
| `VsmPhysicalPoolCompatibilityTest` | `VsmPhysicalPoolCompatibility_test.cpp` | VirtualShadows |
| `VsmPhysicalPoolAddressingTest` | `VsmPhysicalPoolAddressing_test.cpp` | VirtualShadows |
| `VsmPhysicalPagePoolManagerTest` | `VsmPhysicalPagePoolManager_test.cpp` | VirtualShadows |

### Cross-cutting — Cache Manager Lifecycle and Invalidation

These suites exercise cache state transitions, orchestration across multiple frames, retention policy, invalidation, and the scene observer pipeline. They do not map to a single stage.

| Test suite | File | Executable |
| ---------- | ---- | --------- |
| `VsmCacheManagerOrchestrationTest` | `VsmCacheManagerOrchestration_test.cpp` | VirtualShadows |
| `VsmCacheManagerRetentionTest` | `VsmCacheManagerRetention_test.cpp` | VirtualShadows |
| `VsmCacheManagerInvalidationTest` | `VsmCacheManagerInvalidation_test.cpp` | VirtualShadows |
| `VsmInvalidationPassGpuTest` | `VsmInvalidationPass_test.cpp` | VirtualShadowGpuLifecycle |
| `VsmSceneInvalidationCollectorTest` | `VsmSceneInvalidationCollector_test.cpp` | VirtualShadows |
| `VsmSceneObserverIntegrationTest` | `VsmSceneObserverIntegration_test.cpp` | VirtualShadowSceneObserver |

---

## Test Infrastructure

### CPU-only fixtures (`VirtualShadowTestFixtures.h`)

| Class | Purpose |
| ----- | ------- |
| `VirtualShadowTest` | Bare `::testing::Test` base; use when you need no infrastructure at all |
| `VsmPhysicalPoolTestBase` | Provides `MakeShadowPoolConfig()` and `MakeHzbPoolConfig()` factory helpers |
| `VsmCacheManagerTestBase` | Extends `VsmPhysicalPoolTestBase`; adds `MakeSinglePageLocalFrame()`, `MakeSeam()`, and `ExtractReadyFrame()` for assembling cache-manager inputs without writing the plumbing every time |

All three are header-only, no backend dependency, no D3D12.

### CPU stage harness (`VirtualShadowStageCpuHarness.h`)

`VsmStageCpuHarness` extends `VsmCacheManagerTestBase` and adds richer frame assembly:

- `VirtualShadowLiveSceneHarness.h` — shared live-scene harness for the dedicated Stage 1-11 executables; builds the real two-box scene, attaches directional or spot lights with the engine’s `oxygen::space::move::Forward` basis, prepares the per-frame renderer data, exposes real depth-sample readback helpers, and executes the live shell or Stage 5-11 bridge up to the point each suite needs to inspect.
- `MakeFrame()` — builds a mixed `VsmVirtualAddressSpaceFrame` from directional and local-light descriptors so CPU stage suites can assert the published frame contract directly.
- `MakeLocalFrame()` — builds a `VsmVirtualAddressSpaceFrame` from a list of `LocalStageLightSpec` descriptors (mix of single-page and multi-level local lights).
- `MakeDirectionalFrame()` — builds a directional clipmap frame from `DirectionalStageClipmapSpec` descriptors.
- `MakeSinglePageRequest()` / `ResolveLocalEntryIndex()` / `ResolveDirectionalEntryIndex()` — convenience accessors for request construction and page-table entry assertions by virtual coordinate.
- `MakeProjectionRecord()` — constructs a `VsmPageRequestProjection` with identity matrices for CPU-side assertion tests.

Use this harness for any test that needs to drive the cache manager with varied multi-light configurations without constructing address-space frames manually.

### GPU fixture base (`VirtualShadowGpuTestFixtures.h`)

| Class | Purpose |
| ----- | ------- |
| `VirtualShadowGpuTest` | Extends `ReadbackTestFixture`; locates the workspace root for shader path resolution, provides `MakeShadowPoolConfig()`, `MakeHzbPoolConfig()`, `MakeRendererConfig()`, `MakeRenderer()`, `RunPass()`, and `ReadBufferBytes()` |
| `VsmCacheManagerGpuTestBase` | Extends `VirtualShadowGpuTest`; adds `MakeSinglePageLocalFrame()` and `MakeSeam()` helpers for building cache-manager inputs in GPU test contexts |

`RunPass()` drives a render pass through the `TestEventLoop` coroutine executor, which is the same mechanism the live renderer uses. Do not bypass it.

Shader path resolution is automatic: the fixture walks up from `current_path()` looking for `src/Oxygen/Graphics/Direct3D12/Shaders/EngineShaderCatalog.h`. The test must therefore be run from the build output directory (the default CTest working directory).

### GPU stage harness (`VirtualShadowStageGpuHarness.h`)

`VsmStageGpuHarness` extends `VsmCacheManagerGpuTestBase` and provides:

- `MakeCustomLocalFrame()` / `MakeMultiLevelLocalFrame()` — address-space frame helpers for GPU test scenarios.
- `MakePageRequests()` — builds `VsmPageRequest` vectors from virtual page coordinates for paged-light scenarios.
- `ResolvePageTableEntryIndex()` — resolves page-table slots from a layout and virtual page coordinate so tests assert by coordinate instead of hardcoded slot number.
- `ExecutePageManagementPass()` — full pipeline helper that creates a renderer, begins an offscreen frame, executes the pass, waits for idle, and returns the result buffer.
- `ExecutePropagationPass()` — same pattern for flag propagation passes.
- `MakeResolvedView()` — constructs a `ResolvedView` with a configurable viewport size.
- `MakeSingleSliceShadowPoolConfig()` — single-slice pool config for simpler GPU tests.
- `AlignUp()` — alignment utility for readback buffer sizing.

---

## Writing New Tests

### Choose the right base class

| Scenario | Base class |
| -------- | --------- |
| Pure logic on types, no GPU, no cache | `VirtualShadowTest` |
| Physical pool config construction | `VsmPhysicalPoolTestBase` |
| Cache manager with minimal frame data | `VsmCacheManagerTestBase` |
| Cache manager with varied multi-light frames | `VsmStageCpuHarness` |
| GPU stage-owned pass with shared multi-page setup and readback helpers | `VsmStageGpuHarness` |
| GPU pass with full cache-manager lifecycle | `VsmCacheManagerGpuTestBase` |

### CPU test conventions

- Use `NOLINT_TEST_F(SuiteTest, DescriptiveName)` rather than `TEST_F`. The project-wide `NOLINT_TEST_F` macro is required for NOLINT suppression consistency.
- Name tests after what the system should do, not what code path is taken: `RetainsPageAcrossFrameWhenNotInvalidated`, not `TestCacheRetention`.
- Keep each test's arrange/act/assert scope tight. Do not share mutable state between tests; declare it as a local variable inside the test body.
- When you need a `VsmCacheManager`, pass `nullptr` for the `Graphics*` argument in CPU tests — the manager only dereferences the backend during GPU pass execution, which CPU tests never trigger.
- Never call `ExtractReadyFrame` in a loop across tests. Each test must be self-contained and independent of test execution order.

### GPU test conventions

- Always call `WaitForQueueIdle()` after submitting GPU work before reading back results. The harness helpers do this for you; if you bypass them you must call it explicitly.
- Keep GPU tests focused on one observable behavior per test. GPU tests are slow and isolating failures to a single behavior is worth the extra setup verbosity.
- Use `MakeResolvedView(width, height)` to match the depth texture dimensions you supply. Mismatched dimensions produce TDRs that are hard to attribute.
- Call `ASSERT_*` rather than `EXPECT_*` before any GPU submission: an invalid input that reaches the GPU will produce a device-removed error that pollutes the entire test process.
- Use `constexpr` constants for physical tile capacities and page sizes to make it obvious when tests are using a reduced-size pool versus a realistic one.

### Adding a new test file

1. Add the `.cpp` file to the correct `m_gtest_program` block in `src/Oxygen/Renderer/Test/CMakeLists.txt`.
   - Stage 1 (`BeginFrame`) tests belong in the `VsmBeginFrame` block.
   - Stage 2 virtual-address-allocation tests belong in the `VsmVirtualAddressSpace` block.
   - Stage 3 remap-construction tests belong in the `VsmRemap` block.
   - Stage 4 projection-record publication tests belong in the `VsmProjectionRecords` block.
   - Stage 5 request-generation tests belong in the `VsmPageRequests` block.
   - Stage 6 physical-page reuse tests belong in the `VsmPageReuse` block.
   - Stage 7 available-page packing tests belong in the `VsmAvailablePages` block.
   - Stage 8 new-page-mapping tests belong in the `VsmPageMappings` block.
   - Stage 9 hierarchical-flag tests belong in the `VsmHierarchicalFlags` block.
   - Stage 10 mapped-mip tests belong in the `VsmMappedMips` block.
   - Stage 11 selective-page-initialization tests belong in the `VsmPageInitialization` block.
   - All other CPU-only tests belong in the `VirtualShadows` block.
   - Supporting GPU integration and non-stage-owned pass tests belong in the `VirtualShadowGpuLifecycle` block (inside the `if(TARGET oxygen::graphics-direct3d12)` guard).
2. Include only what the test actually uses. The harness headers already pull in the heavy GPU and renderer headers; do not re-include them.
3. Place the test class and all test bodies inside an anonymous `namespace {}` to avoid ODR collisions across translation units.
4. Use the `using` declarations pattern established in existing files — declare all necessary types at the top of the anonymous namespace rather than spelling out full paths in each test.

---

## Pitfalls to Avoid

**Sharing pool managers or cache managers across tests.** Each test must construct its own. The pool manager owns D3D12 resource lifetime; sharing it across tests causes use-after-free when the first test's teardown releases resources still referenced by the second.

**Calling `BuildPageAllocationPlan()` without a preceding `BeginFrame()`.** The cache manager's state machine will `CHECK`-fail. Always follow the documented call sequence: `BeginFrame → BuildPageAllocationPlan → CommitPageAllocationFrame → ExtractFrameData`.

**Passing an empty remap table when a previous frame is expected.** An empty `VsmVirtualRemapTable` is valid; it means no lights survived from the previous frame. If your test intends to exercise reuse, verify that `BuildVirtualRemapTable` produced a non-empty table before asserting reuse occurred.

**Constructing address-space frames with `first_virtual_id = 0`.** Virtual ID 0 is reserved as an invalid sentinel. Always pass a non-zero starting ID. Tests that accidentally use ID 0 may appear to pass while exercising silent no-op code paths.

**GPU tests that do not call `WaitForQueueIdle`.** GPU readback without a queue idle fence will read stale or uninitialized data while the test reports a spurious pass. Use the harness helpers; they call `WaitForQueueIdle` for you.

**Naming files or fixtures after architecture stage numbers.** Do not encode stage numbers or temporary phase labels into file names or fixture names. Use concise semantic names tied to behavior or ownership. Stage numbers are fine in README prose and architecture references, but they are not the naming scheme for the test code itself.

**Bypassing `RunPass` for GPU passes.** The `RunPass` helper drives passes through the same `TestEventLoop` + `co::Run` path the live renderer uses. Calling `PrepareResources` and `Execute` directly (without the coroutine executor) will deadlock or produce undefined behavior on passes that schedule internal coroutines.

---

## Troubleshooting D3D12 Errors

### Device removed / TDR

When a GPU test crashes with `DXGI_ERROR_DEVICE_REMOVED` or `E_FAIL` from a D3D12 API call:

1. Re-run the failing test with `-v 9` to get full trace output including pass setup, resource transitions, and draw/dispatch arguments.
2. Check DRED breadcrumbs. `VsmShadowRendererBridge_test.cpp` contains a reference `LogDredReport(device)` helper that queries `ID3D12DeviceRemovedExtendedData1`. If a test crashes with device removed and does not already call `LogDredReport`, add the call in the `GTEST_SKIP` / failure path to get the breadcrumb chain.
3. Look for mismatched resource states. The most common cause is a buffer submitted as an SRV while it is still in `D3D12_RESOURCE_STATE_UNORDERED_ACCESS` from a preceding dispatch. Check every resource barrier around the failing pass.
4. Check `page_size_texels` and `physical_tile_capacity` against shader constant assumptions. If the test uses a reduced pool (e.g., capacity 16) but the shader assumes a minimum of 64, the compute grid will write out of bounds.

### Validation layer errors

The D3D12 debug layer is enabled by setting `enable_debug_layer = true` in `GraphicsConfig` when constructing the backend. GPU test fixtures that need it must override `BackendConfigJson()` to return JSON with `"enable_debug_layer": true`. Validation errors are printed to the test output stream via the DXGI info queue. Address them before investigating TDRs — many TDRs are caused by a prior validation error that went unnoticed.

### Shader not found at runtime

GPU tests require the compiled shader binary (`shaders.bin`) to be reachable via the workspace path finder. If you see `Failed to locate Oxygen workspace root` in the test output, either:

- the test is not being run from the build output directory (check `WORKING_DIRECTORY` in CMake), or
- `shaders.bin` is stale and needs to be rebuilt: `cmake --build out/build-ninja --config Debug --target oxygen-graphics-direct3d12_shaders`.

### Incorrect readback values

Before concluding a shader is wrong:

1. Verify `AlignUp` is applied correctly to the readback buffer size — partial-row uploads on D3D12 require 256-byte row alignment and reading past the aligned boundary returns garbage.
2. Confirm the buffer was transitioned to `D3D12_RESOURCE_STATE_COPY_SOURCE` before the readback copy and back to the appropriate state afterward.
3. Check that the `size_bytes` passed to `ReadBufferBytes` matches `sizeof(element) * count` for the exact struct used by the shader, not the CPU-side struct (they must match, but verify it with static assertions in the test).
