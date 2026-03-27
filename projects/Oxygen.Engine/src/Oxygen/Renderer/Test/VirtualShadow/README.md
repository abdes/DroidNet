# Virtual Shadow Map Tests

Tests for the full VSM pipeline, from CPU cache and address-space logic through all GPU rendering passes. The coverage is currently spread across eight test programs:

- `VsmBasic`
- `VsmBeginFrame`
- `VsmVirtualAddressSpace`
- `VsmRemap`
- `VsmProjectionRecords`
- `VirtualShadows`
- `VirtualShadowGpuLifecycle`
- `Oxygen.Renderer.VirtualShadowSceneObserver.Tests`

Architecture reference: [`design/VirtualShadowMapArchitecture.md`](../../../../../design/VirtualShadowMapArchitecture.md)

## Philosophy

- Stage-owned suites are the primary functional regression gate for stages 1-17.
- Cross-cutting helper and unit suites are secondary contract coverage. They support stage suites, but they do not replace them.
- Stage suites must reuse the shared stage harnesses in `VirtualShadowStageCpuHarness.h` and `VirtualShadowStageGpuHarness.h` instead of rebuilding bespoke setup paths.
- Functional stage suites should prefer multi-page inputs, real geometry, real shadows, and assertions by behavior, virtual coordinate, or physical output rather than magic slot numbers.
- The dedicated Stage 1-4 executables now also reuse `VirtualShadowLiveSceneHarness.h` for live real-scene validation on real geometry, real light data, and multi-page directional or local layouts.
- One-page fixtures and direct slot assertions are acceptable only for narrow ABI checks, malformed-input checks, or other explicitly scoped negative tests.
- In `src/Oxygen/Renderer/Test/CMakeLists.txt`, use the concise `m_gtest_program(...)` names such as `VsmBeginFrame`, `VsmVirtualAddressSpace`, `VsmRemap`, `VirtualShadows`, and `VirtualShadowGpuLifecycle`. The macro generates the fully qualified build target and binary names.
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
| `VirtualShadows` | `Oxygen.Renderer.VirtualShadows.Tests` / `bin/<Config>/Oxygen.Renderer.VirtualShadows.Tests.exe` | CPU-only stage and cross-cutting logic beyond the dedicated early-stage executables: planner, extraction, cache validity, invalidation, orchestration, helper contracts |
| `VirtualShadowGpuLifecycle` | `Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests` / `bin/<Config>/Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests.exe` | GPU-backed stage and integration coverage: request generation, allocation passes, flag propagation, initialization, rasterizer, merge, HZB, projection, bridge, invalidation |
| `Oxygen.Renderer.VirtualShadowSceneObserver.Tests` | `Oxygen.Renderer.VirtualShadowSceneObserver.Tests` / `bin/<Config>/Oxygen.Renderer.VirtualShadowSceneObserver.Tests.exe` | Scene observer → cache manager invalidation integration |

**All manual testing uses the `build-ninja` tree only.** Visual Studio build trees are not used for running tests by hand.

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
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VirtualShadows.Tests.exe
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests.exe
```

### Run a specific test suite

```powershell
.\out\build-ninja\bin\Debug\Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests.exe `
    --gtest_filter="VsmPageRequestGeneratorGpuTest.*"
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
    -R "Oxygen\.Renderer\.(VsmBasic|VsmBeginFrame|VsmVirtualAddressSpace|VsmRemap|VsmProjectionRecords|VirtualShadows|VirtualShadowGpuLifecycle|VirtualShadowSceneObserver)\.Tests"
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

GBuffer is sampled to determine which virtual pages are requested.

| Test suite | File | Executable |
| ---------- | ---- | --------- |
| `VsmPageRequestGenerationTest` | `VsmPageRequestGeneration_test.cpp` | VirtualShadows |
| `VsmPageRequestGeneratorGpuTest` | `VsmPageRequestGeneratorPass_test.cpp` | VirtualShadowGpuLifecycle |

### Stages 6–8 — Physical Page Allocation GPU Passes

Stage 6: reuse previous pages; Stage 7: pack available pages; Stage 8: allocate new mappings.

| Test suite | File | Executable |
| ---------- | ---- | --------- |
| `VsmPhysicalPageReuseTest` | `VsmPhysicalPageReuse_test.cpp` | VirtualShadowGpuLifecycle |
| `VsmAvailablePagePackingTest` | `VsmAvailablePagePacking_test.cpp` | VirtualShadowGpuLifecycle |
| `VsmNewPageMappingTest` | `VsmNewPageMapping_test.cpp` | VirtualShadowGpuLifecycle |
| `VsmPageReuseStageGpuTest` / `VsmPackAvailablePagesGpuTest` / `VsmAllocateNewPagesGpuTest` | `VsmPageManagementPass_test.cpp` | VirtualShadowGpuLifecycle |
| `VsmPhysicalPagePoolGpuLifecycleTest` | `VsmPhysicalPagePoolGpuLifecycle_test.cpp` | VirtualShadowGpuLifecycle |
| `VsmCacheManagerGpuResourcesTest` | `VsmCacheManagerGpuResources_test.cpp` | VirtualShadowGpuLifecycle |
| `VsmPageAllocationPlannerTest` | `VsmPageAllocationPlanner_test.cpp` | VirtualShadows |

### Stages 9–11 — Flag Propagation and Page Initialization

Stage 9: hierarchical page flags; Stage 10: mapped-mip propagation; Stage 11: selective page initialization.

| Test suite | File | Executable |
| ---------- | ---- | --------- |
| `VsmHierarchicalPageFlagsTest` | `VsmHierarchicalPageFlags_test.cpp` | VirtualShadowGpuLifecycle |
| `VsmMappedMipPropagationTest` | `VsmMappedMipPropagation_test.cpp` | VirtualShadowGpuLifecycle |
| `VsmSelectivePageInitializationTest` | `VsmSelectivePageInitialization_test.cpp` | VirtualShadowGpuLifecycle |
| `VsmPageFlagPropagationGpuTest` / `VsmPageInitializationGpuTest` | `VsmPageLifecyclePasses_test.cpp` | VirtualShadowGpuLifecycle |

### Stage 12 — Shadow Rasterization

GPU-driven per-page shadow depth rendering with culling.

| Test suite | File | Executable |
| ---------- | ---- | --------- |
| `VsmShadowRasterJobsTest` | `VsmShadowRasterJobs_test.cpp` | VirtualShadows |
| `VsmShadowRasterizerPassGpuTest` | `VsmShadowRasterizerPass_test.cpp` | VirtualShadowGpuLifecycle |
| `VsmShadowRendererBridgeGpuTest` | `VsmShadowRendererBridge_test.cpp` | VirtualShadowGpuLifecycle |

### Stage 13 — Static/Dynamic Merge

Dirty pages are composited from the static slice into the dynamic slice.

| Test suite | File | Executable |
| ---------- | ---- | --------- |
| `VsmStaticDynamicMergePassGpuTest` | `VsmStaticDynamicMergePass_test.cpp` | VirtualShadowGpuLifecycle |

### Stage 14 — HZB Update

Per-page shadow-space HZB mip chain is rebuilt for dirty and newly allocated pages.

| Test suite | File | Executable |
| ---------- | ---- | --------- |
| `VsmHzbUpdaterPassGpuTest` | `VsmHzbUpdaterPass_test.cpp` | VirtualShadowGpuLifecycle |

### Stage 15 — Projection and Composite

Screen-space shadow factors are generated for directional and local lights.

| Test suite | File | Executable |
| ---------- | ---- | --------- |
| `VsmProjectionPassGpuTest` | `VsmProjectionPass_test.cpp` | VirtualShadowGpuLifecycle |

### Cross-cutting — Basic Type Contracts

Tests the shared DTO and value-type layer in `VsmCacheManagerTypes.h`. Exercises types across all pipeline stages but has no runtime dependency on the cache manager or any backend.

All tests live in the dedicated `VsmBasic` CMake program, which produces the `Oxygen.Renderer.VsmBasic.Tests.exe` binary.

| Test suite | File | What it covers |
| ---------- | ---- | -------------- |
| `VsmCacheManagerTypesTest` | `VsmCacheManagerTypes_test.cpp` | Copy-constructibility of all cache-manager value types; `to_string` surface of every enum; config-must-not-mirror-seam domain-ownership contract; `IsValid`/`Validate` on `VsmPageRequest` and `VsmRemapKeyList`; field round-trip fidelity for `VsmPageAllocationPlan`, `VsmPageAllocationSnapshot`, `VsmLightCacheEntryState`, `VsmPhysicalPageMeta`, and related types |
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
| `VsmCacheValidityTest` | `VsmCacheValidity_test.cpp` | VirtualShadows |
| `VsmCacheManagerOrchestrationTest` | `VsmCacheManagerOrchestration_test.cpp` | VirtualShadows |
| `VsmCacheManagerRetentionTest` | `VsmCacheManagerRetention_test.cpp` | VirtualShadows |
| `VsmCacheManagerInvalidationTest` | `VsmCacheManagerInvalidation_test.cpp` | VirtualShadows |
| `VsmInvalidationPassGpuTest` | `VsmInvalidationPass_test.cpp` | VirtualShadowGpuLifecycle |
| `VsmFrameExtractionTest` | `VsmFrameExtraction_test.cpp` | VirtualShadows |
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

- `VirtualShadowLiveSceneHarness.h` — shared live-scene harness for the dedicated Stage 1-4 executables; builds the real two-box scene, attaches directional or spot lights, prepares the per-frame renderer data, and executes the live shell up to the point each early-stage suite needs to inspect.
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
   - All other CPU-only tests belong in the `VirtualShadows` block.
   - GPU tests belong in the `VirtualShadowGpuLifecycle` block (inside the `if(TARGET oxygen::graphics-direct3d12)` guard).
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

The D3D12 debug layer is enabled by setting `enable_debug = true` in `GraphicsConfig` when constructing the backend. GPU test fixtures that need it must override `BackendConfigJson()` to return JSON with `"enable_debug": true`. Validation errors are printed to the test output stream via the DXGI info queue. Address them before investigating TDRs — many TDRs are caused by a prior validation error that went unnoticed.

### Shader not found at runtime

GPU tests require the compiled shader binary (`shaders.bin`) to be reachable via the workspace path finder. If you see `Failed to locate Oxygen workspace root` in the test output, either:

- the test is not being run from the build output directory (check `WORKING_DIRECTORY` in CMake), or
- `shaders.bin` is stale and needs to be rebuilt: `cmake --build out/build-ninja --config Debug --target oxygen-graphics-direct3d12_shaders`.

### Incorrect readback values

Before concluding a shader is wrong:

1. Verify `AlignUp` is applied correctly to the readback buffer size — partial-row uploads on D3D12 require 256-byte row alignment and reading past the aligned boundary returns garbage.
2. Confirm the buffer was transitioned to `D3D12_RESOURCE_STATE_COPY_SOURCE` before the readback copy and back to the appropriate state afterward.
3. Check that the `size_bytes` passed to `ReadBufferBytes` matches `sizeof(element) * count` for the exact struct used by the shader, not the CPU-side struct (they must match, but verify it with static assertions in the test).
