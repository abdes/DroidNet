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

4. Coarse/detail policy must be simple.
   - binary and explicit, not a boiling stack of overlapping quality regimes

5. Passes own one concern.
   - clipmap setup
   - page marking
   - invalidation
   - page reuse/allocation
   - page-flag propagation
   - per-page draw-command build
   - raster
   - projection

6. Verification gates are mandatory.
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
2. `DirectionalVirtualPageMarkPass`
3. `DirectionalVirtualCoarseMarkPass`
4. `DirectionalVirtualInvalidationPass`
5. `DirectionalVirtualPageReusePass`
6. `DirectionalVirtualPageAllocatePass`
7. `DirectionalVirtualPageFlagPropagatePass`
8. `DirectionalVirtualBuildPerPageDrawCommandsPass`
9. `DirectionalVirtualPageRasterPass`
10. `DirectionalVirtualProjectionPass`
11. optional `DirectionalVirtualPageHzbPass`

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

Status: `pending`

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

### Phase 3. Replace CPU Bootstrap With GPU Page Marking

Status: `pending`

Goal:

- make visible-sample page marking authoritative

Implementation:

- add a GPU visible page-marking pass driven by main depth / visible receivers
- add a separate GPU coarse-marking pass
- use small local dilation only
- define the binary coarse/detail content policy through page flags

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

- page demand is primarily GPU marked
- the old bootstrap/reinforcement path is no longer authoritative

### Phase 4. Build Dedicated Invalidation And Page Reuse

Status: `pending`

Goal:

- make motion continuity come from page-space reuse and invalidation, not stale
  publication fallback

Implementation:

- add a dedicated invalidation stage
- add a dedicated physical-page reuse/update stage
- remap previous pages by page-space offset
- preserve valid pages in place
- record dirty/invalidated content through page flags / physical metadata

Must remove or demote:

- snapshot-level `last_coherent_*` as the primary continuity tool

Verification:

- focused tests for clipmap panning reuse
- focused tests for invalidation by moved/dirty content
- focused tests that previous valid pages survive compatible motion without
  stale whole-publication reuse

Completion gate:

- page reuse/invalidation is authoritative
- continuity under compatible motion no longer depends on stale snapshot
  publish logic

### Phase 5. Move Allocation And Fallback Into Page Management

Status: `pending`

Goal:

- make allocation and fallback page-centric

Implementation:

- add a dedicated new-page allocation stage
- add page-list management for available/LRU/requested pages
- propagate fallback visibility / mapped hierarchy through page-table or page
  flags
- guarantee one sample-usable coarse path through the new contract

Must remove or demote:

- coarse safety as a backend-only planning policy
- allocator behavior coupled to the old monolithic resolve stage

Verification:

- focused tests for requested-page protection
- focused tests for allocation under pressure
- focused tests for fallback LOD decode at the sample contract

Completion gate:

- fallback is represented in the page-management contract
- coarse fallback is no longer only a backend budgeting concept

### Phase 6. Replace CPU Raster Scheduling With Per-Page Draw Commands

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

### Phase 7. Replace Projection / Shading Contract

Status: `pending`

Goal:

- make sampling stable on the new page-table contract

Implementation:

- rewrite directional VSM sampling around page-table decode helpers
- sample the best available mapped LOD through the new fallback fields
- keep clipmap selection and fallback entirely inside the new contract

Must remove or demote:

- stale full-publication republish as a shading continuity tool
- "return lit because nothing valid is published" as the expected recovery path

Verification:

- focused shader tests where possible
- focused runtime regressions for aggressive zoom / translation / rotation
- manual motion validation showing no wrong-page flashing

Completion gate:

- `last_coherent_*` fallback path is gone from the authoritative runtime
- motion-time shading correctness is proven on the new sample contract

### Phase 8. Delete The Legacy Monolith

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

### Phase 9. Performance Recovery On The New Architecture

Status: `pending`

Goal:

- recover performance only after the architecture is correct

Implementation:

- benchmark the new architecture against the locked moving-camera baseline
- optimize page marking, invalidation, per-page draw-command build, and raster
  cost on the new path only
- do not revive old planner heuristics as quick fixes

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

- Phase 1: `pending`
- Phase 2: `pending`
- Phase 3: `pending`
- Phase 4: `pending`
- Phase 5: `pending`
- Phase 6: `pending`
- Phase 7: `pending`
- Phase 8: `pending`
- Phase 9: `pending`

## 11. Validation

Validation for this plan document:

- document created
- no legacy docs were updated for active task tracking
- `git diff --check` must stay clean aside from existing CRLF warnings
- no builds or tests are required for the document itself

Remaining gap:

- implementation has not started against this plan
