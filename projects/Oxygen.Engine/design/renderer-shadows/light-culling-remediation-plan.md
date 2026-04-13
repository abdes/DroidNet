# Light-Culling Remediation Plan

**Date:** 2026-04-01
**Status:** Design / Implementation Plan

Audience: renderer engineers remediating Oxygen's light-culling path
Scope: replace Oxygen's current depth/HZB-driven `LightCullingPass` with one
final clustered analytic implementation aligned with UE5's non-Lumen
light-grid architecture, then remove the non-final API, shader, DemoShell, and
documentation surfaces around it

This document is intentionally strict. It is not a menu of optional cleanup
ideas. It is the implementation contract for converging `LightCullingPass` on
one shipping design and deleting the rest.

## 0. Current Snapshot (2026-04-01)

Truthful status:

- Overall remediation remains `in_progress`.
- LC-0 through LC-3 are implemented in code.
- LC-4 is still open because
  `src/Oxygen/Renderer/Docs/lighting_overview.md`,
  `src/Oxygen/Renderer/Docs/override_slots.md`,
  `src/Oxygen/Renderer/Docs/passes/design-overview.md`, and
  `src/Oxygen/Renderer/Docs/passes/depth_pre_pass.md` still describe the old
  tile/depth/HZB shape, and `src/Oxygen/Renderer/Docs/passes/light_culling.md`
  does not exist yet.
- LC-5 is still open because the Release benchmark / shipping-configuration
  selection is not recorded yet, and the current RenderScene capture did not
  exercise VSM light-grid pruning.

Current validation evidence:

- Targeted tests passed on 2026-04-01:
  - `Oxygen.Renderer.LightCullingConfig.Tests`: `3/3`
  - `Oxygen.Renderer.LightCullingPassContract.Tests`: `3/3`
  - `Oxygen.Renderer.LightCullingPassMembership.Tests`: `1/1`
- Live runs executed:
  - `RenderScene` debug capture run with explicit repo-local
    `out/build-ninja/bin/Debug/renderdoc.dll`; exit `0`; log at
    `out/build-ninja/analysis/lightculling_validation/renderscene_debug_lightculling_explicit.direct.log`
  - `MultiView` debug run `-v=-1 --frames 80 --fps 0 --debug-layer`; exit `0`;
    log at
    `out/build-ninja/analysis/lightculling_validation/multiview_debug_lightculling.log`
- RenderDoc evidence:
  - capture:
    `out/build-ninja/analysis/lightculling_validation/renderscene_debug_lightculling_explicit_frame30.rdc`
  - pass focus:
    `out/build-ninja/analysis/lightculling_validation/renderscene_debug_lightculling_explicit.pass_focus.txt`
  - pass timing:
    `out/build-ninja/analysis/lightculling_validation/renderscene_debug_lightculling_explicit.pass_timing.txt`
  - detailed light-culling report:
    `out/build-ninja/analysis/lightculling_validation/renderscene_debug_lightculling_explicit.light_culling_report.txt`

Current capture-backed findings:

- `LightCullingPass` dispatch event `327` binds only `Renderer.InlineStaging`
  as compute read-only input and writes `LightCullingPass_ClusterGrid` plus
  `LightCullingPass_LightIndexList`; no scene-depth or HZB bindings were
  observed.
- The captured dispatch ran at `10 x 6 x 8` groups and measured
  `0.081600 ms` in RenderDoc.
- The captured light grid contains `28,160` clusters, `6,619` non-empty
  clusters, `36,974` reported light references, and `31` saturated clusters at
  the inferred `32`-light cap.
- `ShaderPass` draw event `19530` consumes both light-grid buffers in the same
  capture.
- `VsmPageRequestGeneratorPass` dispatch event `54` did not bind the
  light-grid buffers in this capture, so live VSM light-grid pruning is not
  proven by the current RenderScene evidence.
- Old `out/build-ninja/analysis/dp7/*lightculling*` HZB reports are obsolete
  for this remediation and must not be reused as closure evidence.

## 1. Non-Negotiable Decisions

1. `LightCullingPass` ships as one algorithm only: clustered analytic
   light-grid construction.
2. The pass does not read `DepthPrePassOutput`, raw scene depth, scene HZB, or
   `SceneDepthDerivatives`.
3. Scene depth remains a shading-time lookup input only. It is not a build-time
   culling input.
4. No tile-mode, no raw-depth fallback, no HZB fallback, and no parallel legacy
   implementation will remain in-tree.
5. DemoShell keeps only the final light-culling debug visualizations:
   `Heat Map`, `Depth Slice`, and `Cluster Index`.
6. Public runtime tuning for light-culling algorithm shape is removed. No
   scene-level override attachment, no persisted depth-slice slider, no manual
   Z-range UI, no public "cluster mode" switch.
7. Any intentional deviation from the UE reference shape must be benchmarked,
   recorded in this plan, and left as a single shipping path.

## 2. UE5 Reference Anchors

Primary non-Lumen UE references for this work:

- `F:/projects/UnrealEngine2/Engine/Source/Runtime/Renderer/Private/LightGridInjection.cpp`
- `F:/projects/UnrealEngine2/Engine/Shaders/Private/LightGridInjection.usf`
- `F:/projects/UnrealEngine2/Engine/Shaders/Private/LightGridCommon.ush`

The verified UE characteristics that Oxygen must track are:

- light-grid build is analytic, not scene-depth-driven
- clustered grid is engine-owned and narrow in runtime shape
- XY cell bounds come from screen-frustum subdivision, not depth reduction
- Z slices use UE's `LightGridZParams = (B, O, S)` mapping, not a simplified
  `near/far/scale` approximation
- shading uses scene depth only to choose a prebuilt grid cell
- debug visualization is narrow and tied to the final grid, not to alternative
  algorithms

Important UE defaults worth testing against first:

- `LightGridPixelSize = 64`
- `LightGridSizeZ = 32`
- `MaxCulledLightsPerCell = 32`
- `LightGridInjectionGroupSize = 4`

These values are the starting point for Oxygen benchmarking, not an automatic
ship decision. Oxygen must validate the final chosen values on its own scenes.

## 3. Current Oxygen State And Remaining Gaps

Implemented in code:

- `LightCullingPass` now builds one clustered analytic light grid with no
  `DepthPrePass`, raw depth, scene HZB, or `SceneDepthDerivatives` dependency.
- `LightCulling.hlsl` now ships one compute path only: analytic XY cell bounds,
  UE-style `LightGridZParams`, and the existing flat `uint2 + uint` output
  contract.
- `ClusterLookup.hlsli`, `LightCullingConfig.h`, and
  `Renderer/LightCullingConfig.hlsli` now share the final fixed contract:
  engine-owned `64 px / 32 slices / 32 lights-per-cell` with published
  `LightGridZParams`.
- The recorder-state blocker is fixed: `LightCullingPass::PrepareResources()`
  now guards `BeginTrackingResourceState()` with `IsResourceTracked()` checks.
- DemoShell now keeps only the final light-culling visualizations:
  `Heat Map`, `Depth Slice`, and `Cluster Index`.
- Dedicated LightCulling tests now exist for config math, contract/publication,
  and analytic membership correctness.

Remaining gaps:

- Documentation is not closed yet. The repo still contains stale tile/depth/HZB
  LightCulling descriptions, and the authoritative
  `src/Oxygen/Renderer/Docs/passes/light_culling.md` pass doc still needs to
  be written.
- The final shipping configuration is not locked yet by Release benchmarking,
  so this plan cannot truthfully move past `in_progress`.
- The current RenderScene capture proves `ShaderPass` consumption, but it does
  not prove live VSM light-grid pruning because that code path was inactive in
  the captured `VsmPageRequestGeneratorPass` dispatch.

## 4. Final Target Architecture

The final Oxygen shape is:

- one compute pass that consumes view constants, packed local-light buffers,
  fixed shipping grid parameters, and output UAVs
- one clustered grid/list product consumed by `ShaderPass`,
  `TransparentPass`, and existing downstream grid consumers
- one shading-time lookup formula shared by all consumers
- one debug visualization surface set
- zero depth/HZB build-time dependencies

The implementation must follow these design rules:

- keep the current consumer-facing grid/list contract unless measurement proves
  it is the bottleneck
- if Oxygen keeps its flat `uint2 + flat light-index list` storage instead of
  copying UE's linked-list + compact pass, that is acceptable only as a single
  final path and only if benchmarks show it is not the limiting factor
- do not add a second shipping storage format
- do not keep `max_lights_per_cluster` as a public runtime knob
- prefer a power-of-two cell size and publish a cell-size shift or equivalent
  final constant instead of an open-ended tile-size control
- move Oxygen from the current simplified slice mapping to UE-like
  `LightGridZParams`
- if the final chosen grid constants differ from the UE-first candidate, record
  the exact measured reason in this plan before merging

## 5. Implementation Map

Core pass and GPU contract:

- `src/Oxygen/Renderer/Passes/LightCullingPass.h`
- `src/Oxygen/Renderer/Passes/LightCullingPass.cpp`
- `src/Oxygen/Renderer/Types/LightCullingConfig.h`
- `src/Oxygen/Renderer/Types/LightingFrameBindings.h`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Lighting/LightCulling.hlsl`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Lighting/ClusterLookup.hlsli`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/LightCullingConfig.hlsli`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/LightingFrameBindings.hlsli`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/LightingHelpers.hlsli`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Forward/ForwardDirectLighting.hlsli`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Forward/ForwardDebug_PS.hlsl`

Pipeline and public API cleanup:

- `src/Oxygen/Renderer/Pipeline/RenderingPipeline.h`
- `src/Oxygen/Renderer/Pipeline/ForwardPipeline.h`
- `src/Oxygen/Renderer/Pipeline/ForwardPipeline.cpp`
- `src/Oxygen/Renderer/Pipeline/Internal/PipelineSettings.h`

DemoShell cleanup:

- `Examples/DemoShell/Services/LightCullingSettingsService.h`
- `Examples/DemoShell/Services/LightCullingSettingsService.cpp`
- `Examples/DemoShell/UI/LightCullingVm.h`
- `Examples/DemoShell/UI/LightCullingVm.cpp`
- `Examples/DemoShell/UI/LightCullingDebugPanel.h`
- `Examples/DemoShell/UI/LightCullingDebugPanel.cpp`
- `Examples/DemoShell/UI/DemoShellUi.h`
- `Examples/DemoShell/UI/DemoShellUi.cpp`
- `Examples/DemoShell/DemoShell.h`
- `Examples/DemoShell/DemoShell.cpp`

Docs:

- `design/renderer-shadows/light-culling-remediation-plan.md`
- `src/Oxygen/Renderer/Docs/lighting_overview.md`
- `src/Oxygen/Renderer/Docs/override_slots.md`
- `src/Oxygen/Renderer/Docs/passes/design-overview.md`
- `src/Oxygen/Renderer/Docs/passes/depth_pre_pass.md`
- `src/Oxygen/Renderer/Docs/passes/light_culling.md` (new authoritative pass doc)

Tests and analysis:

- `src/Oxygen/Renderer/Test/CMakeLists.txt`
- `src/Oxygen/Renderer/Test/LightCullingConfig_test.cpp` (new)
- `src/Oxygen/Renderer/Test/LightCullingPassContract_test.cpp` (new)
- `src/Oxygen/Renderer/Test/LightCullingPassMembership_test.cpp` (new)
- `Examples/RenderScene/README.md`
- `Examples/RenderScene/AnalyzeRenderDocLightCulling.py` (new)

## 6. Phase Plan

### Phase LC-0: Lock The Shipping Contract

Goal:

- freeze the final shader-facing contract and delete unsupported public knobs
  before touching the main algorithm

Implementation work:

- replace the current public `LightCullingPassConfig.cluster` tuning payload
  with engine-owned construction-time state only
- redesign `LightCullingConfig` and its HLSL mirror around the final data that
  shading actually needs:
  - bindless cluster grid slot
  - bindless cluster index-list slot
  - cluster dimensions
  - power-of-two cell size shift or fixed cell size
  - UE-like `LightGridZParams`
  - fixed debug normalization constant only if debug heat-map output still
    needs it
- remove `SetClusterDepthSlices`, `UpdateLightCullingPassConfig`,
  `cluster_depth_slices`, `HighDensity()`, and all scene-level
  `rndr_cluster_*` guidance
- define the benchmark candidate set for the final shipping constants:
  - candidate A, UE-first: `64 px / 32 slices / 32 lights-per-cell`
  - candidate B, Oxygen fallback: one alternative only, chosen only if
    candidate A loses on measured frame cost or truncation behavior

Exit criteria:

- no public or persisted cluster-shape tuning remains
- only one candidate set is allowed to survive after benchmarking

### Phase LC-1: Replace The Build Path

Goal:

- move `LightCullingPass` to an analytic clustered build that does not depend on
  scene depth products

Implementation work:

- remove `DepthPrePass` and `ScreenHzbBuildPass` includes, lookups, skip
  conditions, and resource-state handling from `LightCullingPass`
- make `LightCullingPass` resource tracking idempotent and recorder-safe while
  the old path still exists, so `PrepareResources()` no longer fails on already
  tracked resources during the transition
- remove closest/furthest HZB fields from pass constants and shader inputs
- remove HZB sampling and depth-overlap rejection from `LightCulling.hlsl`
- adopt one canonical shader path and one PSO; delete the `CLUSTERED` define
  and all tile-mode comments/branches
- compute XY cell bounds analytically from the screen frustum and the chosen
  cell size
- compute Z slice bounds from UE-like `LightGridZParams`
- keep the existing flat cluster-grid + index-list outputs unless profiling
  proves the storage format itself must change
- if storage must change, update this plan first, then land one final contract;
  do not ship both

Exit criteria:

- `LightCullingPass` runs without any depth/HZB pass in the frame graph
- the pass produces the same consumer-visible grid/list contract
- the shader has one shipping path only

### Phase LC-2: Unify Build-Time And Shading-Time Lookup

Goal:

- ensure build-time cell construction and shading-time cell lookup use the same
  final math

Implementation work:

- replace the current simplified lookup contract in `ClusterLookup.hlsli`,
  `LightCullingConfig.hlsli`, and `LightingHelpers.hlsli`
- use the same `LightGridZParams` mapping in:
  - light-grid build
  - forward direct-light lookup
  - debug visualizations
- remove tile-mode-only helpers such as `ComputeTileIndex` if they are no
  longer used
- keep only the final debug outputs:
  - `Heat Map`
  - `Depth Slice`
  - `Cluster Index`
- if heat-map normalization needs the per-cell light cap, make it a fixed
  shipping constant, not a runtime setting

Exit criteria:

- all consumers derive the same cell index from the same final contract
- no tile-mode helper or stale lookup branch remains

### Phase LC-3: Clean Pipeline And DemoShell Surfaces

Goal:

- make the application and pipeline surfaces honest about the final engine
  design

Implementation work:

- remove `SetClusterDepthSlices` from `RenderingPipeline` and `ForwardPipeline`
- remove generic LightCulling pass config mutation from public pipeline APIs if
  it no longer carries a real shipping use case
- strip `PipelineSettings` down to the light-culling debug mode only
- remove persisted keys and service methods for:
  - depth slices
  - camera-Z toggle
  - manual Z near
  - manual Z far
- simplify `LightCullingVm` so it manages visualization mode only
- simplify `LightCullingDebugPanel` so it renders visualization mode controls
  only
- keep DemoShell capable of selecting the final visualization modes and nothing
  else

Exit criteria:

- DemoShell no longer exposes non-final LightCulling controls
- pipeline APIs no longer advertise mutable algorithm shape

### Phase LC-4: Clean Documentation

Goal:

- remove every repo-local statement that still describes the wrong light-culling
  architecture

Implementation work:

- add `src/Oxygen/Renderer/Docs/passes/light_culling.md` as the authoritative
  pass doc
- delete tile-mode and scene-override guidance from `override_slots.md`
- correct `lighting_overview.md` and `design-overview.md` to describe one
  clustered analytic light-grid path
- correct `depth_pre_pass.md` so it no longer claims `LightCullingPass` is a
  depth/HZB consumer
- update comments in headers and shaders to match the final implementation

Exit criteria:

- no in-repo doc or comment claims that LightCulling consumes depth/HZB
- no in-repo doc or comment advertises tile mode or runtime cluster-shape
  tuning

### Phase LC-5: Validation And Lockdown

Goal:

- prove correctness, prove the final shape, and lock one shipping
  configuration

Targeted tests:

- add `LightCullingConfig_test.cpp` to validate:
  - grid-dimension math
  - UE-like `LightGridZParams` generation
  - shading-time slice lookup mapping
- add `LightCullingPassContract_test.cpp` to validate:
  - pass preparation/execution without `DepthPrePass`
  - pass preparation/execution without `ScreenHzbBuildPass`
  - cluster-grid and index-list publication through the current frame bindings
- add `LightCullingPassMembership_test.cpp` to validate:
  - fixed-camera/fixed-light membership against a known-good baseline
  - overflow handling at the chosen shipping cap

Live validation:

- run `RenderScene` in `Debug`
- run `MultiView` in `Debug`
- confirm lighting correctness and debug-visualization correctness with the
  final LightCulling path only

RenderDoc validation:

- capture a late steady-state frame with the final LightCulling path
- prove `LightCullingPass` binds no scene depth SRV and no HZB SRV
- confirm dispatch dimensions, output UAV bindings, and representative cell
  occupancy
- verify at least one shaded pixel end-to-end:
  - pixel depth selects the expected cell
  - cell light list matches the lights actually evaluated

Performance validation:

- run the shipping-candidate benchmark in `Release` only
- compare the candidate sets on the same scene/camera path
- record:
  - `LightCullingPass` GPU time
  - total frame time
  - any visible truncation or pathological occupancy
- keep one shipping configuration and record it in this document

Exit criteria:

1. implementation exists in code
2. docs are updated
3. targeted tests ran and passed
4. `RenderScene` and `MultiView` debug runs were executed and inspected
5. a RenderDoc capture was inspected and proves the final pass shape
6. a Release benchmark selected one shipping configuration
7. no legacy path, legacy UI, or legacy documentation remains

## 7. Explicit Non-Goals

This remediation does not include:

- Lumen-specific light-culling paths
- a VSM redesign beyond keeping the existing light-grid consumer contract valid
- scene-HZB-assisted LightCulling
- raw-depth-assisted LightCulling
- keeping a hidden fallback path "just in case"

If future work needs any of the above, it must start with a plan update first.
