# VirtualShadowMaps

This folder contains the greenfield low-level VSM module. It is intentionally separate from the active renderer shadow backend, which remains the conventional directional shadow path.

## Ownership Split

- `VsmPhysicalPagePool*`: persistent physical pool state, compatibility, snapshots, and GPU resource lifetime.
- `VsmVirtualAddressSpace*`: frame-local virtual layouts, clipmaps, remap products, and layout math.
- `VsmCacheManager*`: cross-frame cache state, planner orchestration, retained-entry continuity publication, targeted invalidation intake, and current-frame working-set publication through `VsmCacheManagerSeam`.
- `VsmCacheManagerTypes.*`: shared cache-manager state/config enums, allocation/invalidation contracts, and explicit initialization-work products.
- `VsmCacheManagerSeam.h`: the stable package a future cache manager will consume.

## Current Status

- The greenfield VSM cache/page-allocation slice is implemented through the current hardening phase:
  - explicit cache/build state machines
  - deterministic CPU allocation planning
  - backend-backed working-set resource publication
  - retained unreferenced-entry continuity publication
  - scoped targeted invalidation and explicit initialization work
  - GPU page-management execution for stages 6-8 through `VsmPageManagementPass`
  - GPU page lifecycle execution for stages 9-11 through `VsmPageFlagPropagationPass` and `VsmPageInitializationPass`
  - GPU static/dynamic depth merge for stage 13 through `VsmStaticDynamicMergePass`
  - GPU per-page and top-level VSM HZB rebuild for stage 14 through `VsmHzbUpdaterPass`
  - standalone Stage 15 screen-space shadow projection/composite through `VsmProjectionPass`
  - deterministic available-page packing so GPU fresh allocation matches CPU plan order
- renderer-level screen-space HZB prep is implemented through `ScreenHzbBuildPass`
  - `ForwardPipeline` now builds a per-view min-reduced screen HZB immediately after `DepthPrePass`
  - the pass retains previous-frame HZB history for the next frame's Phase F instance culling inputs
- shader ABI contracts for page-table encoding, virtual page flags, shared physical metadata, and projection payloads
  - shared physical metadata uses `oxygen::Bool32` for explicit shader-ABI boolean semantics rather than raw integer flags
- standalone Phase C request-generation contracts now exist:
  - `VsmPageRequestProjection` / `VsmShaderPageRequestFlags` define the shader ABI for Stage 5 demand discovery
  - `VsmPageRequestGeneratorPass` owns the request/projection GPU buffers and dispatch contract
  - `VsmPageRequestGeneration.*` keeps the projection/request-merging policy independently testable on CPU
- standalone Phase I projection/composite contracts now exist:
  - `VsmCacheManager::PublishProjectionRecords(...)` publishes current-frame projection records on the committed cache frame and retains them on extraction
  - `VsmProjectionPass` projects directional and local-light VSM pages into a per-view screen-space shadow mask
  - `VsmShadowHelpers.hlsli` owns the Stage 15 page-table projection/sampling helpers used by the projection shaders
- standalone Phase J scene invalidation contracts now exist:
  - `VsmSceneInvalidationCoordinator` binds directly to the active `Scene` and rebinds cleanly across scene switches without modifying `SceneObserverSyncModule`
  - `VsmSceneInvalidationCollector` translates scene mutations into explicit light-request and primitive-invalidation frame inputs
  - `VsmCacheManager::BuildInvalidationWork(...)` merges scene-driven primitive invalidations with retained primitive history and static raster feedback
  - `VsmInvalidationPass` executes the dedicated GPU invalidation stage against previous-frame page tables and physical metadata
- stage-suite refactor is in progress:
  - shared harnesses now live in `src/Oxygen/Renderer/Test/VirtualShadow/VirtualShadowStageCpuHarness.h`
    and `src/Oxygen/Renderer/Test/VirtualShadow/VirtualShadowStageGpuHarness.h`
  - Stage 2 virtual-address-allocation coverage now lives in the dedicated
    `Oxygen.Renderer.VsmVirtualAddressSpace.Tests` program so the Stage 2 contract coverage is
    isolated from helper math and cross-cutting CPU coverage
  - Stage 3 remap-construction coverage now lives in the dedicated
    `Oxygen.Renderer.VsmRemap.Tests` program so stable-key matching, previous-driven remap
    cardinality, directional clipmap pan offsets, and explicit rejection contracts are isolated
    from later CPU cache/planner coverage
  - Stage 4 projection-record publication coverage now lives in the dedicated
    `Oxygen.Renderer.VsmProjectionRecords.Tests` program so real-scene directional clipmap and
    paged local-spot projection publication is isolated from cache lifecycle helpers and shader ABI
    checks
  - Stage 5 request-generation coverage now lives in the dedicated
    `Oxygen.Renderer.VsmPageRequests.Tests` program so real-data request-flag validation is
    isolated from later GPU lifecycle stages and from the CPU helper-policy coverage in
    `Oxygen.Renderer.VsmBasic.Tests`
  - Stage 6 physical-page reuse coverage now lives in the dedicated
    `Oxygen.Renderer.VsmPageReuse.Tests` program so real-scene reuse contracts are isolated from
    the later GPU lifecycle suites that still own stages 8-15
  - Stage 7 available-page packing coverage now lives in the dedicated
    `Oxygen.Renderer.VsmAvailablePages.Tests` program so real-scene packing contracts are isolated
    from the later GPU lifecycle suites that still own stages 8-15
  - Stage 8 new-page-mapping coverage now lives in the dedicated
    `Oxygen.Renderer.VsmPageMappings.Tests` program so real-scene allocation contracts are
    isolated from the later GPU lifecycle suites that still own stages 9-15
  - Stage 9 hierarchical-flag coverage now lives in the dedicated
    `Oxygen.Renderer.VsmHierarchicalFlags.Tests` program so real-scene flag propagation contracts
    are isolated from the later GPU lifecycle suites that still own stages 10-15
  - Stage 10 mapped-mip coverage now lives in the dedicated
    `Oxygen.Renderer.VsmMappedMips.Tests` program so real-scene mapped-descendant propagation
    contracts are isolated from the later GPU lifecycle suites that still own stages 12-15
  - Stage 11 selective-page-initialization coverage now lives in the dedicated
    `Oxygen.Renderer.VsmPageInitialization.Tests` program so real-scene initialization contracts
    are isolated from the later GPU lifecycle suites that still own stages 12-15
  - the shared CPU harness now exposes `MakeFrame(...)`, `ResolveLocalEntryIndex(...)`, and
    `ResolveDirectionalEntryIndex(...)` so Stage 2 suites assert mixed directional/local layout
    publication from real inputs instead of ad hoc setup or magic slot numbers
  - the Stage 3 suites now build their primary inputs through `MakeFrame(...)`,
    `MakeLocalFrame(...)`, and `MakeDirectionalFrame(...)`; only malformed-input checks mutate the
    published snapshots after real construction
  - the shared live-scene harness now lives in
    `src/Oxygen/Renderer/Test/VirtualShadow/VirtualShadowLiveSceneHarness.h` and drives real
    two-box lighting scenes through the dedicated Stage 1-11 executables
  - the shared live-scene harness now aligns light targeting with the engine's
    `oxygen::space::move::Forward` transform basis and exposes real depth-sample readback helpers
    so local-light live-scene suites are validated against the engine's actual geometry and view
    conventions rather than ad hoc target math
  - the dedicated Stage 1-4 executables now each include live real-scene validation:
    Stage 1 checks frame-start/reset behavior against extracted real-scene history; Stage 2 checks
    multi-page directional clipmap publication from the real scene; Stage 3 checks exact
    previous-driven clipmap remap offsets across a real camera pan; Stage 4 checks scene-derived
    projection-record publication for directional clipmaps and paged local spot lights
  - dedicated semantic suites now cover stages 1-17 at the stage-owned level; stages 5 and 9-15
    now execute through the shared GPU harness path or shared GPU harness helpers instead of
    standalone fixture-only setup or hardcoded page-table slots
  - the shared GPU harness now exposes `MakePageRequests(...)` and
    `ResolvePageTableEntryIndex(...)` so paged-light stage suites can assemble real multi-page
    inputs and assert by virtual coordinate instead of by magic slot number
  - the Stage 5 dedicated suite now reuses the shared live-scene and GPU harnesses:
    `VsmPageRequestLiveSceneTest` validates a real two-box directional live-shell request-flag
    regression, and the same executable also validates multi-level local fine/coarse requests and
    directional clip-level requests from real depth fields
  - the Stage 11 dedicated suite now proves selective initialization from real live-scene inputs
    through `VsmSelectivePageInitializationLiveSceneTest.*`, covering stable cached frames that
    must remain untouched, clipmap-pan fresh pages that must be depth-cleared, and dynamic-only
    invalidation pages that must copy the static slice into the dynamic slice
  - `VsmCacheManager` now queues GPU-backed frame extraction and finalizes the readback at the
    next `BeginFrame()` so Stage 11 sees the real previous-frame physical-page metadata instead of
    stale CPU-only snapshot state; this fixes the stable-cached-frame false reinitialization bug
  - historical evidence before the Stage 10 executable split:
    `VsmMappedMipPropagationTest.MarksMappedDescendantsAcrossRequestedLeafAndParentPages`
    passed in the lifecycle binary and originally proved mapped-descendant propagation across a
    three-level paged-light scenario with two requested leaf pages and one directly requested
    parent page
  - the Stage 9 dedicated suite now proves hierarchical allocated/dynamic-uncached/static-uncached
    propagation from real Stage 8 outputs through
    `VsmHierarchicalPageFlagsLiveSceneTest.*`
  - the Stage 9 dedicated suite currently validates exact hierarchical propagation for the real
    Stage 8 leaf-state bits the runtime produces today; `detail_geometry` still has no real-input
    producer in Oxygen, so that specific flag is not yet exercised by a real-scene regression
  - the Stage 10 dedicated suite now proves mapped-descendant propagation from real Stage 8 page
    tables and flags through `VsmMappedMipPropagationLiveSceneTest.*`, while also asserting that
    the propagation pass leaves the page table unchanged and matches a CPU model across
    directional, mixed directional-plus-local, mixed-local, reuse-only, and invalidated-refresh
    scenes
  - Stage 12 rasterization coverage now lives in the dedicated
    `Oxygen.Renderer.VsmShadowRasterization.Tests` program so real Stage 11→12 live-scene page-job
    construction and current-frame raster outputs are validated at the Stage 12 boundary; direct
    pass coverage in the same executable still owns static-only slice routing, reveal forcing,
    point-light face routing, and previous-frame HZB culling
  - the Stage 12 rasterization suite now consumes the shared GPU harness buffer, texture, mip
    readback, and raw-buffer readback helpers instead of carrying a private duplicate fixture layer
  - a later `2026-03-28` Stage 12 hardening pass fixed a real engine-side offscreen-view lifetime
    bug: `Renderer::OffscreenFrameSession::SetCurrentView(...)` had been storing raw pointers to
    caller-owned `ResolvedView` temporaries, which let `VsmProjectionPass` resume against dead
    stack memory in the localized directional-mask raster/projection regression; the offscreen
    session now owns the active resolved/prepared view snapshots and rebinds them after moves
  - the Stage 15 dedicated suite now includes a rasterized multi-page real-geometry proof through
    `VsmProjectionPassGpuTest.DirectionalProjectionPassCompositesRasterizedMultiPageShadowMaskFromRealGeometry`
  - the Stage 14 dedicated suite now includes a rasterized multi-page real-geometry proof through
    `VsmHzbUpdaterPassGpuTest.RebuildsDirtyPageMipsFromRasterizedMultiPageDirectionalScene`
  - renderer test `CMakeLists.txt` now uses logical target names
    `VsmVirtualAddressSpace`, `VsmRemap`, `VsmProjectionRecords`, `VsmPageRequests`,
    `VsmPageReuse`, `VsmAvailablePages`, `VsmPageMappings`, `VsmHierarchicalFlags`,
    `VsmMappedMips`, `VsmPageInitialization`,
    `VirtualShadows`, and `VirtualShadowGpuLifecycle`; `m_gtest_program(...)` expands them to
    `Oxygen.Renderer.VsmVirtualAddressSpace.Tests`,
    `Oxygen.Renderer.VsmRemap.Tests`,
    `Oxygen.Renderer.VsmProjectionRecords.Tests`,
    `Oxygen.Renderer.VsmPageRequests.Tests`,
    `Oxygen.Renderer.VsmPageReuse.Tests`,
    `Oxygen.Renderer.VsmAvailablePages.Tests`,
    `Oxygen.Renderer.VsmPageMappings.Tests`,
    `Oxygen.Renderer.VsmHierarchicalFlags.Tests`,
    `Oxygen.Renderer.VsmMappedMips.Tests`,
    `Oxygen.Renderer.VsmPageInitialization.Tests`,
    `Oxygen.Renderer.VirtualShadows.Tests`, and
    `Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests`
- Frequently run coverage lives under `Oxygen.Renderer.VsmVirtualAddressSpace.Tests`,
  `Oxygen.Renderer.VsmRemap.Tests`, `Oxygen.Renderer.VsmProjectionRecords.Tests`,
  `Oxygen.Renderer.VsmPageRequests.Tests`, `Oxygen.Renderer.VsmPageReuse.Tests`,
  `Oxygen.Renderer.VsmAvailablePages.Tests`, `Oxygen.Renderer.VsmPageMappings.Tests`,
  `Oxygen.Renderer.VsmHierarchicalFlags.Tests`,
  `Oxygen.Renderer.VsmMappedMips.Tests`,
  `Oxygen.Renderer.VsmPageInitialization.Tests`,
  `Oxygen.Renderer.VirtualShadows.Tests`, and `Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests`.
- Additional backend-backed integration coverage lives under `Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests`.
  - that integration bucket now covers physical-pool ABI publication, cache-resource publication, propagation smoke, invalidation readback contracts, static/dynamic merge readback contracts, VSM HZB update readback contracts, Stage 15 projection readback contracts, bridge/orchestrator behavior, and screen-HZB history/readback contracts
  - validation snapshot on `2026-03-27`:
    - `Oxygen.Renderer.VsmBeginFrame.Tests` passes with `33 tests from 4 test suites`
    - `Oxygen.Renderer.VsmVirtualAddressSpace.Tests` passes with `8 tests from 4 test suites`
    - `Oxygen.Renderer.VsmRemap.Tests` passes with `29 tests from 5 test suites`
    - `Oxygen.Renderer.VsmProjectionRecords.Tests` passes with `2 tests from 1 test suite`
    - `Oxygen.Renderer.VsmPageRequests.Tests` passes with `3 tests from 1 test suite`
    - `Oxygen.Renderer.VsmPageReuse.Tests` passes with `4 tests from 1 test suite`
    - `Oxygen.Renderer.VsmAvailablePages.Tests` passes with `3 tests from 1 test suite`
    - `Oxygen.Renderer.VsmPageMappings.Tests` passes with `3 tests from 1 test suite`
    - `Oxygen.Renderer.VsmHierarchicalFlags.Tests` passes with `3 tests from 1 test suite`
    - `Oxygen.Renderer.VsmMappedMips.Tests` passes with `5 tests from 1 test suite`
    - `Oxygen.Renderer.VsmPageInitialization.Tests` passes with `3 tests from 1 test suite`
    - `VsmVirtualAddressSpaceTypesTest.*` passes in `Oxygen.Renderer.VsmBasic.Tests` with
      `2 tests from 1 test suite`
    - `VsmPageRequestPolicyTest.*` passes in `Oxygen.Renderer.VsmBasic.Tests` with
      `7 tests from 1 test suite`
    - `Oxygen.Renderer.VirtualShadows.Tests` passes with `63 tests from 14 test suites`
    - focused stage-owned suites pass for page reuse/packing/allocation,
      hierarchical page flags, mapped-mip propagation, selective initialization, shadow
      rasterization, static/dynamic merge, HZB update, projection/composite, extraction,
      and cache validity
    - `VsmPageFlagPropagationGpuTest.*` now remains as supporting mapped-descendant coverage only;
      Stage 9 and Stage 10 ownership moved into
      `Oxygen.Renderer.VsmHierarchicalFlags.Tests` and
      `Oxygen.Renderer.VsmMappedMips.Tests`
    - the Stage 11 dedicated executable now owns selective initialization through
      `VsmSelectivePageInitializationLiveSceneTest.*`; the previous synthetic
      `VsmPageInitializationGpuTest.*` lifecycle ownership was removed
    - `VsmShadowRasterizerPassGpuTest.*` passes with `7 tests from 1 test suite`
    - `Oxygen.Renderer.VsmShadowRasterization.Tests` now passes with
      `13 tests from 3 test suites`
    - the localized directional-mask raster/projection regression in the Stage 12 executable now
      passes `5/5` repeated executions in `out/build-ninja`
    - after the offscreen-view ownership fix, both
      `VsmProjectionPassGpuTest.DirectionalProjectionPassCompositesRasterizedMultiPageShadowMaskFromRealGeometry`
      and
      `VsmShadowRendererBridgeGpuTest.ExecutePreparedViewShellProjectsLocalizedDirectionalMaskForRasterizedCasters`
      pass again in `out/build-ninja`
    - the bottom-stage GPU filter for request generation plus stages 12-15 passes with
      `29 tests from 5 test suites`, including the Stage 12 shared-harness refactor plus the
      Stage 14 and Stage 15 rasterized multi-page proofs
    - a full rerun of `Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests` now reports
      `46 tests from 9 test suites`, `45 passed / 1 failed`; the only failing case remains the
      analytic-floor bridge test below
    - corrective status update on `2026-03-28`: the reported Stage 6-10 local-light regressions
      were traced to a test-side previous-frame seed bug. Those suites were seeding reuse from an
      early page-request bridge snapshot instead of a real extracted previous frame. The shared
      live-scene harness now seeds those suites through `PrimeTwoBoxExtractedFrame(...)`, which
      runs a real prior frame through the live shell before the Stage 6-10 current frame executes
    - after that harness correction, the dedicated Stage 6-10 executables all pass again in
      `out/build-ninja` as part of a clean Stage 1-11 rerun:
      - `VsmPageReuse`: `4 tests from 1 test suite`
      - `VsmAvailablePages`: `3 tests from 1 test suite`
      - `VsmPageMappings`: `3 tests from 1 test suite`
      - `VsmHierarchicalFlags`: `3 tests from 1 test suite`
      - `VsmMappedMips`: `5 tests from 1 test suite`
      - `VsmPageInitialization`: `3 tests from 1 test suite`
    - a later `2026-03-28` live-scene timing fix then changed the shared harness to rotate
      sequential offscreen frames across a 3-slot ring instead of pinning every step to
      `Slot { 0 }`; this removed the test-side inline-staging timing hazard behind the
      `RingBufferStaging.cpp:294` warnings seen in the multi-frame Stage 6-11 suites
    - recorded post-fix stress evidence in `out/build-ninja`:
      - `VsmAvailablePages` specific stable-local-light Stage 7 test passes `10/10` consecutive
        iterations without `-v`
      - `VsmAvailablePages` combined suite passes `3` consecutive repeated runs
      - `VsmHierarchicalFlags` specific mixed-local Stage 9 test passes `10/10` consecutive
        iterations without `-v`
      - `VsmPageInitialization` combined suite passes `3` consecutive repeated runs
    - `out/build-asan-vs` was not rerun after the seed-path correction and therefore remains
      unvalidated here
    - the dedicated Stage 5 live-shell regression now passes after restoring the shared
      two-box depth texture to `Common` between the standalone depth prepass recorder and the live
      shell, then switching the Stage 5 oracle to the actual rasterized depth texture copied into
      an `R32Float` readback surface instead of analytic raycasts
    - `Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests` still has one known live-shell failure in
      `VsmShadowRendererBridgeGpuTest.ExecutePreparedViewShellMatchesAnalyticFloorShadowClassificationForTwoBoxes`;
      after removing the embedded Stage 5 assertions from that bridge test, the remaining failure
      is purely Stage 15: several shadowed floor probes remain lit
    - corrective scope note on `2026-03-28`: despite the focused Stage 12 and Stage 15 passes
      above, a user-provided live renderer capture still shows semantically wrong page-aligned
      floor shadow artifacts, so the broader Stage 12→15 path remains `in_progress`

## Known Forward Gaps

- Phase J is implemented as a standalone invalidation slice, but it is not yet wired into a renderer-owned VSM shadow orchestrator. Phase K-a owns that renderer integration path.
- The page-request generator now has focused off-screen GPU execution coverage, but it is still not wired into the main renderer orchestration path. Phase K-a owns that integration.
- The standalone Stage 15 projection pass now exists, but its shadow-mask output is not yet fully consumed by the normal renderer path. Phase K-b and Phase K-c own that forward-lighting integration.
- Distant-local-light refresh budgeting and point-light per-face update scheduling remain Phase K-d work.
- Translucent-receiver transmission sampling for VSM-projected shadows is not integrated yet. That stays deferred until the renderer path actually consumes the Stage 15 mask.
- Dedicated Phase F coverage already proves previous-frame screen-space HZB consumption during
  instance culling. The remaining Phase F gap is broader live-scene visual validation across the
  Stage 12→15 path, where user-provided evidence still shows incorrect final floor-shadow output.

## Helper Policy

- `VsmPhysicalPageAddressing.*` and `VsmPhysicalPoolCompatibility.*` exist because they carry reusable contract logic.
- `VsmVirtualClipmapHelpers.*` and `VsmVirtualRemapBuilder.*` exist because clipmap reuse and remap construction are separate policy-free helpers.
- New files should only be added when they introduce a clear ownership or dependency boundary.

## Troubleshooting

- Invalid public configs fail fast.
- Reuse rejection reasons are explicit and test-covered.
- Strategic warnings are emitted for malformed frames, malformed layouts, duplicate remap keys, missing remap keys, incompatible pool/snapshot reuse, and rejected targeted invalidation inputs.
