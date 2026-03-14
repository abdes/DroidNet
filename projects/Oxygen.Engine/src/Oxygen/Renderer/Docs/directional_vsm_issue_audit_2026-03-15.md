# Directional VSM Issue Audit

Date: March 15, 2026
Status: `reviewed`
Scope: directional virtual shadow maps only
Cross-reference: [directional_vsm_architecture_review.md](directional_vsm_architecture_review.md)

## 1. Purpose

This document checks the current Oxygen directional VSM codebase against each
issue called out in the March 12, 2026 architecture review.

This is a code-inspection audit only.

Validation performed:

- inspected current backend, shader, request, raster, and test code
- compared current implementation against the original review issues
- no build run
- no test run

Remaining validation gap:

- runtime correctness under motion was not revalidated in this audit

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
- legacy diagnostics remain, but those mechanisms are explicitly no longer live
  page-demand sources
- the same function still owns large CPU-side policy decisions for:
  `feedback_decision`, `same_frame_detail_pages`, `coarse_backbone_pages`,
  `force_full_same_frame_detail_region`, `bootstrap_prefers_finest_detail_pages`,
  `allow_fallback_aliases`, and coarse-safety budgeting

Why this is better:

- the worst legacy synthetic-demand mechanisms were removed from live authority

What is still incomplete:

- the backend still interprets previous-frame feedback and CPU-authors the
  selected page set for the next resolve
- visible-sample demand is still mediated through CPU policy instead of being
  the authoritative same-frame GPU source

Task:

- remove previous-frame feedback acceptance and same-frame detail fallback
  policy from `BuildDirectionalVirtualViewState()` and move page demand
  authority fully onto GPU page marking/page management

### 3.4 Issue 4: Oxygen turned shadow quality into a boiling multi-band problem

Verdict: `partially fixed`

Evidence:

- the request shader now requests only the selected clip and an optional finer
  prefetch clip, which is much simpler than the previous layered CPU demand
  stack
- the backend no longer drives live `receiver_bootstrap`,
  `feedback_refinement`, or `current_frame_reinforcement`
- the shading path still blends between selected, finer, and coarser results
  in `ComputeVirtualDirectionalShadowVisibility()`
- the backend still combines coarse backbone selection, same-frame detail
  publication, accepted feedback seeding, and coarse feedback channels

Why this is better:

- the number of live quality bands is lower than in the reviewed March 12
  design

What is still incomplete:

- there are still multiple overlapping quality regimes in both shading and
  backend selection
- large flat receivers can still be sensitive to selected/finer/coarser blend
  boundaries plus backend coarse/detail coverage transitions

Task:

- reduce directional sampling to a simpler page-table-driven fallback path with
  less cross-band blending and less backend-authored clip-quality shaping

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

Verdict: `still failing`

Evidence:

- `ResolvePendingPageResidency()` still owns page-table reset, resident carry,
  requested-page allocation, eviction, fallback alias population, hierarchy
  propagation, and snapshot rebuilds
- `BuildDirectionalVirtualViewState()` still owns clipmap setup consumption,
  feedback acceptance, selected-page generation, dirty-page derivation,
  coarse/detail policy, and pending resolve construction
- while some helpers were extracted, the main control flow remains centered on
  two very large backend functions

Why this is still a problem:

- ownership boundaries are still blurred
- replacing one stage still means touching selection, feedback, fallback, and
  resolve logic together

Task:

- split backend responsibilities into clipmap setup, marking, invalidation,
  page management, and raster schedule ownership with clear pass boundaries

### 3.8 Issue 8: Oxygen never split invalidation / page management / draw-command build

Verdict: `partially fixed, but still incomplete`

Evidence:

- there are now dedicated request and coarse-mark passes, and live GPU raster
  inputs are consumed by `VirtualShadowPageRasterPass`
- page-management bindings exist as a separate exported concept
- invalidation is still derived inside `BuildDirectionalVirtualViewState()` by
  CPU construction of dirty resident key sets
- page reuse, eviction, allocation, and page-table writes still happen in
  `ResolvePendingPageResidency()` on CPU
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

Verdict: `partially fixed`

Evidence:

- `PopulateDirectionalFallbackPageTableEntries()` now writes per-entry fallback
  aliases into the page table using fallback LOD offsets
- hierarchical page flags are propagated and used as a visibility gate for
  fallback alias population
- the backend still computes `coarse_safety_clip_index`,
  `coarse_safety_budget_pages`, and `coarse_safety_capacity_fit`
- `allow_fallback_aliases` is still a backend policy decision, not an always-on
  page-management guarantee

Why this is better:

- fallback is now represented in the page table instead of existing only as a
  budget counter or publication intention

What is still incomplete:

- coarse fallback is still gated by backend policy and accepted detail lineage
- the guarantee is not yet produced by a dedicated coarse-mark + page-management
  pipeline that is authoritative on its own

Task:

- make coarse coverage and fallback validity unconditional outputs of the
  page-management pipeline rather than a CPU alias-population recovery policy

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
9. Coarse fallback bolted on too late: `partially fixed`
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
- CPU resolve/allocation/eviction authority in `ResolvePendingPageResidency()`
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

### 5.2 Stage 2: Finish backend decomposition first

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

Exit condition:

- `BuildDirectionalVirtualViewState()` no longer owns feedback policy,
  invalidation, page demand synthesis, and resolve construction together
- `ResolvePendingPageResidency()` no longer owns reuse, allocation, fallback,
  and page-table mutation as one CPU stage

### 5.3 Stage 3: Replace readback-led demand authority with same-frame GPU marking

The request and coarse-mark passes must stop being previous-frame feedback
feeds into CPU policy and become same-frame authoritative marking inputs for
page management.

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

### 5.4 Stage 4: Move invalidation to a dedicated authoritative stage

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

### 5.5 Stage 5: Make page reuse/update/allocation GPU-authoritative

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

### 5.6 Stage 6: Finish coarse fallback as a guaranteed page-management product

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

- this audit was code inspection only
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

- code inspection only
- current file evidence taken from backend, shader, pass, and test sources
- no builds run
- no tests run

Remaining gap:

- motion-time correctness and boiling/performance behavior still need runtime
  validation against camera/light stress scenes
