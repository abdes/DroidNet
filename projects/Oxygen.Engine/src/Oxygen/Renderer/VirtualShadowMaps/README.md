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
- Frequently run coverage lives under `Oxygen.Renderer.VirtualShadows.Tests`.
- Backend-backed dedicated coverage lives under `Oxygen.Renderer.VirtualShadows.GpuLifecycle.Tests`.
  - that dedicated bucket now covers physical-pool ABI publication, request generation, invalidation readback contracts, page-management stage readback contracts, static/dynamic merge readback contracts, VSM HZB update readback contracts, Stage 15 projection readback contracts, and screen-HZB history/readback contracts

## Known Forward Gaps

- Phase J is implemented as a standalone invalidation slice, but it is not yet wired into a renderer-owned VSM shadow orchestrator. Phase K-a owns that renderer integration path.
- The page-request generator now has focused off-screen GPU execution coverage, but it is still not wired into the main renderer orchestration path. Phase K-a owns that integration.
- The standalone Stage 15 projection pass now exists, but its shadow-mask output is not yet fully consumed by the normal renderer path. Phase K-b and Phase K-c own that forward-lighting integration.
- Distant-local-light refresh budgeting and point-light per-face update scheduling remain Phase K-d work.
- Translucent-receiver transmission sampling for VSM-projected shadows is not integrated yet. That stays deferred until the renderer path actually consumes the Stage 15 mask.
- Phase F still needs to consume the screen-space HZB during instance culling. The HZB producer and previous-frame history contract now exist, but the shadow rasterizer does not use them yet.

## Helper Policy

- `VsmPhysicalPageAddressing.*` and `VsmPhysicalPoolCompatibility.*` exist because they carry reusable contract logic.
- `VsmVirtualClipmapHelpers.*` and `VsmVirtualRemapBuilder.*` exist because clipmap reuse and remap construction are separate policy-free helpers.
- New files should only be added when they introduce a clear ownership or dependency boundary.

## Troubleshooting

- Invalid public configs fail fast.
- Reuse rejection reasons are explicit and test-covered.
- Strategic warnings are emitted for malformed frames, malformed layouts, duplicate remap keys, missing remap keys, incompatible pool/snapshot reuse, and rejected targeted invalidation inputs.
