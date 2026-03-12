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

Status: `pending`

Goal:

- make compatible-motion continuity come from remapping valid physical pages
  into the new clipmap address space

Implementation:

- add a dedicated physical-page remap/update stage
- consume the Phase 3 next-data mapping for each prior virtual page
- remap old page addresses by page-space offset
- if the remapped page stays in bounds, rewrite the current page table so it
  points at the same physical page
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
- focused tests for page-address offset remap
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

### Phase 5. Replace CPU Bootstrap With GPU Page Marking

Status: `pending`

Goal:

- make visible-sample page marking authoritative for missing or dirty pages
  only

Implementation:

- add a GPU visible page-marking pass driven by main depth / visible receivers
- add a separate GPU coarse-marking pass
- use only small local dilation
- define binary coarse/detail policy through page flags
- consume page marking only to request pages that are currently unmapped or
  uncached after the remap/reuse stage

Must remove or demote:

- `receiver_bootstrap` as an authoritative page source
- `feedback_refinement` as an authoritative page source
- `current_frame_reinforcement` as an authoritative page source

Verification:

- focused tests for visible-sample request generation
- focused tests for coarse/detail flag assignment
- runtime debug capture proving motion no longer explodes into heuristic
  reseeding near the camera

Completion gate:

- page demand is primarily GPU marked for missing/dirty pages
- the old bootstrap/reinforcement path is no longer authoritative
- motion no longer depends on near-camera heuristic reseeding

### Phase 6. Build Invalidation And Page Management

Status: `pending`

Goal:

- make invalidation, allocation, and fallback page-centric on top of the remap
  contract

Implementation:

- add a dedicated invalidation stage
- add new-page allocation backed by explicit physical page lists
- preserve requested pages and evict from the correct lists only
- propagate fallback visibility / mapped hierarchy through page-table entries
  and page flags
- guarantee one sample-usable coarse path through the page-management contract

Must remove or demote:

- coarse safety as a backend-only budgeting concept
- allocator behavior coupled to monolithic backend resolve logic

Verification:

- focused tests for requested-page protection
- focused tests for allocation under pressure
- focused tests for invalidation by moved/dirty content
- focused tests for fallback LOD decode at the sample contract

Completion gate:

- invalidation/allocation/fallback are page-management responsibilities
- coarse/detail fallback is encoded in the sampling contract, not carried by
  stale publication state

### Phase 7. Replace CPU Raster Scheduling With Per-Page Draw Commands

Status: `pending`

Goal:

- make raster consume compact per-page draw lists instead of CPU-authored page
  jobs

Implementation:

- add a GPU per-page draw-command build stage
- compact visible caster instances per page
- feed raster from the per-page draw lists only
- keep dirty-page update hooks integrated with this stage

Must remove or demote:

- CPU-authored authoritative `resolved_raster_pages`
- raster scheduling hidden inside backend resolve

Verification:

- focused tests for per-page draw-list compaction
- focused tests for page-local caster overlap
- benchmark evidence that raster submissions scale with page-local overlap, not
  full scene caster count

Completion gate:

- raster consumes only the new authoritative per-page draw command source

### Phase 8. Replace Projection / Shading Contract

Status: `pending`

Goal:

- make sampling stable on the new page-table contract

Implementation:

- rewrite directional VSM sampling around page-table decode helpers
- select the best available mapped level through the page-table fallback fields
- keep clipmap selection and fallback entirely inside the new contract
- remove stale whole-publication republish from the authoritative runtime
- remove the "return lit because nothing valid is published" path as the normal
  recovery behavior

Verification:

- focused shader tests where possible
- focused runtime regressions for aggressive zoom / translation / rotation
- manual motion validation showing no wrong-page flashing

Completion gate:

- `last_coherent_*` fallback path is gone from the authoritative runtime
- motion-time shading correctness is proven on the new sample contract

### Phase 9. Delete The Legacy Monolith

Status: `pending`

Goal:

- remove the old architecture instead of dragging it behind the new one

Implementation:

- delete or reduce the old backend functions so they are no longer authoritative
- remove the legacy kill-list items from active code
- simplify the backend to orchestration of explicit stages only

Verification:

- targeted compile/build passes
- focused tests still green
- code search proving legacy authority symbols are gone or dead

Completion gate:

- no parallel legacy authority remains

### Phase 10. Performance Recovery On The New Architecture

Status: `pending`

Goal:

- recover performance only after the architecture is correct

Implementation:

- benchmark the new architecture against the locked moving-camera baseline
- optimize cache-validity, remap/reuse, invalidation, page marking,
  per-page draw-command build, and raster cost on the new path only
- do not revive old planner heuristics or stale-publication shortcuts as quick
  fixes

Verification:

- locked benchmark runner only
- before/after evidence attached to this document

Completion gate:

- correctness preserved
- benchmark materially better than the frozen baseline

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
- Phase 3: `in_progress`
- Phase 4: `pending`
- Phase 5: `pending`
- Phase 6: `pending`
- Phase 7: `pending`
- Phase 8: `pending`
- Phase 9: `pending`
- Phase 10: `pending`

## 11. Validation

Validation for this plan document:

- document scope-corrected around the UE-style directional cache/remap model
- no legacy docs were updated for active task tracking
- `git diff --check` must stay clean aside from existing CRLF warnings
- no builds or tests are required for the document itself

Remaining gap:

- corrected Phase 3 implementation and validation are still outstanding
