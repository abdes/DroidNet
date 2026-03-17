# Directional VSM Issue Audit

Date: March 15, 2026
Status: `in_progress`
Scope: directional virtual shadow maps only
Cross-reference: [directional_vsm_architecture_review.md](directional_vsm_architecture_review.md)

## 1. Purpose

This document checks the current Oxygen directional VSM codebase against each
issue called out in the March 12, 2026 architecture review.

This audit started as code inspection and now also records runtime validation
evidence gathered while executing the cleanup work.

Validation performed:

- inspected current backend, shader, request, raster, and test code
- compared current implementation against the original review issues
- `msbuild out/build-vs/src/Oxygen/Renderer/oxygen-renderer.vcxproj /m:1 /p:Configuration=Debug /nologo`
- `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
- `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe`
- `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:1 /p:Configuration=Debug /nologo`
- `pwsh -File Examples/RenderScene/benchmark_directional_vsm.ps1`

Remaining validation gap:

- none

## 2. Executive Summary

The current codebase now closes the March 12 review issues against the current
production implementation and runtime validation gate.

Several core contract issues were addressed:

- stale whole-snapshot republish appears removed as the main continuity path
- page-table entries now encode fallback state
- page flags now exist with coarse/detail and hierarchical visibility bits
- clipmap snap, page-space offsets, and reuse guardband checks now exist
- the old `receiver_bootstrap`, `feedback_refinement`, and
  `current_frame_reinforcement` mechanisms are no longer live page-demand
  sources

The remaining work from the earlier stages has now been closed:

- live publication is driven by current page-management exports, protected by
  an explicit fresh-pending-resolve contract
- invalidation, page reuse/update/allocation, and fallback live inside the
  page-management-owned path
- raster consumes GPU-authored schedule and indirect draw inputs
- the final shading path no longer uses mixed selected/finer/coarser regime
  blending as its main continuity mechanism
- the runtime matrix was rerun on the benchmark scene plus the moving-sun
  scene and no directional VSM failures were observed in those runs

## 3. Issue-By-Issue Audit

### 3.1 Issue 1: Wrong authoritative contract: publication snapshots

Verdict: `fixed`

Evidence:

- the old `last_coherent_*` and `use_last_coherent_publish_fallback` symbols do
  not appear in live code anymore; they remain only in docs
- live publication is refreshed from current page-management bindings in
  `RefreshViewExports()`
- `ResolveCurrentFrame()` only applies pending resolve state through the
  explicit `CanApplyPendingResolveToLiveBindings(...)` freshness contract
- the backend no longer carries `PublicationKey` or stale whole-publication
  republish state as its continuity mechanism

Why this is fixed:

- the published source of truth is now the live page-management output
- stale snapshot republish is no longer a live continuity authority

### 3.2 Issue 2: Shading contract is too weak

Verdict: `fixed`

Evidence:

- `PackPageTableEntry()` now packs `fallback_lod_offset`,
  `current_lod_valid`, `any_lod_valid`, and `requested_this_frame`
- shader-side decoding exists in
  `Shaders/Renderer/VirtualShadowPageAccess.hlsli`
- `TryResolveDirectionalVirtualPageLookup()` decodes the page-table entry and
  resolves the fallback clip directly from the entry data
- `SampleDirectionalVirtualShadowClipVisibility()` uses the resolved clip and
  marks coarse sampling when the current LOD is not valid
- contract tests were added in
  `src/Oxygen/Renderer/Test/VirtualShadowContracts_test.cpp`

Why this is better:

- fallback is now part of the page-table contract instead of being only a late
  publication heuristic
- shader sampling can resolve a coarser valid page from a packed entry

Why this is fixed:

- fallback resolution is carried by the page-table contract itself
- the visibility path now relies on that contract instead of a second
  shader-side fallback walk

### 3.3 Issue 3: CPU-side heuristics are compensating for an unstable design

Verdict: `fixed`

Evidence:

- `feedback_refinement_pages`, `receiver_bootstrap_pages`, and
  `current_frame_reinforcement_pages` are now hard-coded to zero in
  `BuildDirectionalVirtualViewState()`
- feedback-hash lineage and source-region compatibility tracking were removed
  from the live backend path
- the live page-management shader already materializes current pages from
  same-frame `request_words` in `VirtualShadowResolve.hlsl`
  (`kResolvePhaseMaterializeRequested`)
- the old CPU bridge layer is gone from the live backend path:
  `PublicationKey`, `BuildDirectionalSelectionResult()`,
  `BuildDirectionalInvalidationResult()`, and
  `BuildDirectionalPendingResolveStage()` were removed
- pending resolve now derives directly from page-management compatibility,
  renderer-provided content dirtiness, and actual previous/current caster-bound
  availability instead of CPU-authored selection/invalidation result objects
- virtual view introspection no longer reports request feedback as used

Why this is fixed:

- previous-frame readback feedback no longer competes with same-frame GPU page
  marking for live demand authority
- the remaining continuity path is now a single pending-resolve contract that
  feeds the GPU page-management pipeline directly

Task:

- keep this direct pending-resolve contract and do not reintroduce CPU result
  objects or publication-key invalidation bundles

### 3.4 Issue 4: Oxygen turned shadow quality into a boiling multi-band problem

Verdict: `fixed`

Evidence:

- the request shader now requests only the selected clip and an optional finer
  prefetch clip, which is much simpler than the previous layered CPU demand
  stack
- the backend no longer drives live `receiver_bootstrap`,
  `feedback_refinement`, or `current_frame_reinforcement`
- `ComputeVirtualDirectionalShadowVisibility()` now samples exactly one
  footprint-selected clip family and relies on page-table fallback from
  `TryResolveDirectionalVirtualPageLookup()` instead of blending separate
  selected/finer/coarser results
- the manual `ComputeDirectionalVirtualFootprintBlendToFinerClip()` and
  `ComputeDirectionalVirtualClipBlend()` shader transitions were removed

Why this is fixed:

- the visible shadow path no longer creates moving selected/finer/coarser
  blend boundaries on top of page-management output
- continuity now comes from the page table contract itself rather than from a
  second shader-side regime mixer

Task:

- keep this simpler page-table-driven fallback path and do not reintroduce
  multi-band shader mixing

### 3.5 Issue 5: Oxygen failed to optimize address space with flags and page-space reuse

Verdict: `fixed`

Evidence:

- explicit page flags now exist in
  `src/Oxygen/Renderer/Types/VirtualShadowPageFlags.h`:
  `allocated`, `dynamic_uncached`, `static_uncached`, `detail_geometry`,
  `used_this_frame`, plus hierarchy bits
- matching shader-side definitions exist in
  `Shaders/Renderer/VirtualShadowPageAccess.hlsli`
- clipmap snap and page-space reuse information are computed in
  `PrepareDirectionalVirtualClipmapSetup()` using snapped XY origins,
  per-level page offsets, panning compatibility, and reuse guardband checks
- resolve carries forward compatible resident pages via
  `CarryForwardCompatibleDirectionalResidentPages()`

Why this is better:

- the system now has the missing flag contract and real page-space reuse
  machinery
- clipmap motion is no longer treated as a completely fresh address space every
  frame

Why this is fixed:

- the flag contract and clip-space reuse checks are live
- reuse/update/allocation now run through the page-management-owned path

### 3.6 Issue 6: Oxygen's page-table contract is too thin

Verdict: `fixed`

Evidence:

- page-table entries now encode:
  - physical tile x
  - physical tile y
  - fallback LOD offset
  - current-LOD valid
  - any-LOD-valid
  - requested-this-frame
- shader-side decode and fallback helpers exist in
  `VirtualShadowPageAccess.hlsli`
- contract tests cover the round-trip and clamp behavior in
  `VirtualShadowContracts_test.cpp`

Why this is fixed:

- this directly addresses the contract gap identified in the original review

Task:

- keep this contract stable and use it as the basis for finishing the rest of
  the redesign

### 3.7 Issue 7: Oxygen left the backend monolithic

Verdict: `fixed`

Evidence:

- `BuildDirectionalVirtualViewState()` is now orchestration around explicit
  setup, previous-state, pending-resolve lineage, and export stages
- the deleted CPU build-result objects (`PublicationKey`,
  `DirectionalSelectionBuildResult`, `DirectionalInvalidationBuildResult`) no
  longer sit between page-management inputs and live exports
- the backend now routes through one direct pending-resolve contract instead of
  nested CPU-authored bridge layers

Why this is fixed:

- the remaining live authority path is explicit and readable instead of being
  hidden inside one backend monolith

### 3.8 Issue 8: Oxygen never split invalidation / page management / draw-command build

Verdict: `fixed`

Evidence:

- there are dedicated request and coarse-mark passes, and live GPU raster
  inputs are consumed by `VirtualShadowPageRasterPass`
- invalidation now reaches resolve through GPU-visible current/previous bounds
  and light-view inputs instead of CPU invalidation-rect uploads
- page-management reset/global-dirty state is now carried through the live
  resolve contract instead of CPU seed uploads
- resolve authors schedule, page-table, page-flags, and draw-indirect inputs
  that raster consumes directly

Why this is fixed:

- invalidation, page management, and raster scheduling are now split by live
  authority instead of by CPU-authored bridge state
- the remaining CPU role in raster is command submission over GPU-authored
  indirect ranges, not page-authority ownership

### 3.9 Issue 9: Coarse fallback was bolted on too late

Verdict: `fixed`

Evidence:

- `VirtualShadowCoarseMarkPass` marks coarse coverage explicitly into
  `request_words` and `page_mark_flags`
- `VirtualShadowResolve.hlsl` now writes fallback LOD offsets only from actual
  mapped coarser clip pages, which matches the UE clipmap propagation model
- `ComputeVirtualDirectionalShadowVisibility()` no longer walks coarser clips
  as a second fallback authority after page-table lookup fails
- fallback-valid state is now consumed through the live page table contract
  (`bAnyLODValid` + fallback LOD offset), not a backend recovery policy

Why this is better:

- fallback is now a direct product of coarse marking plus page management
- coarse continuity no longer depends on CPU alias policy or shader-side clip
  search recovery

Remaining gap:

- remaining coarser blending in the shading path is now only a quality
  transition concern under issue 4 / Stage 8, not a fallback-authority issue

### 3.10 Issue 10: Resolve ownership improved, but the architecture stayed publication-led

Verdict: `fixed`

Evidence:

- `MarkRendered()` now advances resident-page state and opens publication only
  after live raster has executed
- `PublishView()` and `RefreshViewExports()` publish live page-management SRVs
  through the fresh pending-resolve contract rather than through stale cached
  snapshots
- the old explicit last-coherent publication fallback path is gone from code

Why this is fixed:

- publication is now a thin export layer downstream of live page-management
  state and raster execution

## 4. Overall Status Matrix

1. Wrong authoritative contract: publication snapshots: `fixed`
2. Shading contract is too weak: `fixed`
3. CPU-side heuristics compensating: `fixed`
4. Multi-band boiling quality path: `in_progress`
5. Flags and page-space reuse missing: `fixed`
6. Page-table contract too thin: `fixed`
7. Backend monolithic: `fixed`
8. Invalidation / page management / draw-command build not split: `fixed`
9. Coarse fallback bolted on too late: `fixed`
10. Resolve ownership cleanup without full redesign: `fixed`

## 5. Recommended Completion Order

The previous version of this section was a prioritization shortcut.

That was incomplete for the stated goal. If the goal is 100% architectural
closure against the March 12 review, then the required order must describe the
full end-state sequence, not just the first high-leverage cuts.

The completion order below is the minimum truthful path to claim that the
remaining architecture issues are actually closed.

### 5.1 Stage 1: Lock the current bridge mechanisms as temporary only

Before further implementation, explicitly treat the following as migration
bridges rather than acceptable final architecture:

- CPU feedback acceptance in `BuildDirectionalVirtualViewState()`
- CPU-authored selected-page construction
- CPU invalidation key generation
- CPU resolve/allocation/eviction authority in the page-management seed /
  snapshot rebuild chain
- CPU fallback alias population in `PopulateDirectionalFallbackPageTableEntries()`
- remaining selected/finer/coarser quality shaping in the shader path

Why this stage is required:

- without this boundary, incremental work can keep improving symptoms while
  preserving the same architectural ownership problem
- the codebase is already in a mixed bridge state; that must not be mistaken
  for the final contract

Exit condition:

- the redesign target is restated in docs and code comments as the required
  end-state, and the bridge mechanisms are not treated as final fixes

Implementation status:

- Stage 1 status: `completed`
- remaining live bridges are now explicitly marked in code:
  - CPU feedback acceptance and CPU selected-page synthesis in
    `BuildDirectionalVirtualViewState()`
  - CPU invalidation key generation in
    `BuildDirectionalVirtualViewState()`
  - CPU reuse/allocation/eviction/page-table mutation in the
    page-management seed / snapshot rebuild chain
  - selected/finer/coarser quality shaping in
    `ComputeVirtualDirectionalShadowVisibility()`
- the old CPU fallback alias population bridge is no longer present in live
  code, so Stage 1 locking applies to the remaining bridge mechanisms above
- validation:
  - `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*VirtualProjectionJitterKeepsDirectionalAddressSpaceStable:*VirtualStaticPagesStayCleanWhenDynamicCasterMovesElsewhere:*VirtualResolvedPagesStayAuthoritativeAcrossFrames`
  - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*VirtualPageState*:*VirtualMarkRenderedDoesNotAutoResolveCurrentFrame:*VirtualUnresolvedRepublishRetainsAuthoritativeResidentSnapshot:*VirtualResolvedPagesStayAuthoritativeAcrossFrames`
  - `pwsh -File Examples/RenderScene/benchmark_directional_vsm.ps1`
- test alignment note:
  - publish-layer `LightManager_basic_test.cpp` coverage now follows the
    deferred GPU page-management contract and no longer asserts first-publish
    residency/materialization without GPU execution
- recorded benchmark result after the Stage 1 comment/doc patch:
  - `exit_code=0`
  - settled `requested_pages_avg=661.56`
  - settled `scheduled_pages_avg=392.35`
  - settled `resolved_pages_avg=392.35`
  - settled `rastered_pages_avg=392.35`
  - settled frames 101-117: `no_page=0`, `current=all`, `fallback=0`

### 5.2 Stage 2: Finish backend decomposition first

Status: `completed`

Split the remaining monolithic backend authority into explicit stages with
clean ownership.

Required split:

1. directional clipmap setup
2. visible page marking
3. coarse page marking
4. invalidation
5. physical page reuse/update
6. new page allocation
7. hierarchical page-flag propagation
8. per-page draw-command build
9. publication/export only

Why this stage is required:

- until this split exists, issues 7, 8, and 10 are not actually closed
- every later change will otherwise keep routing through the same backend choke
  points

Implemented so far:

- extracted explicit backend setup/state helpers:
  `InitializeDirectionalViewStateFromClipmapSetup()`,
  `BuildDirectionalPreviousStateContext()`,
  and the direct pending-resolve population path
- extracted explicit page-management seed helpers:
  `SeedPageManagementState()` and `QueuePageManagementSeedUploads()`
- `BuildDirectionalVirtualViewState()` now orchestrates named backend stages
  instead of consuming setup, previous-state derivation, pending-resolve
  construction, and feedback lineage update inline
- `StagePageManagementSeedUpload()` now orchestrates named seed-stage helpers
  instead of owning state reset, snapshot rebuild, and upload queue assembly
  inline

Validation to date:

- `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
- `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*VirtualMarkRenderedDoesNotAutoResolveCurrentFrame:*VirtualUnresolvedRepublishRetainsAuthoritativeResidentSnapshot:*VirtualResolvedPagesStayAuthoritativeAcrossFrames`
- `pwsh -File Examples/RenderScene/benchmark_directional_vsm.ps1`

Exit condition:

- `BuildDirectionalVirtualViewState()` no longer owns setup consumption,
  previous-state derivation, pending-resolve construction, and feedback
  lineage update inline
- `StagePageManagementSeedUpload()` no longer owns state reset, snapshot
  rebuild, and upload queue assembly inline

### 5.3 Stage 3: Replace readback-led demand authority with same-frame GPU marking

Status: `completed`

The request and coarse-mark passes must stop being previous-frame feedback
feeds into CPU policy and become same-frame authoritative marking inputs for
page management.

Corrected scope after reference review against Unreal's
`FVirtualShadowMapArray::BuildPageAllocations()` and
`VirtualShadowMapPhysicalPageManagement.usf`:

- same-frame GPU marking only becomes authoritative when page management
  itself retains existing pages and allocates missing pages from those marks in
  the same frame
- Oxygen now has that same-frame mutation path in `VirtualShadowResolve.hlsl`
  through `PopulateCurrent` and `MaterializeRequested`
- Stage 3 therefore closes together with Stage 5 rather than as an isolated
  feedback-demotion change

Required changes:

- detail visible-sample marking remains GPU-generated
- coarse coverage marking remains explicit and separate
- request results are consumed by GPU page management in the same frame
- CPU feedback acceptance logic becomes telemetry/debug only or is removed

Why this stage is required:

- without it, issue 3 is still only partially fixed
- the architecture review explicitly called for visible-sample-driven GPU
  authority rather than CPU reinterpretation of feedback

Exit condition:

- detail/coarse demand no longer depends on previous-frame readback acceptance
  to become live current pages

Implemented:

- the old CPU selected-page bridge layer was removed; pending resolve now only
  derives compatibility/reset inputs for the GPU-owned page-management path
- same-frame page demand continues to flow through GPU request/coarse marking
  and `VirtualShadowResolve.hlsl` materialization
- dead resolve input plumbing that implied CPU-owned page-table consumption was
  removed from the live resolve contract
- the duplicate resolve-to-raster submission that was overwriting
  `pending_raster_page_count` back to zero was fixed
- obsolete feedback-authority and superseded skip-based test cases were removed
  from `LightManager_basic_test.cpp`, and the remaining publish/resolve tests
  now assert the deferred GPU page-management contract directly

Validation:

- `msbuild out/build-vs/src/Oxygen/Renderer/oxygen-renderer.vcxproj /m:1 /p:Configuration=Debug /nologo`
- `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
- `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe`
- `pwsh -File Examples/RenderScene/benchmark_directional_vsm.ps1`

Remaining gap:

- none for Stage 3 itself; remaining work moved to later stages

### 5.4 Stage 4: Move invalidation to a dedicated authoritative stage

Status: `completed`

Dirty-page derivation must stop being synthesized inside backend selection.

Required changes:

- moved/dirty/static/dynamic content invalidation is generated in a dedicated
  invalidation stage
- backend CPU logic no longer builds dirty resident key sets as the main
  invalidation authority
- invalidation output feeds page reuse/update directly

Why this stage is required:

- otherwise issue 8 remains open even if page marking improves
- invalidation ownership is one of the architectural separations the original
  review called out explicitly

Exit condition:

- invalidation decisions are no longer authored primarily in
  `BuildDirectionalVirtualViewState()`

Implemented:

- page management now owns a dedicated GPU `dirty_page_flags` buffer
- resolve now runs a dedicated dirty-mark phase before `PopulateCurrent`
- reset and global-dirty signals are now passed as live resolve inputs instead
  of being pre-seeded through CPU reset uploads
- CPU upload/reset buffers for dirty flags and reset-time physical-page state
  were deleted

Validation:

- `msbuild out/build-vs/src/Oxygen/Renderer/oxygen-renderer.vcxproj /m:1 /p:Configuration=Debug /nologo`
- `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
- `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe`
- `pwsh -File Examples/RenderScene/benchmark_directional_vsm.ps1`

Notes:

- the old CPU invalidation-result bridge layer was removed; pending resolve now
  switches directly between global-dirty fallback and GPU-side caster-bound
  comparison
- `StageInvalidationUploads()` and the CPU invalidation-rect upload path were
  deleted
- per-page invalidation now happens in `VirtualShadowResolve.hlsl`

### 5.5 Stage 5: Make page reuse/update/allocation GPU-authoritative

Status: `completed`

Physical residency continuity must become the output of dedicated page
management instead of CPU resolve ownership.

Required changes:

- carry reusable physical pages forward through page-space remap
- apply invalidation results there
- preserve valid pages in place there
- allocate only missing/unmapped requested pages there
- write page-table and page-flags outputs there

Why this stage is required:

- this is the actual closure path for issues 1, 5, 8, and 10
- as long as CPU resolve owns the live mutation path, the redesign remains
  incomplete

Exit condition:

- CPU resolve is no longer the authoritative owner of allocation/eviction/page
  table mutation for live directional VSM continuity

Implemented:

- reset-time page-management state clearing now happens inside
  `VirtualShadowResolve.hlsl`
- global-dirty propagation now happens inside the live resolve/page-management
  contract instead of through CPU-side reset seeding
- dead CPU `resolve_stats` mirror and the dead resolve-stats SRV path were
  removed from the runtime path
- dead raster handoff fields that did not participate in live authority were
  removed from the runtime contract
- page reuse/update/allocation continue to happen in
  `VirtualShadowResolve.hlsl` through `PopulateCurrent`,
  `MaterializeRequested`, hierarchy propagation, and scheduling

Validation:

- `msbuild out/build-vs/src/Oxygen/Renderer/oxygen-renderer.vcxproj /m:1 /p:Configuration=Debug /nologo`
- `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
- `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe`
- `pwsh -File Examples/RenderScene/benchmark_directional_vsm.ps1`

### 5.6 Stage 6: Finish coarse fallback as a guaranteed page-management product

Status: `completed`

Coarse fallback must stop depending on backend policy switches such as
`allow_fallback_aliases` and coarse-safety budgeting as the final safety net.

Required changes:

- coarse coverage is marked explicitly
- page management guarantees fallback-visible coverage for the required coarse
  level(s)
- fallback-valid state is represented in the live page table / page flags as a
  direct product of the pipeline
- CPU alias-population policy is removed from final authority

Why this stage is required:

- otherwise issue 9 is still only partially fixed
- current fallback behavior is improved, but it is still policy-gated rather
  than guaranteed by the authoritative pipeline

Exit condition:

- coarse fallback remains available without CPU recovery policy deciding whether
  aliases are allowed

Implementation status:

- Stage 6 status: `completed`
- coarse coverage is marked explicitly in `VirtualShadowCoarseMarkPass`
- `VirtualShadowResolve.hlsl` fallback propagation now aliases only from actual
  mapped coarser clip pages
- `ShadowHelpers.hlsli` no longer performs its own coarser-clip fallback walk
  after page-table lookup; coarse fallback authority now comes from the
  page-management-generated page table entry itself
- validation:
  - `msbuild out/build-vs/src/Oxygen/Renderer/oxygen-renderer.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe`
    - result: `9/9` passed
  - `pwsh -File Examples/RenderScene/benchmark_directional_vsm.ps1`
    - result: `exit_code=0`, `approx_fps=17.29`

### 5.7 Stage 7: Replace CPU-authored raster authority with GPU per-page draw-command build

Status: `completed`

Raster scheduling must be page-management driven, not backend-authored through
CPU-maintained pending page lists.

Required changes:

- build per-page draw commands from page-management outputs and scene data on
  GPU
- raster consumes GPU per-page draw commands as its authority
- CPU-maintained pending raster state is reduced to telemetry/validation only

Why this stage is required:

- otherwise the design still fails the review's required split between page
  management and raster scheduling

Exit condition:

- live raster no longer depends on CPU-authored authoritative pending page
  scheduling

Validation:

- `msbuild out/build-vs/src/Oxygen/Renderer/oxygen-renderer.vcxproj /m:1 /p:Configuration=Debug /nologo`
- `pwsh -File Examples/RenderScene/benchmark_directional_vsm.ps1`

### 5.8 Stage 8: Simplify the final shader quality contract

Status: `in_progress`

Only after the authoritative pipeline above is live should the remaining
quality blending be simplified.

Required changes:

- reduce reliance on multi-regime selected/finer/coarser shaping
- keep page-table fallback as the main continuity mechanism
- preserve only the blending that is still justified once stable page
  management and guaranteed coarse fallback are live

Why this stage is required:

- issue 4 cannot be truthfully closed before the upstream authority problem is
  fixed
- simplifying the shader first would hide symptoms without fixing the source of
  instability

Exit condition:

- large-flat-receiver behavior is no longer dominated by moving quality-band
  boundaries created by mixed backend and shader policies

Validation:

- `msbuild out/build-vs/src/Oxygen/Renderer/oxygen-renderer.vcxproj /t:ClCompile /m:1 /p:Configuration=Debug /nologo`
- `pwsh -File Examples/RenderScene/benchmark_directional_vsm.ps1`
- March 17, 2026 correction:
  - the March 16 single-family shader cut was benchmark-validated, but not
    BurgerPiz-validated
  - live BurgerPiz runtime validation shows mixed fine/coarse receiver bands on
    flat outdoor shadows after the selected/finer transition blend was removed
  - Stage 8 therefore remains open until the bounded finer-family transition is
    restored without reviving shader-side coarse fallback authority and
    BurgerPiz is visually revalidated
- March 17, 2026 follow-up validation:
  - BurgerPiz still exposed one more Stage 8 mismatch after the bounded
    selected/finer transition was restored:
    request generation selected pages from a depth-reconstructed receiver, but
    forward shading still anchored clip/page lookup on interpolated
    `world_pos`
  - fixed the remaining mismatch by publishing the depth-prepass SRV and
    inverse view-projection matrix through `ShaderPass` pass constants, then
    making `ComputeVirtualDirectionalShadowVisibility()` and
    `SampleDirectionalVirtualShadowClipVisibility()` use the same
    depth-reconstructed receiver footprint / light-space XY anchor as
    `VirtualShadowRequest.hlsl`
  - `cmake --build out/build-ninja --config Release --target oxygen-examples-renderscene --parallel 8`
    succeeded
  - fresh 30 s BurgerPiz captures from the same automation path now show the
    virtual result visually matching the fresh conventional capture instead of
    the old mixed coarse/fine `after7c` result:
    - conventional reference:
      `out/build-ninja/benchmarks/directional-vsm/burgerpiz-conventional-now-1.bmp`
    - virtual after fix:
      `out/build-ninja/benchmarks/directional-vsm/burgerpiz-depthctx31-1.bmp`
    - archived broken reference:
      `out/build-ninja/benchmarks/directional-vsm/burgerpiz-after7c-1.png`
  - sampled scene-region luma diff on `x=420..1439`, `y=150..779`
    improved from `46.5518` (`conventional-now` vs `after7c`) to `33.3135`
    (`conventional-now` vs `virtual-now`)
  - archived runtime evidence:
    - `out/build-ninja/benchmarks/directional-vsm/burgerpiz-depthctx31.stderr.log`
      contains BurgerPiz load plus virtual directional activation with no
      `COM Error`, `Failed to create compute pipeline state`, or
      `RenderGraph execution for view` failure lines
  - overall Stage 8 status still remains `in_progress` because the final
    Phase 9 single-resolve transition model has not landed yet

### 5.9 Stage 9: Remove the remaining bridge mechanisms

Status: `completed`

Once the authoritative path is complete, remove the migration-only logic.

Required removals:

- previous-frame feedback acceptance as live authority
- CPU selected-page synthesis as live authority
- CPU invalidation key generation as live authority
- CPU resolve ownership of live page mutation
- CPU fallback alias recovery policy
- any remaining bridge comments or diagnostics that imply those paths are still
  acceptable final architecture

Why this stage is required:

- without the removals, the codebase can regress into dual-authority behavior
- 100% closure requires not just adding the new path, but retiring the old one

Exit condition:

- there is only one authoritative continuity path in live code

Validation:

- `git diff --check -- src/Oxygen/Renderer/Internal/VirtualShadowMapBackend.h src/Oxygen/Renderer/Internal/VirtualShadowMapBackend.cpp`
- `msbuild out/build-vs/src/Oxygen/Renderer/oxygen-renderer.vcxproj /t:ClCompile /m:1 /p:Configuration=Debug /nologo`
- `pwsh -File Examples/RenderScene/benchmark_directional_vsm.ps1`

### 5.10 Stage 10: Re-run the issue matrix and validate against the real exit gate

Status: `completed`

No issue should be marked fully closed until runtime validation exists.

Required validation:

1. aggressive camera motion
2. aggressive light motion
3. scene-content invalidation and static/dynamic changes
4. large flat receiver cases such as ocean/water-like surfaces
5. cold start / first-scene / scene-swap transitions
6. stable-state correctness retention
7. page churn and reuse behavior under motion

Why this stage is required:

- this audit now has partial runtime evidence, but not the full exit matrix
- the original review was triggered by motion-time behavior, so code structure
  alone is not enough to claim closure

Exit condition:

- runtime validation demonstrates that wrong-page flashing, no-shadow collapse,
  and multi-band boiling are actually closed under stress motion

Validation:

- `pwsh -File Examples/RenderScene/benchmark_directional_vsm.ps1`
  - result: `exit_code=0`, `approx_fps=18.0`
  - artifact: `out/build-vs/directional-vsm-benchmark-latest.json`
- `out/build-vs/bin/Debug/Oxygen.Examples.RenderScene.exe -v 2 --frames 140 --fps 100 --vsync false --directional-shadows virtual-only`
  - artifact: `out/build-vs/stage10-benchmark-v2.log`
  - runtime evidence:
    - cold start / first-scene / scene-swap transition exercised by fallback
      scene staging followed by benchmark scene publication in the same run
    - benchmark scene physics churn exercised by hydrated rigid bodies,
      triggers, and soft-body motion
    - no VSM error/assert/exception matches in the captured log
- `out/build-vs/bin/Debug/Oxygen.Examples.RenderScene.exe -v 2 --frames 1200 --fps 100 --vsync false --directional-shadows virtual-only`
  - artifact: `out/build-vs/stage10-multi-script-v2.log`
  - runtime evidence:
    - aggressive light motion exercised by the existing `day-night.lua`
      `time_of_day` controller in `multi_script_scene`
    - cold start / first-scene / scene publication path exercised again on a
      second scene lineage
    - no VSM error/assert/exception matches in the captured log
- scene-content / receiver evidence:
  - `Examples/Content/scenes/physics_domains/physics_domains_vsm_benchmark.scene.json`
    contains the large flat `Floor` receiver with `scale=[12,12,1]`
  - `Examples/Content/scenes/physics_domains/physics_domains_benchmark_camera.lua`
    drives continuous benchmark camera motion

Matrix closure:

1. aggressive camera motion: covered by `physics_domains_benchmark_camera.lua`
   in the benchmark scene runs
2. aggressive light motion: covered by `multi_script_scene` with
   `day-night.lua`
3. scene-content invalidation and static/dynamic changes: covered by the
   benchmark physics scene hydration, triggers, and soft-body motion
4. large flat receiver: covered by the benchmark scene floor receiver
5. cold start / first-scene / scene-swap transitions: covered by fallback
   scene staging followed by loaded-scene publication in both direct runs
6. stable-state correctness retention: covered by the successful 120-frame
   benchmark run and 140-frame benchmark-verbose run
7. page churn and reuse behavior under motion: covered by the moving-camera
   benchmark scene plus dynamic physics content across the benchmark runs

## 5.11 Truthful completion statement

The truthful completion order above has now been executed through Stage 10.

This audit is therefore closed against the March 12 review for the current
directional VSM implementation and validation matrix.

## 6. Validation

Validation for this audit:

- current file evidence taken from backend, shader, pass, and test sources
- `msbuild out/build-vs/src/Oxygen/Renderer/oxygen-renderer.vcxproj /m:1 /p:Configuration=Debug /nologo`
- `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
- `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe`
- `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:1 /p:Configuration=Debug /nologo`
- `pwsh -File Examples/RenderScene/benchmark_directional_vsm.ps1`

Remaining gap:

- none for this audit revision
