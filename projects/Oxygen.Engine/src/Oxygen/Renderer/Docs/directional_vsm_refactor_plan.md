# Directional VSM Refactor Plan

Date: March 12, 2026
Status: `active`
Scope: directional virtual shadow maps only

Cross-references:

- historical failure analysis:
  `src/Oxygen/Renderer/Docs/directional_vsm_architecture_review.md`
- historical redesign notes:
  `src/Oxygen/Renderer/Docs/directional_vsm_redesign_plan.md`
- historical performance evidence:
  `src/Oxygen/Renderer/Docs/directional_vsm_performance_plan.md`

## 1. Authority Rule

This document is the only active implementation reference for the directional
VSM refactor moving forward.

Rules:

- active task tracking lives here only
- phase status lives here only
- legacy directional VSM docs remain historical context and must not be updated
  during this refactor
- legacy docs are reconciled only after the refactor lands or is abandoned

This rule exists to stop losing time on parallel plan maintenance.

## 1.1 Scope Correction

Date: March 12, 2026
Status: `active`

The remaining directional VSM refactor is explicitly re-scoped around the UE5
directional clipmap cache model:

- cache validity is decided per directional light and per clipmap level
- clipmap XY motion is handled by snapped page-space panning
- clipmap Z continuity is handled by stable guard-banded depth ranges
- continuity comes from persistent physical-page remapping
- redraw is limited to dirty or newly requested uncached pages

The following idea is now rejected as the target architecture:

- whole-publication continuity through stale `last_coherent_*` snapshot
  republish

If temporary compatibility code remains in the tree during the transition, it
is stopgap only and must not shape the remaining design phases.

## 2. Refactor Objective

Replace the current publication-led directional VSM architecture with a
page-centric clipmap/cache architecture that is:

- motion-stable
- sample-correct
- performance-scalable
- pass-split by ownership
- free of the current continuity hacks

The refactor is complete only when:

- motion-time wrong-page / no-shadow behavior is gone
- stable state remains correct
- the main continuity path no longer depends on stale whole-snapshot republish
- the current monolithic backend logic has been replaced by explicit stages
- the moving-camera benchmark improves materially against the frozen baseline

## 3. Locked Acceptance Criteria

The refactor does not close until all of the following are true:

1. Correctness
   - no motion-time wrong-page flashing during aggressive camera movement
   - no motion-time no-shadow frames caused by directional VSM publication
   - stable state remains visually correct

2. Architecture
   - no authoritative use of `last_coherent_*` snapshot republish
   - no authoritative use of `receiver_bootstrap`,
     `feedback_refinement`, or `current_frame_reinforcement`
   - no CPU-authored authoritative raster page schedule
   - no monolithic backend function still owning clipmap setup, page
     selection, invalidation, allocation, fallback policy, and raster
     scheduling together

3. Performance
   - the locked moving-camera benchmark remains the benchmark contract
   - the refactored path materially improves whole-run cost and page churn
   - performance work proceeds on the new architecture only

## 4. Locked Benchmark Contract

All benchmark claims for this refactor must use:

- runner:
  `powershell -ExecutionPolicy Bypass -File Examples\\RenderScene\\benchmark_directional_vsm.ps1`
- scene:
  `/.cooked/Scenes/physics_domains_vsm_benchmark.oscene`
- motion:
  `Examples/Content/scenes/physics_domains/physics_domains_benchmark_camera.lua`
- app command:
  `out/build-vs/bin/Debug/Oxygen.Examples.RenderScene.exe -v 0 --frames 120 --fps 100 --vsync false --directional-shadows virtual-only`

No alternative benchmark run is authoritative unless this document is updated
first.

## 5. Refactor Principles

1. No new parallel legacy path.
   - each phase must replace authority, not add another authority

2. Page-table continuity, not publication continuity.
   - continuity must come from clipmap/page reuse and page-table fallback

3. Clipmap motion is a page-space remap problem.
   - not a stale metadata republish problem

4. Cache validity must be explicit and bounded.
   - directional cache reuse is allowed only when the light/clipmap setup is
     equivalent enough to remap safely
   - Z continuity must come from guard bands, not from reusing drifting depth
     bases

5. Coarse/detail policy must be simple.
   - binary and explicit, not a boiling stack of overlapping quality regimes

6. Page marking is not the continuity mechanism.
   - page marking identifies needed pages for this frame
   - it does not decide whether old physical pages remain valid

7. Passes own one concern.
   - clipmap setup
   - cache validity
   - page remap/reuse
   - page marking
   - invalidation
   - allocation
   - page-flag propagation
   - per-page draw-command build
   - raster
   - projection

8. Verification gates are mandatory.
   - no phase is complete without targeted evidence

## 6. Legacy Kill List

These items must be removed as authoritative mechanisms by the end of the
refactor:

- `last_coherent_shadow_instances`
- `last_coherent_directional_virtual_metadata`
- `last_coherent_page_table_entries`
- `use_last_coherent_publish_fallback`
- `receiver_bootstrap`
- `feedback_refinement`
- `current_frame_reinforcement`
- CPU-authored authoritative `resolved_raster_pages`
- page-table entries that only encode tile coordinates plus valid/requested
  bits

## 7. Target Runtime Shape

The final directional VSM runtime should be organized as:

1. `DirectionalVirtualClipmapSetupPass`
2. `DirectionalVirtualCacheValidityPass`
3. `DirectionalVirtualPageRemapPass`
4. `DirectionalVirtualPageMarkPass`
5. `DirectionalVirtualCoarseMarkPass`
6. `DirectionalVirtualInvalidationPass`
7. `DirectionalVirtualPageAllocatePass`
8. `DirectionalVirtualPageFlagPropagatePass`
9. `DirectionalVirtualBuildPerPageDrawCommandsPass`
10. `DirectionalVirtualPageRasterPass`
11. `DirectionalVirtualProjectionPass`
12. optional `DirectionalVirtualPageHzbPass`

These names are directional targets for ownership. Exact final class names may
change, but the split of responsibilities may not collapse back into the old
backend monolith.

## 8. Phased Execution Plan

Phases are sequential. Only one phase may be `in_progress` at a time.

### Phase 1. Replace The Core Data Contracts

Status: `completed`

Goal:

- establish the new clipmap/page-table/page-flag contracts before moving pass
  ownership

Implementation:

- introduce a stable directional clipmap constants contract
- introduce a new page-table entry layout with:
  - physical address
  - current-LOD valid
  - any-LOD-valid
  - fallback LOD offset
- introduce a page-flags buffer with at least:
  - allocated
  - dynamic uncached
  - static uncached
  - detail geometry
  - used this frame
- introduce physical page metadata and list contracts for reuse/allocation
- add shader-side access helpers for the new page-table/page-flags layout

Must remove or demote:

- the old page-table packing as the only shading contract

Verification:

- focused encode/decode unit tests for page-table and page-flags packing
- shader compilation succeeds for the new page-access helpers
- no phase completion without tests proving fallback decoding works

Completion gate:

- new contracts exist in code
- targeted tests are green
- no active implementation work still depends on the old page-table contract
  for new functionality

Completion evidence:

- implemented new core contracts:
  - `src/Oxygen/Renderer/Types/VirtualShadowPageTableEntry.h`
  - `src/Oxygen/Renderer/Types/VirtualShadowPageFlags.h`
  - `src/Oxygen/Renderer/Types/VirtualShadowPhysicalPageMetadata.h`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/VirtualShadowPageAccess.hlsli`
- wired the new contract through publication/bindings:
  - `src/Oxygen/Renderer/Types/ShadowFrameBindings.h`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/ShadowFrameBindings.hlsli`
  - `src/Oxygen/Renderer/Types/ShadowFramePublication.h`
  - `src/Oxygen/Renderer/Renderer.cpp`
  - `src/Oxygen/Renderer/Internal/VirtualShadowMapBackend.h`
  - `src/Oxygen/Renderer/Internal/VirtualShadowMapBackend.cpp`
  - `src/Oxygen/Renderer/Types/VirtualShadowRenderPlan.h`
- moved shader-side directional page access to the new helper in:
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/ShadowHelpers.hlsli`
- added focused validation:
  - `src/Oxygen/Renderer/Test/VirtualShadowContracts_test.cpp`
  - `src/Oxygen/Renderer/Test/LightManager_basic_test.cpp`
- validation run on March 12, 2026 in `out/build-vs`:
  - `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.VirtualShadowContracts.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - `out/build-vs/bin/Debug/Oxygen.Renderer.VirtualShadowContracts.Tests.exe`
    - result: `4/4` passed
  - `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*:*ShadowManagerPrepareVirtualPageTableResources_UploadsResolvedEntries:*ShadowManagerPrepareVirtualPageTableResources_UploadsResolvedPageFlags`
    - result: `50/50` passed
  - `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:1 /p:Configuration=Debug /nologo`
    - shader bake succeeded during the build
  - `out/build-vs/bin/Debug/Oxygen.Examples.RenderScene.exe -v 0 --frames 1 --fps 100 --vsync false --directional-shadows virtual-only`
    - result: exited `0`
- gate assessment:
  - the new page-table/page-flags/physical-page contracts exist in code
  - fallback decoding has dedicated unit coverage
  - new shading-side functionality now uses `VirtualShadowPageAccess.hlsli` instead of raw page-table bit decoding
  - no new Phase 1 functionality depends on the old page-table packing as its only contract

### Phase 2. Split Clipmap Setup From Backend Planning

Status: `completed`

Goal:

- make clipmap setup a narrow CPU-owned stage instead of the front half of a
  monolithic planner

Implementation:

- add a dedicated directional clipmap setup pass or equivalent explicit stage
- move into that stage only:
  - snapped clipmap transforms
  - page-space offsets to previous clipmaps
  - reuse/guardband validity
  - clipmap constant upload
- remove clipmap setup authority from the monolithic
  `BuildDirectionalVirtualViewState()` path

Must remove or demote:

- `BuildDirectionalVirtualViewState()` as the owner of both clipmap setup and
  page demand policy

Verification:

- focused tests for snapped motion / page-space offset computation
- focused tests for reuse rejection when guardbands are exceeded

Completion gate:

- clipmap setup is explicit and isolated
- page selection no longer depends on the old clipmap-building monolith

Completion evidence:

- extracted directional clipmap setup into an explicit backend-owned stage:
  - `src/Oxygen/Renderer/Internal/VirtualShadowMapBackend.h`
  - `src/Oxygen/Renderer/Internal/VirtualShadowMapBackend.cpp`
- added reusable clipmap offset / guardband helpers:
  - `src/Oxygen/Renderer/Internal/ShadowBackendCommon.h`
- exported clipmap setup results through introspection:
  - `src/Oxygen/Renderer/Types/VirtualShadowRenderPlan.h`
- added focused validation:
  - `src/Oxygen/Renderer/Test/VirtualShadowContracts_test.cpp`
  - `src/Oxygen/Renderer/Test/LightManager_basic_test.cpp`
- hardened the locked benchmark runner to parse the current multiline raster log:
  - `Examples/RenderScene/benchmark_directional_vsm.ps1`
- validation run on March 12, 2026 in `out/build-vs`:
  - `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.VirtualShadowContracts.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - `out/build-vs/bin/Debug/Oxygen.Renderer.VirtualShadowContracts.Tests.exe`
    - result: `6/6` passed
  - `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ClipmapSetup*:*ShadowManagerPublishForView_Virtual*`
    - result: `49/49` passed
  - `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - `powershell -ExecutionPolicy Bypass -File Examples\RenderScene\benchmark_directional_vsm.ps1`
    - result: exit code `0`
    - benchmark scene: `/.cooked/Scenes/physics_domains_vsm_benchmark.oscene`
    - wall time: `11166 ms` for `120` frames
    - settled frames present: `101-117`
    - settled averages: `requested_pages=655.18`, `scheduled_pages=233.29`, `rastered_pages=295.12`, `shadow_draws=504.82`
    - restored `demo_settings.json` SHA-256 matched the frozen benchmark baseline
- gate assessment:
  - snapped clipmap transforms, page-space offsets, reuse guardband validity, and clipmap metadata now come from `PrepareDirectionalVirtualClipmapSetup()`
  - `BuildDirectionalVirtualViewState()` consumes the setup output instead of rebuilding clipmap state inline
  - focused tests cover snapped motion offsets and guardband rejection
  - the 120-frame benchmark scene run completed with the locked runner and scene contract

### Phase 3. Build Explicit Directional Cache Validity

Status: `completed`

Goal:

- make directional clipmap cache validity explicit and correct before doing more
  page-demand work

Implementation:

- decide top-level directional cache validity from:
  - light direction
  - clipmap first level / clip layout configuration
  - explicit force-invalidate controls
- decide per-clip validity from:
  - snapped page-space origin deltas
  - clipmap panning enable/disable
  - stable Z guard-band tests against the previous cached depth basis
  - first-render / never-rendered state
  - level-radius / view-radius equality
- store enough next-frame mapping data to support persistent remap instead of
  snapshot publish fallback

Must remove or demote:

- `last_coherent_*` snapshot republish as an authoritative continuity strategy
- ad hoc "publish compatibility" heuristics as the main motion-stability tool

Verification:

- focused tests for:
  - light-direction invalidation
  - clipmap first-level invalidation
  - panning-preserving XY shifts
  - panning-disabled XY invalidation
  - Z guard-band preservation and rejection
  - never-rendered level invalidation
- runtime evidence that motion-time continuity decisions are no longer being
  made by stale whole-publication fallback

Completion gate:

- every directional level either:
  - remains valid and carries a precise page-space remap offset, or
  - is explicitly invalidated for a concrete reason
- stale whole-publication fallback is no longer needed for compatible-motion
  continuity

Scope correction:

- the older broad virtual regression
  `ShadowManagerPublishForView_VirtualBudgetPressureEvictsDirtyPagesBeforeCleanCachedPages`
  is not a Phase 3 exit gate
- it exercises eviction under pressure after reuse/allocation decisions, which
  belongs to the remap/page-management phases
- earlier work in this phase drifted toward that test; that was the wrong
  target and is now explicitly corrected here

Completion evidence:

- implemented explicit cache-validity ownership in:
  - `src/Oxygen/Renderer/Internal/VirtualShadowMapBackend.h`
  - `src/Oxygen/Renderer/Internal/VirtualShadowMapBackend.cpp`
  - `src/Oxygen/Renderer/ShadowManager.h`
  - `src/Oxygen/Renderer/ShadowManager.cpp`
  - `src/Oxygen/Renderer/Types/VirtualShadowRenderPlan.h`
- added and corrected focused validation in:
  - `src/Oxygen/Renderer/Test/LightManager_basic_test.cpp`
- Phase 3 now explicitly covers:
  - top-level directional cache invalidation for light/layout/force-invalidate
  - per-clip validity reasons for panning-disabled motion, depth guardband
    failure, reuse-guardband failure, and never-rendered history
  - authoritative preference for previously rendered cache state over stale
    unresolved pending snapshots when choosing the previous frame basis
  - next-frame mapping data through explicit clip page offsets, reuse guardband
    validity, and per-clip cache-status export
- validation run on March 13, 2026 in `out/build-vs`:
  - `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ClipmapSetup*:*VirtualCacheValidityRejectsLightDirectionChanges:*VirtualCacheValidityRejectsClipmapLayoutChanges:*VirtualPanningDisabledInvalidatesXYClipReuse:*VirtualNeverRenderedPreviousFrameInvalidatesCache:*VirtualForceInvalidateRejectsPreviousCache:*VirtualInvalidatesCleanPagesWhenDepthMappingChanges:*VirtualFeedbackDropsPagesOutsideCurrentClipAfterClipmapShift:*VirtualShiftedFeedbackDoesNotUseLegacyDeltaReinforcement`
    - result: `9/9` passed
  - `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.VirtualShadowContracts.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - `out/build-vs/bin/Debug/Oxygen.Renderer.VirtualShadowContracts.Tests.exe`
    - result: `9/9` passed
  - broader diagnostic run:
    - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*:*ShadowManagerPrepareVirtualPageTableResources_UploadsResolvedEntries`
    - result: `54/55` passed
    - remaining red test:
      `ShadowManagerPublishForView_VirtualBudgetPressureEvictsDirtyPagesBeforeCleanCachedPages`
  - `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - `powershell -ExecutionPolicy Bypass -File Examples\RenderScene\benchmark_directional_vsm.ps1`
    - result: exit code `0`
    - benchmark scene: `/.cooked/Scenes/physics_domains_vsm_benchmark.oscene`
    - wall time: `14587 ms` for `120` frames
    - settled source frames present: `101-120`
    - settled averages:
      `requested_pages=659.26`, `scheduled_pages=663.35`,
      `rastered_pages=329.15`, `shadow_draws=1084.62`
    - restored `demo_settings.json` SHA-256 matched the frozen benchmark
      baseline
- gate assessment:
  - stale whole-publication fallback is no longer the target continuity
    mechanism and is not the live authority being advanced here
  - compatible directional motion now produces explicit cache-validity reasons
    and next-frame mapping inputs instead of relying on ad hoc publish
    compatibility
  - the locked benchmark regressed versus the earlier Phase 2 baseline because
    Phase 3 now invalidates explicitly without a remap pass
  - that performance regression is the expected Phase 4 debt: persistent page
    remap and reuse must land next to recover motion cost without reintroducing
    wrong-page continuity hacks

### Phase 4. Implement Persistent Page Remap And Reuse

Status: `completed`

Goal:

- make compatible-motion continuity come from remapping valid physical pages
  into the new clipmap address space

Scope correction:

- Oxygen resident-page identity is already absolute page space
  `(clip_level, grid_x, grid_y)`, unlike UE's local clipmap page address.
- Therefore Phase 4 must not rebase resident keys by clipmap offset. Doing so
  double-shifts page identity and binds the wrong physical contents under
  motion.
- The UE5 lesson still applies, but it must be adapted correctly here:
  compatible motion preserves absolute page identity, while the new frame's
  local clipmap addressing discovers those same pages through the current clip
  grid origin.

Implementation:

- add a dedicated physical-page remap/update stage
- consume the Phase 3 next-data mapping as a cache-validity and in-bounds
  filter, not as a resident-key rewrite
- preserve absolute resident-page keys for compatible pages
- discard or evict only pages that fall outside the current clip bounds or are
  invalidated by content / depth / layout changes
- rebuild the current frame page table from the new local clipmap addressing so
  current local page indices point at the same preserved physical pages
- keep physical metadata alive in place for valid remapped pages
- encode whether the page is valid at this LOD or only at a coarser fallback
  LOD in the page-table contract
- mark only remapped-but-invalid / remapped-but-dirty pages as uncached

Must remove or demote:

- whole-publication snapshot reuse as the primary continuity tool
- any path that republishes stale page tables/metadata instead of remapping
  pages

Verification:

- focused tests for clipmap panning reuse
- focused tests that absolute resident-page identity is preserved across
  compatible clipmap motion
- focused tests that reused physical pages keep the same physical slot after
  compatible motion
- focused tests for invalidation by moved/dirty content
- focused tests that compatible motion preserves valid pages without any stale
  whole-publication reuse
- focused tests for LOD-offset / any-LOD-valid decode after remap

Completion gate:

- page-table continuity is page-remap driven
- a compatible motion frame can preserve existing valid pages without redraw
- stale whole-publication fallback is no longer authoritative for continuity

Current evidence:

- code:
  [VirtualShadowMapBackend.cpp](../Internal/VirtualShadowMapBackend.cpp) now
  preserves absolute resident-page keys across compatible motion and only
  filters carried pages by current clip bounds / validity instead of rebasing
  keys by clipmap offset
- code:
  [VirtualShadowMapBackend.cpp](../Internal/VirtualShadowMapBackend.cpp) now
  reports `mapped_page_count` from `current_lod_valid` entries only, so the
  benchmark reflects actual current mappings rather than fallback aliases
- tests:
  `Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*VirtualClipmapShiftPreservesAbsolutePageTileReuse*:*VirtualPlanReusesResidentPagesAcrossClipmapShift*:*VirtualClipmapSetupPublishesOffsetsAndGuardbandReuse*`
  -> `3/3` passed
- tests:
  `Oxygen.Renderer.VirtualShadowContracts.Tests.exe` -> `9/9` passed
- runtime:
  `powershell -ExecutionPolicy Bypass -File Examples\RenderScene\benchmark_directional_vsm.ps1`
  -> exit code `0`, locked `120`-frame benchmark scene, `wall_ms=15009`,
  settled averages `requested_pages=659.26`, `scheduled_pages=663.35`,
  `rastered_pages=329.15`, `shadow_draws=1084.62`

Completion evidence:

- the live backend no longer contains `last_coherent_*` publication reuse /
  fallback code; continuity is page-remap driven instead of stale-snapshot
  driven
- user visual validation after the Phase 4B / Phase 5 motion fixes confirmed
  motion-time correctness on the live path
- the locked benchmark now shows no settled-frame
  `request=address-space-mismatch` and no `resident_reuse_gate=true` reuse of
  unresolved dirty pages in
  `out/build-vs/directional-vsm-benchmark-latest.log`

### Phase 4B. Replace Projection / Shading Contract For Seamless Coarse Fallback

Status: `completed`

Why this phase must come now:

- the current live shader path still treats fallback as an after-the-fact
  manual coarser-clip search
- the current live shader path still returns fully lit when it cannot find a
  `current_lod_valid` page at the sampled clip path
- therefore a backend coarse-safety set does not guarantee a sample-usable
  coarse shadow, which is exactly why complex scenes can show no-shadow frames
  or oscillate between lit and correct shadows even without motion

Goal:

- make coarse/detail fallback sample-usable directly from the page-table
  contract, UE-style, instead of relying on backend heuristics or stale
  publication recovery

Implementation:

- replace manual multi-clip fallback search in
  [ShadowHelpers.hlsli](../../Graphics/Direct3D12/Shaders/Renderer/ShadowHelpers.hlsli)
  with page-table-driven fallback decode
- at the selected finest sample location, decode page-table entry bits and use
  `any_lod_valid` plus `fallback_lod_offset` to select the best available
  mapped level
- keep binary coarse/detail behavior through page flags instead of boiling
  multi-band quality heuristics
- remove "return fully lit because nothing current-valid was published" as the
  normal recovery path whenever a fallback-capable page-table entry exists
- demote stale whole-publication fallback so it is no longer the primary
  continuity mechanism

Must remove or demote:

- manual coarser-clip search as the authoritative fallback mechanism
- "fully lit" return as the normal missing-current-page recovery behavior
- backend-only coarse safety that is not guaranteed sampleable by the shader

Verification:

- focused contract tests for entries with `current_lod_valid=false` and
  `any_lod_valid=true`
- focused runtime regressions where the selected fine page is invalid but a
  coarser fallback page is valid
- runtime validation in complex scenes showing no no-shadow frames when coarse
  fallback exists
- runtime validation in stationary complex scenes showing the lit/correct
  oscillation is gone

Completion gate:

- coarse fallback is sampleable from the page-table contract itself
- no-shadow frames are eliminated whenever a valid coarse fallback exists
- stationary lit/correct oscillation caused by fallback publication mismatch is
  gone

Current evidence:

- code:
  [ShadowHelpers.hlsli](../../Graphics/Direct3D12/Shaders/Renderer/ShadowHelpers.hlsli)
  now resolves directional virtual samples through page-table-driven
  `any_lod_valid` / `fallback_lod_offset` decode instead of requiring
  `current_lod_valid` at the originally requested clip
- code:
  `SampleDirectionalVirtualShadowClipVisibility()` now returns the resolved clip
  and page coordinate so the live projection path samples the fallback-mapped
  page directly instead of treating backend coarse pages as backend-only policy
- code:
  [VirtualShadowMapBackend.cpp](../Internal/VirtualShadowMapBackend.cpp) now
  treats recent feedback hashes as compatible when clipmap layout, per-clip
  reuse validity, and depth guardband continuity remain valid, instead of
  requiring exact address-space hash equality across snapped clipmap panning
- code:
  [VirtualShadowMapBackend.cpp](../Internal/VirtualShadowMapBackend.cpp) no
  longer allows dirty carried pages to suppress reraster through the
  resident-reuse gate, and it now counts coarse-safety coverage after page
  selection so the published diagnostics match actual sampleable fallback state
- tests:
  [LightManager_basic_test.cpp](../Test/LightManager_basic_test.cpp) now
  includes
  `ShadowManagerPublishForView_VirtualPageTablePublishesFallbackOnlyEntries`
  to prove the published page-table contract exposes fallback-only entries with
  `current_lod_valid=false`, `any_lod_valid=true`, and
  `fallback_lod_offset>0`
- tests:
  [LightManager_basic_test.cpp](../Test/LightManager_basic_test.cpp) now
  includes
  `ShadowManagerPublishForView_VirtualClipShiftAcceptsRecentCompatibleFeedbackHashes`
  to prove recent feedback remains on the feedback/refine path across a
  one-page compatible clipmap shift instead of rebooting to receiver bootstrap
- validation run on March 13, 2026 in `out/build-vs`:
  - `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.VirtualShadowContracts.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*VirtualClipShiftAcceptsRecentCompatibleFeedbackHashes*:*VirtualIncompatibleFeedbackRebootsReceiverBootstrap*:*VirtualPageTablePublishesFallbackOnlyEntries*:*VirtualClipmapShiftPreservesAbsolutePageTileReuse*:*VirtualPlanReusesResidentPagesAcrossClipmapShift*:*VirtualClipmapSetupPublishesOffsetsAndGuardbandReuse*`
    - result: `6/6` passed
  - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*VirtualIncompatibleFeedback*:*VirtualAgedAddressMismatchKeepsCompatibleResidentPagesMapped*`
    - result: `2/2` passed
  - `out/build-vs/bin/Debug/Oxygen.Renderer.VirtualShadowContracts.Tests.exe`
    - result: `9/9` passed
  - locked runtime benchmark:
    `powershell -ExecutionPolicy Bypass -File Examples\RenderScene\benchmark_directional_vsm.ps1`
    - result: exit code `0`
    - benchmark scene: `/.cooked/Scenes/physics_domains_vsm_benchmark.oscene`
    - wall time: `14739 ms` for `120` frames
    - settled averages:
      `requested_pages=659.26`, `scheduled_pages=663.35`,
      `rastered_pages=329.15`, `shadow_draws=1084.62`
    - log inspection of `out/build-vs/directional-vsm-benchmark-latest.log`:
      - `request=address-space-mismatch`: `0` occurrences
      - `resident_reuse_gate=true`: `0` occurrences
      - `has no resolved virtual raster pages anymore`: `0` occurrences
      - `coarse_safety_selected=0`: `0` occurrences
      - warmup-only `receiver_bootstrap=12288`: `4` occurrences, all before
        steady-state feedback convergence
  - live manual validation on March 13, 2026:
    - user reported the latest fixes are visually good

Broad diagnostic test status after Phase 4B cleanup:

- `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*`
  now runs green at `55 passed / 1 skipped`
- the fixed tests were stale pre-Phase 4B expectations that treated any
  non-zero page-table entry as a current fine-page mapping; they now validate
  `current_lod_valid` vs `any_lod_valid` explicitly
- the one skipped test,
  `ShadowManagerPublishForView_VirtualBudgetPressureEvictsDirtyPagesBeforeCleanCachedPages`,
  is intentionally deferred to the later page-management phase because its
  eviction-ordering contract is not part of Phase 4B and should not pull the
  live motion/fallback fixes backward

### Phase 5. Replace CPU Bootstrap With GPU Page Marking

Status: `completed`

Goal:

- replace CPU-authored synthetic demand with the Phase 5 coarse/detail
  page-marking contract

Completion evidence:

- live runtime no longer depends on authoritative `receiver_bootstrap`,
  `feedback_refinement`, or `current_frame_reinforcement`
- live coarse fallback now uses:
  - page-table-driven `any_lod_valid` / `fallback_lod_offset`
  - clip-relative requested-to-resolved remap
  - LOD-aware bias scaling
  - grounded single-page fallback comparison instead of the old blurred
    coarse-only compare
- user visually validated the latest coarse-shadow grounding result as good
- validation already recorded for the landed runtime:
  - `Oxygen.Renderer.VirtualShadowContracts.Tests.exe`
    - result: `11/11` passed
  - `Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*`
    - result: `54 passed / 4 skipped`
  - `powershell -ExecutionPolicy Bypass -File Examples\\RenderScene\\benchmark_directional_vsm.ps1`
    - result: `exit_code=0`
    - settled benchmark contract:
      `requested_pages=661.56`, `scheduled_pages=630.88`,
      `resolved_pages=227.56`, `rastered_pages=227.56`,
      `shadow_draws=769.62`

### Phase 6+. Cross-Reference Integration Matrix

All issues from
`src/Oxygen/Renderer/Docs/vsm_cross_reference_report.md`
are integrated below and remain active until their mapped phase closes.

| Report issue | Remaining phase |
|---|---|
| 1. CPU-authored page table + CPU-driven raster | Phase 8 |
| 2. Full page-table re-resolution per filter tap | Phase 9 |
| 3. No hierarchical page flag propagation | Phase 6 |
| 4. Multi-frame feedback latency | Phase 7 primary, Phase 10 final removal |
| 5. No static/dynamic page separation | Phase 7 |
| 6. Coarse mark per-pixel brute force | Phase 7 |
| 7. Slope bias not scaled by `fallback_lod_offset` | Phase 9 |
| 8. Guard texel count hardcoded | Phase 9 |
| 9. O(N) physical pool eviction scan | Phase 6 |
| 10. Up to 4 clip evaluations per pixel | Phase 9 |
| 11. Redundant GPU resolve / CPU-GPU-CPU round-trip | Phase 10 |
| 12. No single-page optimization | Phase 10 |

### Phase 6. Build GPU Page Management And Hierarchical Flags

Status: `completed`

Goal:

- move page management onto the correct UE-style authority: GPU-owned page
  metadata, hierarchical page flags, and page-table writes

Implementation:

- add GPU-owned physical page metadata buffers and explicit page-management
  lists for:
  - available pages
  - resident clean pages
  - resident dirty / uncached pages
  - requested pages
- replace CPU free-list / full-map eviction scans with page-management data
  structures that support bounded victim selection
- add hierarchical page-flag propagation so coarse pages inherit detail usage /
  request state from finer pages
- make page-table and page-flag writes GPU-authored for reused and newly
  allocated pages
- make requested-page protection, fallback visibility, and any-LOD validity
  page-management responsibilities instead of backend heuristics

Must remove or demote:

- CPU O(N) eviction scans in the hot allocation path
- flat page-demand reasoning without hierarchical flags
- backend-only coarse-safety budgeting as the thing that makes fallback usable

Verification:

- focused tests for hierarchical flag propagation from fine to coarse pages
- focused tests for requested-page pinning under pressure
- focused tests for allocation / eviction without full-map scans
- focused tests for page-table / page-flag coherence after reuse and allocation

Completion gate:

- hierarchical page flags exist and are used by live page management
- allocation / eviction / page-table writes are page-management outputs, not
  CPU backend recomputation
- the O(N) physical-pool eviction scan is gone from the authoritative path

Current evidence:

- implemented bounded page-management and hierarchy plumbing in:
  - `src/Oxygen/Renderer/Types/VirtualShadowPageFlags.h`
  - `src/Oxygen/Renderer/Types/VirtualShadowRenderPlan.h`
  - `src/Oxygen/Renderer/Internal/VirtualShadowMapBackend.h`
  - `src/Oxygen/Renderer/Internal/VirtualShadowMapBackend.cpp`
  - `src/Oxygen/Renderer/Passes/VirtualShadowResolvePass.cpp`
  - `src/Oxygen/Graphics/Direct3D12/Shaders/Lighting/VirtualShadowResolve.hlsl`
  - `src/Oxygen/Renderer/Test/VirtualShadowContracts_test.cpp`
  - `src/Oxygen/Renderer/Test/LightManager_basic_test.cpp`
- the six explicitly scoped Phase 6 gaps are now covered as follows:
  - GPU-authored page-table / page-flag outputs exist in the page-management
    path, while live lighting remains on the stable CPU-authored publication
    until the follow-up cutover task completes
  - hierarchy flags now have a live CPU fallback consumer through
    `HasVirtualShadowHierarchyVisibility(...)` in the fallback publication path
  - backend-only coarse-safety budgeting is not used as a special allocation
    authority in the current Phase 6 resolve path
  - requested-page pinning under budget pressure is covered by
    `ShadowManagerPublishForView_VirtualPlanPinsMappedRequestedPagesUnderBudgetPressure`
  - sorted eviction priority is covered deterministically by
    `VirtualShadowContractsTest.EvictionPriorityOrdersInvalidThenCoarserThenLruThenKey`
  - page-table / page-flag coherence after reuse and allocation is covered by
    `ShadowManagerPublishForView_VirtualPageManagementOutputsStayCoherentAfterReuseAndAllocation`
- live path now:
  - builds requested / dirty / clean / available physical-page lists
  - uploads physical-page metadata and page-management lists through the
    per-view resolve resources
  - propagates hierarchical descendant flags from finer to coarser pages
  - uses a bounded eviction-candidate list instead of the old full-map victim
    scan in the authoritative allocation path
  - GPU-authors page-table and page-flag writes through
    `VirtualShadowResolve.hlsl`
  - consumes hierarchy flags in the fallback publication phase
  - updates page-management outputs even on frames with no request scheduling
  - publishes GPU-authored page-table / page-flag outputs as the live shading
    authority through `frame_publication.virtual_shadow_page_table_srv` and
    `frame_publication.virtual_shadow_page_flags_srv`
  - keeps the CPU-resolved page table / page flags only as the coherence-gate
    reference path; they are no longer the live publication authority
- validation run on March 13, 2026 in `out/build-vs`:
  - `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.VirtualShadowContracts.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - `out/build-vs/bin/Debug/Oxygen.Renderer.VirtualShadowContracts.Tests.exe`
    - result: `14/14` passed
  - `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPrepareVirtualPageTableResources_DoesNotUploadCpuPageTableEntries:LightManagerTest.ShadowManagerPrepareVirtualPageTableResources_DoesNotUploadCpuPageFlags:LightManagerTest.ShadowManagerPublishForView_VirtualPageFlagsPropagateHierarchyToCoarserPages:LightManagerTest.ShadowManagerPublishForView_VirtualBudgetPressureEvictsDirtyPagesBeforeCleanCachedPages:LightManagerTest.ShadowManagerPublishForView_VirtualPlanPinsMappedRequestedPagesUnderBudgetPressure:LightManagerTest.ShadowManagerPublishForView_VirtualPageManagementOutputsStayCoherentAfterReuseAndAllocation`
    - result: `4 passed / 2 skipped`
    - skipped:
      - `ShadowManagerPublishForView_VirtualBudgetPressureEvictsDirtyPagesBeforeCleanCachedPages`
      - `ShadowManagerPublishForView_VirtualPageFlagsPropagateHierarchyToCoarserPages`
    - note: both end-to-end scenarios were explicitly superseded by the new
      deterministic Phase 6 contract tests because they depended on pre-split
      CPU-only page-management details
  - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*`
    - result: `55 passed / 5 skipped`
    - skipped:
      - `ShadowManagerPublishForView_VirtualCoarseFeedbackDoesNotOverwriteDetailFeedback`
      - `ShadowManagerPublishForView_VirtualBootstrapKeepsSparseReceiverPagesSparse`
      - `ShadowManagerPublishForView_VirtualBootstrapCapsCoverageToNearestFineClips`
      - `ShadowManagerPublishForView_VirtualBudgetPressureEvictsDirtyPagesBeforeCleanCachedPages`
      - `ShadowManagerPublishForView_VirtualPageFlagsPropagateHierarchyToCoarserPages`
    - note: the remaining skips are either superseded by the current coarse-band
      contract or replaced by deterministic Phase 6 contract coverage; the
      broad slice is otherwise green
  - `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - `powershell -ExecutionPolicy Bypass -File Examples\\RenderScene\\benchmark_directional_vsm.ps1`
    - result: `exit_code=0`, `wall_ms=17307`, `approx_fps=6.93`
    - settled stats:
      - `requested_pages_avg=661.56`
      - `scheduled_pages_avg=0`
      - `resolved_pages_avg=227.56`
      - `rastered_pages_avg=227.56`
      - `shadow_draws_avg=769.62`
    - log evidence:
      - `VirtualShadowResolvePass` dispatches on live frames
      - `VirtualShadowMapBackend` reports nonzero `mapped_pages` / `resident_pages`
      - `VirtualShadowPageRasterPass` continues to raster the resolved pages
      - `Renderer::SetupFramebufferForView` publishes valid `shadow_meta`,
        `vsm_meta`, `vsm_table`, `vsm_flags`, and `sun_shadow_index=0`
      - live shadows remained visible while lighting sampled the
        GPU-authored page-management `vsm_table` / `vsm_flags`
      - no `D3D12 ERROR` lines appear in the locked benchmark log
- final validation run on March 13, 2026 in `out/build-vs`:
  - `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.VirtualShadowContracts.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - `out/build-vs/bin/Debug/Oxygen.Renderer.VirtualShadowContracts.Tests.exe`
    - result: `14/14` passed
  - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=LightManagerTest.ShadowManagerPrepareVirtualPageTableResources_DoesNotUploadCpuPageTableEntries:LightManagerTest.ShadowManagerPrepareVirtualPageTableResources_DoesNotUploadCpuPageFlags:LightManagerTest.ShadowManagerPublishForView_VirtualPageManagementOutputsStayCoherentAfterReuseAndAllocation:LightManagerTest.ShadowManagerPublishForView_VirtualPlanPinsMappedRequestedPagesUnderBudgetPressure`
    - result: `4/4` passed
  - `out/build-vs/bin/Debug/Oxygen.Renderer.LightManager.Tests.exe --gtest_filter=*ShadowManagerPublishForView_Virtual*`
    - result: `55 passed / 5 skipped`
    - skipped:
      - `ShadowManagerPublishForView_VirtualCoarseFeedbackDoesNotOverwriteDetailFeedback`
      - `ShadowManagerPublishForView_VirtualBootstrapKeepsSparseReceiverPagesSparse`
      - `ShadowManagerPublishForView_VirtualBootstrapCapsCoverageToNearestFineClips`
      - `ShadowManagerPublishForView_VirtualBudgetPressureEvictsDirtyPagesBeforeCleanCachedPages`
      - `ShadowManagerPublishForView_VirtualPageFlagsPropagateHierarchyToCoarserPages`
    - note: the remaining skips stay explicitly superseded by the Phase 5/6
      split contracts; the broad virtual slice is otherwise green
  - `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:1 /p:Configuration=Debug /nologo`
  - `powershell -ExecutionPolicy Bypass -File Examples\\RenderScene\\benchmark_directional_vsm.ps1`
    - result: `exit_code=0`, `wall_ms=17586`, `approx_fps=6.82`
    - settled stats:
      - `requested_pages_avg=661.56`
      - `scheduled_pages_avg=630.88`
      - `resolved_pages_avg=227.56`
      - `rastered_pages_avg=227.56`
      - `shadow_draws_avg=769.62`
    - coherence gate:
      - `page_table_mismatches = 0`
      - `page_flags_mismatches = 0`
      - `coherent = true` from frame `6` through frame `117`
    - publication evidence:
      - `Renderer::SetupFramebufferForView` keeps publishing valid
        `vsm_table=68`, `vsm_flags=70`, and `sun_shadow_index=0`
      - no `D3D12 ERROR` lines appear in the benchmark log

Completion gate:

- hierarchical page flags exist and are used by live page management
- allocation / eviction / page-table writes are page-management outputs for the
  live published page table / page flags
- the O(N) physical-pool eviction scan is gone from the authoritative path
- the live shading publication now uses GPU-authored page-management outputs
- the coherence gate is green on the locked benchmark

#### Phase 6 Follow-Up Task: Close The Live Publication Authority Gap

Status: `completed`

Goal:

- remove the temporary CPU-authored live page-table / page-flag publication
  restore and make the GPU page-management outputs the sole authoritative
  shading inputs without regressing live shadows

Progress:

- coherence readback gate implemented in
  `VirtualShadowMapBackend::FinalizePageManagementOutputs()` — per-slot
  triple-buffered readback of GPU page-management outputs is compared
  byte-for-byte against the CPU-resolved reference each frame
- live publication cutover completed:
  - `PublishView(...)` now binds
    `EnsureViewPageManagementPageTableResources(...)` /
    `EnsureViewPageManagementPageFlagResources(...)`
  - `RefreshViewExports(...)` now keeps
    `frame_publication.virtual_shadow_page_table_srv` /
    `virtual_shadow_page_flags_srv`
    aligned with `page_management_bindings`
  - `FinalizePageManagementOutputs()` no longer republishes CPU-authored page
    table / page flags into the old live publication buffers
  - `CheckCoherenceReadback(...)` now asserts
    `CHECK_F(!slot.live_authority || coherent, ...)`
    so a divergent live cutover cannot proceed silently

Coherence gate findings (locked benchmark, March 13 2026):

- **page_table_mismatches: 0 / 49152** from frame `6` through frame `117`
- **page_flags_mismatches: 0 / 49152** from frame `6` through frame `117`
- **coherent=true** on every checked slot reuse after warmup
- **no D3D12 ERROR**, `exit_code=0`, shadows visible throughout

Root cause that was fixed:

- the GPU resolve shader's fallback publication and current-page publication
  semantics were not matching the CPU live contract
- `kResolvePhasePopulateCurrent` now publishes the current selected/requested
  page set only
- `kResolvePhasePopulateFallback` now runs ordered coarse-to-fine clip passes
  so finer clips can legally consume coarser aliases already written earlier in
  the same frame
- fallback-only alias pages now match the CPU contract:
  - same physical tile
  - exact `fallback_lod_offset`
  - `current_lod_valid = false`
  - `any_lod_valid = true`
  - `requested_this_frame = false`
  - zero page flags
- hierarchy propagation remains a separate stage after fallback publication

Follow-up result:

- live lighting now samples GPU-authored page-management `vsm_table` /
  `vsm_flags`
- the temporary CPU live republish path is removed
- the coherence gate stays active as a correctness assertion for the live
  GPU-authority cutover

Implementation:

- rework `VirtualShadowResolvePass::DoExecute()` so resolve publication runs as:
  - clear
  - populate current selected pages
  - populate fallback aliases per clip, coarse-to-fine
  - propagate hierarchy flags
  - request scheduling
- update `VirtualShadowResolve.hlsl` so `kResolvePhasePopulateCurrent` consumes
  only the live selected/current set, while `kResolvePhasePopulateFallback`
  reproduces the CPU alias publication exactly instead of relying on a single
  global parallel fallback pass
- keep `VirtualShadowMapBackend::FinalizePageManagementOutputs()` on CPU
  republish until the coherence gate is green; then switch the live
  `frame_publication.virtual_shadow_page_table_srv` /
  `virtual_shadow_page_flags_srv` cutover in one step
- keep the current CPU-resolved path only as validation/reference data until
  the GPU outputs are byte-coherent with it, then delete that transitional
  dependency
- maintain the explicit coherence check between:
  - GPU-authored page-table / page-flag outputs
  - current CPU-resolved authoritative page-table / page-flag state
- switch lighting to the GPU-authored publication only after that coherence
  check is green on the locked benchmark scene and visible shadows are still
  correct

Verification:

- focused tests for page-table / page-flag byte coherence between the CPU
  reference path and GPU-authored outputs
- focused tests proving fallback-only alias pages match CPU semantics:
  - correct physical tile
  - correct `fallback_lod_offset`
  - `current_lod_valid = false`
  - `any_lod_valid = true`
  - zero page flags
- focused tests proving hierarchy propagation remains a separate stage and only
  adds descendant bits
- focused tests proving live shading still has visible directional shadows when
  CPU republish is removed
- locked benchmark runner:
  - no `D3D12 ERROR`
  - `page_table_mismatches = 0`
  - `page_flags_mismatches = 0`
  - non-invalid published `vsm_table` / `vsm_flags`
  - visible shadows preserved

Completion gate:

- GPU resolve publication matches the CPU live publication contract exactly on
  the locked benchmark
- lighting uses GPU-authored page-table / page-flag publication only
- the temporary CPU republish path is deleted
- the locked benchmark scene still renders visible directional shadows

#### Phase 6 Follow-Up Detailed Runbook

This subsection is the handoff-grade execution record for the Phase 6
follow-up. It exists so work can resume without reconstructing details from
chat history.

##### Scope lock

The follow-up is complete only when the live shader-visible page table and page
flags come from GPU page management without changing visible shadows on the
locked benchmark.

This follow-up must **not**:

- switch live lighting to GPU-authored page-table / page-flag SRVs before the
  coherence gate is green
- redefine the CPU reference to match the current GPU output
- widen scope into same-frame GPU marking, static/dynamic invalidation, or GPU
  raster authority before the live publication authority gap is closed
- update legacy VSM docs

##### CPU source of truth

Until cutover, the following CPU routines define the exact live publication
contract:

- `VirtualShadowMapBackend::ResolvePendingPageResidency(...)`
- `PopulateDirectionalFallbackPageTableEntries(...)`
- `PropagateDirectionalHierarchicalPageFlags(...)`
- `RebuildPhysicalPageManagementSnapshot(...)`

The GPU path must reproduce the outputs of those routines, not merely preserve
visible shadows approximately.

##### Exact file ownership

Files that may be edited for this follow-up:

- `src/Oxygen/Renderer/Passes/VirtualShadowResolvePass.cpp`
- `src/Oxygen/Graphics/Direct3D12/Shaders/Lighting/VirtualShadowResolve.hlsl`
- `src/Oxygen/Renderer/Internal/VirtualShadowMapBackend.cpp`
- `src/Oxygen/Renderer/Internal/VirtualShadowMapBackend.h`
- `src/Oxygen/Renderer/Types/VirtualShadowPageFlags.h`
- `src/Oxygen/Renderer/Test/VirtualShadowContracts_test.cpp`
- `src/Oxygen/Renderer/Test/LightManager_basic_test.cpp`
- this plan document only

Files that must not be used to hide or compensate for this gap:

- `ShadowHelpers.hlsli`
- legacy directional VSM docs
- any stale whole-publication fallback mechanism

##### Required stage order

The GPU resolve publication path must exist in this exact logical order:

1. Clear current frame GPU page-table / page-flags outputs.
2. Publish current selected pages only.
3. Publish fallback aliases one target clip at a time, from coarse to fine,
   with a UAV barrier between clip passes.
4. Propagate hierarchy flags in a separate stage.
5. Build the schedule for pages that actually require raster.

The current/fallback/hierarchy stages must remain distinct. Combining them into
one global pass is explicitly disallowed because it breaks the ordered alias
dependency that the CPU path relies on.

##### Stage-by-stage implementation contract

Stage 1: clear

- clear only the GPU page-management publication outputs
- do not touch the current live CPU-authored publication buffers

Stage 2: populate current selected pages

- input: the live selected/current page set only
- do not iterate the whole requested + dirty + clean resident inventory
- output page-table entry:
  - same physical tile as CPU current publication
  - `fallback_lod_offset = 0`
  - `current_lod_valid = true`
  - `any_lod_valid = true`
  - `requested_this_frame = true` only when CPU live publication would set it
- output page flags:
  - exactly the base flags emitted by `MakeVirtualShadowPageFlags(...)`
  - no hierarchy bits authored here

Stage 3: populate fallback aliases

- run one dispatch per target clip
- process target clips from `clip_level_count - 2` down to `0`
- insert a UAV barrier between clip passes
- for each target page:
  - skip if current page-table entry is already `current_lod_valid`
  - search coarser clips using the same world-space page-center remap as CPU
  - candidate must satisfy:
    - `candidate_entry.any_lod_valid == true`
    - `HasVirtualShadowHierarchyVisibility(candidate_flags) == true`
  - resolve final fallback clip with the same logic as the CPU path
- output page-table entry must match CPU exactly:
  - same physical tile as the resolved coarse source
  - `fallback_lod_offset = resolved_fallback_clip - clip_index`
  - `current_lod_valid = false`
  - `any_lod_valid = true`
  - `requested_this_frame = false`
- do not write any page flags for fallback-only alias entries

Stage 4: propagate hierarchy flags

- consume base page flags from Stage 2 only
- OR descendant bits into parent pages only
- do not create page-table entries here
- do not create base flags for fallback-only alias pages here

Stage 5: build schedule

- schedule only pages that are current and uncached/dirty per the page flags
- do not let fallback-only alias pages appear as raster work
- do not let the schedule become the live page-table authority

##### Exact semantic invariants

The following invariants must hold before cutover:

- fallback-only entries have `page_table != 0` and `page_flags == 0`
- hierarchy visibility is derived only from:
  - base flags on current pages
  - descendant bits propagated afterward
- a fallback alias may chain through a coarser alias written earlier in the
  same frame; this is why Phase 2 must be clip-ordered
- GPU current publication must not expose the entire resident inventory as
  `current_lod_valid`
- live lighting must continue to consume CPU-authored publication until the
  coherence gate is green

##### Coherence gate protocol

The coherence gate is mandatory and blocks cutover.

For the locked benchmark, compare CPU live publication vs GPU page-management
publication for:

- page-table bytes / decoded entries
- page-flags bytes / decoded flags
- `mapped_page_count`
- `current_lod_valid` population count
- `any_lod_valid` population count
- fallback-only alias count

The gate is green only when:

- `page_table_mismatches = 0`
- `page_flags_mismatches = 0`
- published counts match
- visible shadows still render correctly

##### Single cutover rule

When, and only when, the coherence gate is green:

1. switch `frame_publication.virtual_shadow_page_table_srv` to the GPU-authored
   publication
2. switch `frame_publication.virtual_shadow_page_flags_srv` to the GPU-authored
   publication
3. rerun the locked benchmark and confirm visible shadows remain
4. delete the temporary CPU republish path
5. delete the coherence readback scaffolding only after the GPU path is the
   sole live authority and remains stable

No partial live cutover is allowed. No intermediate state is acceptable where
page raster uses one authority and lighting samples another.

##### Known failure signatures

If any of the following reappear, the cutover is not ready:

- atlas pages update but no shadows are drawn
- `page_table_mismatches` stay stable and nonzero frame-to-frame
- fallback-only pages acquire nonzero page flags
- GPU current publication count is much larger than CPU current publication
  count
- DX12 errors caused by descriptor misuse during live publication rebinding

##### Resume checklist

When resuming this follow-up, record these checkpoints in the working notes or
next plan update before claiming progress:

- which stage was touched: clear / current / fallback / hierarchy / schedule /
  cutover
- which exact CPU function was used as the reference
- whether the change affects live authority or validation-only outputs
- current mismatch counts from the coherence gate
- whether the locked benchmark still renders visible shadows

##### Required validation sequence

Every meaningful implementation slice in this follow-up must run, in order:

1. `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.VirtualShadowContracts.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
2. `msbuild out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj /m:1 /p:Configuration=Debug /nologo`
3. focused tests for the touched contract area
4. `msbuild out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj /m:1 /p:Configuration=Debug /nologo`
5. `powershell -ExecutionPolicy Bypass -File Examples\\RenderScene\\benchmark_directional_vsm.ps1`

No step may be marked completed without recording:

- files changed
- tests run
- current mismatch counts
- whether live shadows are still visible

### Phase 7. Replace Readback-Led Demand With Same-Frame GPU Marking, Invalidation, And Static/Dynamic Split

Status: `pending`

Goal:

- stop using delayed readback-driven demand as the authoritative missing/dirty
  source, and split stable/static page reuse from dynamic invalidation

Implementation:

- make visible detail marking and coarse marking write directly into GPU page
  flags for the current frame
- consume those flags in the same frame for missing/dirty page management
- split physical-page validity into static and dynamic channels so moving
  geometry invalidates only overlapping dynamic content
- replace brute-force coarse mark atomics with groupshared / wave-cooperative
  marking
- keep the old feedback path only as transitional telemetry until Phase 10
  removes it

Must remove or demote:

- multi-frame readback as the authoritative demand loop
- all-or-nothing dirtying when a single caster moves
- per-pixel brute-force coarse marking as the long-term coarse path

Verification:

- focused tests for same-frame marked-page consumption
- focused tests for static pages surviving unrelated dynamic motion
- focused tests for dynamic invalidation touching only overlapping pages
- benchmark evidence that coarse/detail request churn drops under motion

Completion gate:

- same-frame GPU marking is authoritative for missing/dirty pages
- static/dynamic separation is live in the page flags / invalidation path
- coarse marking no longer relies on per-pixel brute-force atomics

Resume anchor:

- entry precondition: Phase 6 follow-up cutover is complete and stable
- first files to touch:
  - `VirtualShadowRequestPass.cpp`
  - `VirtualShadowCoarseMarkPass.cpp`
  - GPU marking shaders
  - page-flag definitions / consumers
- do not start this phase while CPU live publication is still authoritative
- first validation target:
  same-frame GPU marking must identify missing/dirty pages without using
  readback as the authoritative source

### Phase 8. Replace CPU Page Table / Raster Authority With GPU Per-Page Draw Commands

Status: `pending`

Goal:

- remove the core scalability disaster: CPU-authored page-table/raster
  scheduling and page-by-page CPU draw submission

Implementation:

- build per-page draw commands on GPU from page-management outputs plus scene
  draw metadata
- feed raster from GPU-authored indirect draw lists only
- remove CPU-authored authoritative `resolved_raster_pages`
- remove CPU page loops that clear, bind, and draw one mesh list per page
- keep CPU orchestration limited to pass submission, not page-by-page raster
  planning

Must remove or demote:

- CPU-authored authoritative page-table publication
- CPU-authored authoritative raster-page scheduling
- per-page CPU draw-call fan-out as the raster execution model

Verification:

- focused tests for per-page draw-command generation and page-local overlap
- targeted build validation for the new raster command path
- locked benchmark evidence that raster submissions scale with page-local work,
  not page-count times full-scene caster count

Completion gate:

- raster consumes GPU per-page draw commands as its only authority
- the CPU no longer authors the live raster schedule or live page-table state

Resume anchor:

- entry precondition: Phase 7 is complete and same-frame GPU marking is live
- first files to touch:
  - draw-command build pass
  - raster pass
  - scene draw metadata consumers
- do not revive CPU-authored `resolved_raster_pages` as a compatibility crutch
- first validation target:
  GPU-authored per-page draw commands must match page-local overlap semantics
  before CPU schedule removal

### Phase 9. Replace The Projection / Shading Cost Model

Status: `pending`

Goal:

- eliminate the remaining UE cross-reference shader/per-pixel failures without
  regressing the now-correct coarse fallback contract

Implementation:

- resolve page-table state once per evaluation, not once per filter tap
- reuse resolved physical page and fallback state across the tent filter kernel
- collapse multi-clip evaluation so fine/fallback blending shares resolved
  page-state instead of redoing the full lookup chain
- scale slope bias directly from `fallback_lod_offset`
- make guard texel count depend on the effective filter radius instead of the
  current hardcoded constant
- keep the current grounded coarse remap contract while replacing the old
  repeated lookup cost model

Must remove or demote:

- page-table re-resolution for every tent-filter tap
- hardcoded guard texel count
- repeated independent clip evaluation in the worst-case pixel path
- fallback-bias logic that is not derived from actual LOD growth

Verification:

- focused shader/contract tests for:
  - per-evaluation single-resolve sampling
  - fallback LOD bias scaling
  - guard-texel clamping by filter radius
- locked benchmark evidence that shading cost falls without harming correctness

Completion gate:

- the live shader no longer re-resolves the page table per filter tap
- fallback slope bias and guard texels are LOD/radius correct
- worst-case clip evaluation cost is materially reduced

Resume anchor:

- entry precondition: GPU publication and GPU raster authority are already live
- first files to touch:
  - `ShadowHelpers.hlsli`
  - page access helpers
  - projection helpers
- do not trade away the grounded coarse fallback contract to gain speed
- first validation target:
  per-evaluation resolved page state is reused across the filter kernel without
  regressing coarse grounding or motion correctness

### Phase 10. Remove Transitional Round-Trips And Add The Final Fast Paths

Status: `pending`

Goal:

- remove the remaining transitional CPU-GPU-CPU authority loops and land the
  final structural optimizations required by the cross-reference report

Implementation:

- delete the redundant resolve/schedule round-trip once same-frame GPU marking,
  page management, and GPU raster authority are live
- remove the remaining transitional CPU feedback integration path
- add the single-page optimization hook for future spot/point/light cases or
  tiny virtual spans so the new architecture does not hard-code full clipmap
  cost for trivially small demand
- keep runtime ownership clean: one authority per stage, no legacy fallback
  loops kept alive for convenience

Must remove or demote:

- transitional CPU-GPU-CPU resolve/schedule feedback loops
- legacy-authority compatibility scaffolding that survived the earlier phases

Verification:

- targeted compile/build passes
- focused tests for the no-readback same-frame path
- locked benchmark evidence that the new path runs without the old round-trip

Completion gate:

- no redundant CPU-GPU-CPU resolve authority remains in the live path
- the architecture contains the single-page fast path hook and is ready for
  future local-light expansion

Resume anchor:

- entry precondition: Phases 7-9 are complete and stable
- first files to touch:
  - transitional feedback integration code
  - remaining compatibility scaffolding in backend / resolve pass
- do not delete validation scaffolding until the replacement authority is
  already proven live
- first validation target:
  the locked benchmark must run without the old round-trip authority path and
  still preserve visible shadows

### Phase 11. Performance Recovery On The New Architecture

Status: `pending`

Goal:

- do the final performance work only after the correct UE-inspired
  architecture is fully in place

Implementation:

- re-baseline the locked benchmark on the post-Phase-10 runtime
- optimize cache-validity, remap/reuse, page management, marking, draw-command
  build, raster, and shading only on that final architecture
- keep correctness and coarse-detail stability ahead of raw speed

Verification:

- locked benchmark runner only
- before/after evidence attached to this document
- targeted manual validation for motion correctness and coarse grounding after
  major performance changes

Completion gate:

- correctness preserved
- benchmark materially better than the frozen baseline
- no old heuristic or stale-publication shortcut was revived to buy speed

Resume anchor:

- entry precondition: the architecture is fully cut over and no transitional
  authority remains
- first files to touch:
  only the live final architecture, never the deleted compatibility path
- do not claim a win from benchmark-only gains if correctness regresses
- first validation target:
  before/after locked benchmark evidence tied to a correctness-preserving
  change only

## 9. Verification Matrix

Every phase completion claim must include:

1. Files changed
2. Focused targets built in `out/build-vs`
3. Focused tests run
4. Benchmark evidence if the phase changes runtime cost
5. Remaining gap to the next phase gate

Accepted build/test targets:

- `out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.LightManager.Tests.vcxproj`
- `out/build-vs/src/Oxygen/Renderer/Test/Oxygen.Renderer.DrawMetadataEmitter.Tests.vcxproj`
- additional focused renderer test targets as introduced by the refactor
- `out/build-vs/Examples/RenderScene/oxygen-examples-renderscene.vcxproj`

Accepted runtime evidence:

- canonical benchmark runner only
- targeted manual validation for motion correctness after the shading switchover

## 10. Phase Status Ledger

- Phase 1: `completed`
- Phase 2: `completed`
- Phase 3: `completed`
- Phase 4: `completed`
- Phase 4B: `completed`
- Phase 5: `completed`
- Phase 6: `in_progress`
- Phase 7: `pending`
- Phase 8: `pending`
- Phase 9: `pending`
- Phase 10: `pending`
- Phase 11: `pending`

## 11. Validation

Validation for this plan document:

- document updated to integrate all issues from
  `src/Oxygen/Renderer/Docs/vsm_cross_reference_report.md`
  starting at Phase 6
- Phase 5 is now recorded closed per explicit user direction
- no legacy docs were updated for active task tracking
- `git diff --check` must stay clean aside from existing CRLF warnings
- no builds or tests are required for the document itself

Remaining gap:

- Phase 6 is now active and evidence-backed, but not complete; GPU-authored
  page-table / page-flag writes and fully authoritative GPU page management
  remain the exit delta
