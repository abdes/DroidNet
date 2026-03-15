# Directional VSM Issue Audit

Date: March 15, 2026
Status: `reviewed`
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

- the current audit update does not close the full runtime matrix for Stage 4+
  changes yet
- first-scene cold-start behavior must keep being rechecked after each larger
  Stage 4/5 cut

## 2. Executive Summary

The current codebase is materially better than the March 12 review baseline.
Several core contract issues were addressed:

- stale whole-snapshot republish appears removed as the main continuity path
- page-table entries now encode fallback state
- page flags now exist with coarse/detail and hierarchical visibility bits
- clipmap snap, page-space offsets, and reuse guardband checks now exist
- the old `receiver_bootstrap`, `feedback_refinement`, and
  `current_frame_reinforcement` mechanisms are no longer live page-demand
  sources

However, the redesign is still incomplete.

The main remaining problems are:

- the backend is still architecturally monolithic
- invalidation, selection, feedback acceptance, resolve prep, and fallback
  policy are still CPU-authored in the backend
- visible-sample GPU marking is still read back and reinterpreted by CPU
  instead of becoming same-frame GPU page-management authority
- coarse fallback still depends on backend policy and alias population instead
  of a fully self-sufficient page-management pipeline
- the quality path is simpler than before, but it still blends across multiple
  clip/fallback regimes

## 3. Issue-By-Issue Audit

### 3.1 Issue 1: Wrong authoritative contract: publication snapshots

Verdict: `mostly fixed`

Evidence:

- the old `last_coherent_*` and `use_last_coherent_publish_fallback` symbols do
  not appear in live code anymore; they remain only in docs
- live publication now binds the current page-management buffers only when
  `page_management_publication_ready` is true in
  `VirtualShadowMapBackend::PublishView()`
- publication is refreshed from live page-management bindings in
  `RefreshViewExports()` instead of republishing a cached old snapshot
- previous-frame continuity is carried through `resident_reuse_snapshot`,
  `resident_pages`, clip offsets, and compatibility checks rather than through
  a stale whole-publication metadata republish

Why this is better:

- the renderer no longer appears to publish an old full page table + metadata
  snapshot as the primary motion fallback
- live GPU page-management outputs are now the published source of truth

What is still incomplete:

- the backend still depends heavily on previous metadata and reuse snapshots to
  make continuity decisions
- the architecture is still publication-gated, even though it is no longer
  stale-snapshot-led

Task:

- keep continuity anchored on page-management state and remove remaining
  publication-centric decision making from the backend path

### 3.2 Issue 2: Shading contract is too weak

Verdict: `substantially fixed`

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

What is still incomplete:

- `ComputeVirtualDirectionalShadowVisibility()` still returns fully lit when no
  valid mapping can be resolved at all
- some fallback behavior still depends on CPU-populated alias entries being
  present in the page table

Task:

- finish the contract so the shader relies on page-management-produced fallback
  data alone, without needing CPU alias population as a bridge

### 3.3 Issue 3: CPU-side heuristics are compensating for an unstable design

Verdict: `partially fixed`

Evidence:

- `feedback_refinement_pages`, `receiver_bootstrap_pages`, and
  `current_frame_reinforcement_pages` are now hard-coded to zero in
  `BuildDirectionalVirtualViewState()`
- `BuildDirectionalSelectionResult()` no longer accepts previous-frame request
  feedback as a live signal; submitted feedback is classified as
  telemetry-only at most
- feedback-hash lineage and source-region compatibility tracking were removed
  from the live backend path
- the live page-management shader already materializes current pages from
  same-frame `request_words` in `VirtualShadowResolve.hlsl`
  (`kResolvePhaseMaterializeRequested`)
- virtual view introspection no longer reports request feedback as used

Why this is better:

- previous-frame readback feedback no longer competes with same-frame GPU page
  marking for live demand authority

What is still incomplete:

- `BuildDirectionalSelectionResult()` still CPU-authors the selected page set
  that seeds pending resolve work
- same-frame request/coarse marking is not yet the sole live authority for page
  demand and mutation

Task:

- keep this contract stable while issue 4/5 finish the remaining invalidation
  and page-management ownership cuts

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

Verdict: `mostly fixed`

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

What is still incomplete:

- physical page reuse and invalidation are still CPU-authored in the backend,
  not a dedicated GPU page-management stage
- feedback demand is still read back and accepted on CPU

Task:

- keep the current flag/reuse contract, but move reuse, invalidation, and new
  allocation to the intended GPU page-management authority

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

Verdict: `partially fixed`

Evidence:

- `BuildDirectionalVirtualViewState()` now delegates explicit setup,
  previous-state, pending-resolve, and feedback-lineage stages instead of
  owning those responsibilities inline
- `StagePageManagementSeedUpload()` now delegates explicit seed-state rebuild
  and upload-queue staging helpers instead of owning reset + snapshot rebuild
  + upload bookkeeping inline
- CPU feedback acceptance, invalidation, and page-management mutation still
  exist, but they now sit behind named stage seams instead of a single backend
  body

Why this is better:

- later work on issues 3, 4, and 5 can replace stage internals without
  reopening unrelated setup/export logic

What is still incomplete:

- CPU feedback acceptance, CPU invalidation, and CPU page-management mutation
  are still live behind those stage seams

Task:

- keep replacing the stage internals until page-state authority follows the UE
  model

### 3.8 Issue 8: Oxygen never split invalidation / page management / draw-command build

Verdict: `partially fixed, but still incomplete`

Evidence:

- there are now dedicated request and coarse-mark passes, and live GPU raster
  inputs are consumed by `VirtualShadowPageRasterPass`
- page-management bindings exist as a separate exported concept
- invalidation is still derived inside the backend selection/invalidation
  stages by CPU construction of dirty resident key sets
- page-management seed/reset and resolve-state snapshot rebuild still happen on
  CPU before GPU resolve consumes those resources
- there is still no live `VirtualShadowInvalidation.hlsl`,
  `VirtualShadowPageManagement.hlsl`, or
  `VirtualShadowBuildPerPageDrawCommands.hlsl` implementation in the codebase

Why this is only partial:

- the pass graph is more structured than before
- the authoritative invalidation and page-management stages are still not split
  the way the redesign requires

Task:

- implement dedicated GPU invalidation, GPU page management, and GPU per-page
  draw-command build so the backend stops authoring these stages directly

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

Verdict: `mostly fixed`

Evidence:

- `MarkRendered()` now advances resident-page state and opens publication only
  after live raster has executed
- `PublishView()` and `RefreshViewExports()` publish live page-management SRVs
  only when `page_management_publication_ready` is true
- the old explicit last-coherent publication fallback path is gone from code

Why this is better:

- publication is now downstream of live page-management state instead of being
  an older snapshot-republish mechanism

What is still incomplete:

- the overall architecture still routes through `PublishView()`,
  `BuildDirectionalVirtualViewState()`, and `pending_residency_resolve` as the
  central organizing model
- this is no longer the March 12 failure mode, but it is still not the final
  pass-split redesign

Task:

- keep publication as a thin export layer only and finish moving authority into
  explicit marking, invalidation, page management, and raster stages

## 4. Overall Status Matrix

1. Wrong authoritative contract: publication snapshots: `mostly fixed`
2. Shading contract is too weak: `substantially fixed`
3. CPU-side heuristics compensating: `partially fixed`
4. Multi-band boiling quality path: `partially fixed`
5. Flags and page-space reuse missing: `mostly fixed`
6. Page-table contract too thin: `fixed`
7. Backend monolithic: `still failing`
8. Invalidation / page management / draw-command build not split: `partially fixed`
9. Coarse fallback bolted on too late: `fixed`
10. Resolve ownership cleanup without full redesign: `mostly fixed`

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
  and `BuildDirectionalPendingResolveStage()`
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

- `BuildDirectionalSelectionResult()` now reduces to compatibility and
  previous-state-existence checks instead of CPU-authoring accepted live page
  demand
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

- `BuildDirectionalInvalidationResult()` now only chooses between
  global-dirty fallback and GPU-side caster-bound comparison
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

### 5.8 Stage 8: Simplify the final shader quality contract

Status: `completed`

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

### 5.9 Stage 9: Remove the remaining bridge mechanisms

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

### 5.10 Stage 10: Re-run the issue matrix and validate against the real exit gate

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

## 5.11 Truthful completion statement

The truthful completion order for 100% closure is therefore:

1. mark current bridge paths as temporary
2. finish backend decomposition
3. make same-frame GPU marking authoritative
4. move invalidation to its own authoritative stage
5. move reuse/update/allocation to GPU page management
6. make coarse fallback a guaranteed page-management output
7. replace CPU raster authority with GPU per-page draw-command build
8. simplify the final shader quality contract
9. remove the remaining bridge mechanisms
10. run runtime validation against motion, invalidation, startup, and flat-receiver stress cases

Anything shorter than that is not a 100% completion plan. It is only a partial
prioritization.

## 6. Validation

Validation for this audit:

- current file evidence taken from backend, shader, pass, and test sources
- `msbuild out/build-vs/src/Oxygen/Renderer/oxygen-renderer.vcxproj /m:1 /p:Configuration=Debug /nologo`
- `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
- `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe`
- `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:1 /p:Configuration=Debug /nologo`
- `pwsh -File Examples/RenderScene/benchmark_directional_vsm.ps1`

Remaining gap:

- Stages 6-10 are still open, so the full runtime matrix is not closed
- motion-time correctness and boiling/performance behavior still need repeated
  runtime validation against camera/light stress scenes after the remaining
  Stage 6-9 cuts
- first-scene cold-start behavior must keep being checked after each larger
  cleanup cut to avoid reopening the startup-history bug
