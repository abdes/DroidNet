SoA Renderer Migration Work Log
================================

Purpose
-------
This document is for me, the GitHub Copilot AI assistant, so I can keep track of what I need to do and what I have done already for this long refactoring.
I must really read it and pay attention to its content. This is the authoritative, persistent log for the migration from the legacy AoS `RenderItem` / `RenderItemsList` path to a fully SoA + DrawMetadata based renderer. Updated strictly at every stable step (after a task completes or major decision). Serves as a memory anchor to prevent context loss during long refactor.
When I get stuck or run into difficulty I must pause and ask for help.

Guiding Principles
------------------
1. No dual-path or dead code at the end: remove AoS completely.
2. Keep passes consuming immutable SoA snapshots (`PreparedSceneFrame`).
3. Deterministic ordering and partitioning (debug hashable).
4. Zero per-draw pointer chasing at submission.
5. Leverage existing bindless infrastructure; minimize new abstractions.
6. Fail fast (assert) on invariant violations in debug; silent fast path in release.
7. Reuse allocations (capacity grows monotonically, rarely shrinks) for perf stability.

Glossary (Current)
------------------
AoS: Array-of-Structs legacy `RenderItemsList`.
SoA: Structure-of-Arrays: collected `RenderItemData` → finalized arrays + `DrawMetadata`.
PreparedSceneFrame (PSF): Immutable per-frame snapshot (spans into renderer-owned vectors).
PartitionMap: pass_id -> [begin,end) over sorted `draw_metadata`.
DrawMetadata: GPU-facing per-draw packed struct (indexed / non-indexed info + handles).

Baseline (Captured Before Refactor – Run: oxyrun async -- -f 1 -v 2)
--------------------------------------------------------------------
Collected Items: 3
Converted RenderItems (bridge): 3
Existing uploads: vertex/index buffers, DrawResourceIndices, WorldTransforms, MaterialConstants, SceneConstants.
Passes executed: DepthPrePass, ShaderPass.
Bridge still active (`Renderer.cpp` log: "converted 3 collected items to 3 RenderItems").

High-Level Migration Phases
---------------------------
P1: Introduce PSF type + finalization skeleton (no removal yet) -> quick compile & run.
P2: Implement finalization population of DrawMetadata directly (parallel path) + optional toggle.
P3: Convert passes to SoA consumption; remove AoS bridge usage.
P4: Delete AoS types & tests; tighten interfaces; doc updates.
P5: Sorting, partition map, perf & determinism validation.
P6: Cleanup (macros, exports, docs, changelog) & final audit.

Initial Decisions (Locked)
-------------------------
* PSF will NOT own memory; renderer owns vectors; PSF holds spans.
* DrawMetadata gains fields: material_slot (uint32), transform_index (uint32), flags (uint32) – pack to preserve 16-byte multiple.
* Sorting key (initial): (pass_group, material_slot, geometry_id). Geometry id derivable from (vertex_buffer_index << 16) ^ index_buffer_index (cheap hash) unless an explicit ID emerges.
* PartitionMap built after final sort; pass masks computed earlier and stored parallel to filtered items; final ordering uses pass group first to make partition derivation O(n) single pass.

Open Questions (Track & Resolve Early)
--------------------------------------
Q1: Do we expose normal matrices or rely on inverse-transpose in shader? (Default: precompute normal matrices CPU side.)
Q2: Need separate transparency ordering early? (Defer; initial pass mask only.)
Q3: Transform dedupe epsilon threshold? (Start with exact bitwise equality.)

Risk Register
-------------
R1: Shader / C++ DrawMetadata divergence → Mitigation: central offset static asserts & mirrored comment header.
R2: Sorting instability across platforms → Mitigation: stable_sort and deterministic key; debug hash of sequence.
R3: Memory churn (vector reallocation) → Mitigation: track high-water mark; reserve before population.
R4: Latent dependencies on removed headers in external modules → Mitigation: phased grep + CI build across presets before deletion commit.

Metrics Plan
------------
Capture at each phase: collected_count, draw_count, build_time(us) (collection, finalization separate), peak_allocated_bytes(optional), sort_time(us).
Simple struct recorded and optionally logged at verbose level 2.

// (Removed obsolete P1 immediate next-step section – Task 1 completed.)

Update Procedure
----------------
1. Complete a task (or atomic sub-step) → update this file: status, decisions, metrics.
2. If a plan changes, append under a "Plan Adjustments" section with timestamp.
3. Never remove entries from History Log (Chronological) except for correcting typos.

Plan Adjustments
----------------
2025-09-04T01:40Z  Refined short-horizon sequence: prioritize integration of PreparedSceneFrame into RenderContext (Tasks 7 & 8) before implementing DrawMetadata SoA population to enable immediate validation via DepthPrePass dual-path. Defer partition map (Task 11) until at least one pass consumes SoA successfully. Dual timing (collection vs finalization) moved up within Task 6 to lock baseline before further structural edits.

History Log (Chronological)
---------------------------
2025-09-04T00:00Z  Initialized log (baseline run shows 3 items, AoS bridge active).
2025-09-04T00:10Z  Task 1 scaffold added: PreparedSceneFrame.{h,cpp} + CMake entries. No functional usage yet.
2025-09-04T00:25Z  Task 2 extended DrawMetadata (C++ & HLSL) with material_slot, transform_index; renderer population updated; shaders updated; helper comments synced.
2025-09-04T00:50Z  Task 6 partial: Added SoA finalization method `FinalizeScenePrepSoA` populating world & normal matrices; `PreparedSceneFrame` now exposes spans. No draw metadata bytes yet; legacy AoS still source for DrawMetadata buffer. Baseline run unchanged.
2025-09-04T01:20Z  Task 6 update: Finalization now respects `filtered_indices` (fallback synthesizes sequence). Added high-water reservation tracking, timing metrics (microseconds), stats struct, placeholder zeroed DrawMetadata bytes sized to finalized count. Logging now includes collected/filtered/finalized counts.
2025-09-04T01:40Z  Planning refinement: Decided to integrate PSF into RenderContext and adapt DepthPrePass for matrix consumption before populating real DrawMetadata bytes. Rationale: early vertical slice validation reduces risk before removing AoS bridge. Added upcoming execution order & acceptance criteria sections.
2025-09-04T01:55Z  Tasks 7/8 partial: Added `prepared_frame` pointer to `RenderContext` and wired in `Renderer::PreExecute`. Added collection timing instrumentation (Task 6.f) capturing `collection_us`. DepthPrePass now logs presence and sizes of PSF matrix spans (debug only). No behavioral change yet.
2025-09-04T02:15Z  Task 6 incremental: Implemented minimal DrawMetadata SoA population (one record per finalized item, transform_index only) inside `FinalizeScenePrepSoA`; removed placeholder zeroed byte span; exposed real SoA bytes via `prepared_frame_.draw_metadata_bytes`; added size-only parity check (legacy vs SoA counts). Chrono durations now stored in `FinalizeStats` (collection_time / finalize_time) though work log still lists _us; will rename acceptance criteria fields once full parity established. (Reordered here; earlier misplacement below corrected.)
2025-09-04T02:30Z  Task 6 advancement: Promoted full DrawMetadata SoA population (copy from legacy buffer + byte-for-byte memcmp parity) into `FinalizeScenePrepSoA`; removed temporary duplication/parity block from `PreExecute`. PreparedSceneFrame now fully exposes synchronized draw metadata bytes during pass execution. Next step: refactor first pass to consume `prepared_frame_.draw_metadata_bytes` directly (legacy AoS fallback for transitional frames) before deleting bridge path.
2025-09-04T02:45Z  DepthPrePass updated: Added conditional SoA draw path consuming `PreparedSceneFrame.draw_metadata_bytes` (iterates DrawMetadata records, binds draw index constant). Falls back to legacy `IssueDrawCalls` if SoA unavailable or counts empty. Current limitation: DrawMetadata records lack populated index/vertex counts (parity phase); draws may be skipped until ScenePrep populates counts. Establishes integration pattern for subsequent passes. Next: enrich DrawMetadata population from collected submeshes to enable real depth draws via SoA.
2025-09-04T03:00Z  Planning note: Natural next step is to eliminate legacy copy for DrawMetadata by generating full `engine::DrawMetadata` entries directly from `ScenePrepState` (expanding each collected item into per-submesh MeshViews). This requires: (1) Submesh/MeshView expansion loop inside `FinalizeScenePrepSoA`; (2) computing vertex/index buffer bindless indices & first_index/base_vertex; (3) setting `is_indexed`, `instance_count=1`, `transform_index` and placeholder `material_index/material_slot`; (4) preserving deterministic order for future sorting step. After direct generation is stable & parity-logged, migrate DepthPrePass to SoA-only path (remove legacy IssueDrawCalls fallback) and mark Task 6 Done. Follow-up: unify the two DrawMetadata struct variants (ScenePrep::DrawMetadata vs engine::DrawMetadata) or rename the ScenePrep one to avoid confusion.
2025-09-04T03:20Z  Task 6 major: Replaced legacy parity copy with direct DrawMetadata generation inside `FinalizeScenePrepSoA`. For each filtered collected item, expand (LOD -> SubMesh -> MeshViews) producing one `engine::DrawMetadata` per MeshView. Populated fields: `first_index` & `base_vertex` (if indexed), `is_indexed`, `instance_count=1`, `transform_index` (slot = filtered order), remaining geometry/material/bindless fields set to 0 placeholders pending integration (Tasks 13/14). Removed byte-for-byte memcmp; retained count-only divergence log (expected). DepthPrePass simplified to SoA-aware metadata presence log + single legacy IssueDrawCalls (transitional). Acceptance for completing Task 6 now requires: (a) material & geometry bindless indices populated (non-zero) OR explicit TODO gating; (b) DepthPrePass issues draws without legacy RenderItemsList dependency; (c) visual parity for sample scene; (d) removal of legacy DrawMetadata copy path code. Next: integrate geometry bindless index resolution & emit per-record draws or indirect args.
2025-09-04T03:35Z  Task 6 progress: Populated real vertex/index buffer bindless indices in direct DrawMetadata generation by invoking `EnsureMeshResources` per LOD mesh (cached). Added debug logging of first 3 DrawMetadata entries (verbosity 3) to verify indices and slice fields. Kept counts absent (TODO: extend DrawMetadata or transition to indirect args). Next steps: add per-record draw emission path (DepthPrePass) using is_indexed + first_index/base_vertex and retrieving counts via MeshView (requires either extending GPU struct or issuing immediate DrawIndexed with view.IndexCount()). After stable, remove legacy IssueDrawCalls dependency from DepthPrePass. Follow-up: add material index population and pass masks.
2025-09-04T04:05Z  Index Count Integration: Repurposed DrawMetadata.material_slot -> index_count (C++ & DepthPrePass consumption). Renderer now fills dm.index_count from MeshView IndexBuffer(). DepthPrePass emits per-record DrawIndexed calls using md.index_count (SoA-only path); legacy fallback remains only if no records emitted. Added DrawIndexedInstanced alias for API parity. Future Task: introduce indirect args buffer (ExecuteIndirect) once SoA validated & legacy path removed. Follow-ups: reintroduce material_slot via new field or separate material indirection table; add pass mask bits into flags; implement non-indexed vertex_count path.
2025-09-04T04:25Z  DepthPrePass SoA Finalization: Removed legacy IssueDrawCalls fallback; replaced with DCHECK if no SoA records emitted. Confirms migration milestone (DepthPrePass now hard-requires PreparedSceneFrame.draw_metadata_bytes). Next: begin Task 11 (material indirection) & Task 12 (non-indexed support) before adding pass mask flags.
2025-09-04T04:40Z  Material Indirection (SoA Path): SoA finalization now builds `material_constants_cpu_soa_` alongside `draw_metadata_cpu_soa_`, assigning per-view `material_index` (fallback default material when absent). Uploaded via existing bindless structured buffer (`material_constants_`) and refreshed scene constants slot through `UpdateBindlessMaterialConstantsSlotIfChanged()`. Logging extended to include material_index in sampled DrawMetadata entries. Next: evaluate deduplication & introduce pass mask flags; implement non-indexed draw path.
2025-09-04T04:55Z  Material Indirection Fix: Corrected undefined identifier in `FinalizeScenePrepSoA` (`filtered_item`/`collected_item` -> `item`) for per-view material constants generation. Validated successful compile & single-frame run (`oxyrun async -- -f 1 -v 2` exit 0). Observed MaterialConstants structured buffer upload (heap index assigned) and DepthPrePass executing exclusively via SoA metadata. Logging now shows DrawMetadata sample lines with populated `material_index`.
2025-09-04T05:05Z  Document Refresh: Pruned obsolete immediate-next sections now satisfied (PreparedSceneFrame integration, basic PSF wiring, initial DepthPrePass dual-path). Updated task table statuses (see below). Marked Tasks 7,8,15,18 Done; Task 10 In Progress (only DepthPrePass migrated); Task 17 In Progress (buffer plumbing extends to material constants & draw metadata but not yet indirect args); Task 31 In Progress (SceneConstants gained bindless slots, further adjustments pending for pass masks). Established new focused next steps list.
2025-09-04T05:25Z  Material Dedup (N3 partial): Added pointer-based map (MaterialAsset* -> material_index) in `FinalizeScenePrepSoA` to avoid duplicate constant pushes. Single fallback default material maintained. Logged dedupe stats (duplicates avoided) at finalize verbose line; initial sample scene produced no duplicates (unique==total==3). This is an interim solution pending Task 13 registry integration which will provide stable indices & hash-based reuse across frames. Exit criteria impact: N3 considered structurally implemented; future improvement will shift to hash+content equality instead of pointer identity.
2025-09-04T05:45Z  Pass Mask + Validation Asserts: Added `constexpr uint32_t kPassFlagOpaque = 1u << 0;` and populated `dm.flags = kPassFlagOpaque` for every generated DrawMetadata record inside `FinalizeScenePrepSoA`. Introduced pre-push debug asserts validating: (a) `transform_index < world_matrices_cpu_soa_.size()`, (b) `material_index < material_constants_cpu_soa_.size()`, (c) `dm.flags != 0`. Added post-finalization validation loop re-checking all records (defensive until broader test coverage). DepthPrePass unaffected functionally (already SoA-only) but now has future-ready pass mask bit available for upcoming partition map (Task 11). Metrics (single-frame verbose run): collected=3 filtered=3 finalized=3 finalize_time_us≈97,000 (debug build; includes added validation). All 3 records carried opaque flag; 0 assertion failures; material dedupe avoided duplicates=0 (no repeats in sample). This completes N2 (initial pass mask population portion) & N4 (validation asserts) from Focused Next Steps; remaining for N2 is partition map scaffolding. Task 6 remaining scope narrowed to: non-indexed draws, second pass migration, and partition scaffolding.
2025-09-04T06:05Z  Non-Indexed Draw Support (N1) (initial, superseded): Temporary implementation reused `index_count` to carry a vertex count when `is_indexed==0` to avoid an immediate struct expansion ahead of indirect args work. DepthPrePass branched on `is_indexed` to issue `Draw` with the repurposed field. This approach is now replaced by an explicit `vertex_count` field (see 2025-09-04T07:55Z) and retained here only for historical traceability.
2025-09-04T07:55Z  DrawMetadata Explicit Vertex Count: Added dedicated `vertex_count` field (C++ & HLSL), expanding struct from 12 → 14 uint32 (48 → 56 bytes). Renderer now sets either `index_count` (indexed draws) or `vertex_count` (non-indexed draws) with the other zero. DepthPrePass & ShaderPass compute draw count via `is_indexed ? index_count : vertex_count`. Logs updated; prior DCHECK enforcing non-zero `index_count` for all draws removed in favor of validating the active count only. This resolves semantic overloading and aligns with user requirement for distinct fields.
2025-09-04T06:35Z  Second Pass SoA Migration (ShaderPass): Added `ShaderPass::IssueDrawCalls` override iterating `PreparedSceneFrame.draw_metadata_bytes` (mirrors DepthPrePass pattern) issuing indexed / non-indexed draws via unified DrawMetadata. Removed legacy AoS iteration for ShaderPass (base `RenderPass::IssueDrawCalls` no longer used here). Establishes breadth criterion for Task 6 exit (two passes consuming SoA). Task 10 breadth now includes DepthPrePass + ShaderPass migrated; remaining derived passes pending. Partition map scaffolding still outstanding (Task 11) before marking Task 6 complete. Next: implement partition map ranges (opaque flag -> single range) and update status snapshot.
2025-09-04T07:05Z  ShaderPass Mesh Pipeline Attempt: Switched pipeline to non-existent `Mesh.hlsl`, causing runtime failure ("Shader not found: VS@Mesh.hlsl") and preventing IssueDrawCalls execution (no color pass draws). Root cause: file absent; existing `FullScreenTriangle.hlsl` already implemented generic mesh vertex fetch via DrawMetadata. Resolution path: add `Mesh.hlsl` as a temporary duplicate of `FullScreenTriangle.hlsl` to satisfy new pipeline identifiers while preserving future intent to rename/cleanup. Also normalized verbose record count logging format specifiers (replaced `%zu` with `{}`) across passes so counts render correctly in logs. Next: re-run single-frame validation to confirm ShaderPass now emits draws and logs record count; then evaluate whether to collapse back to original shader filename vs keep alias until partition/sorting work completes.
2025-09-04T08:10Z  DrawMetadata Stride Regression Fix: Discovered only first mesh rendered after vertex_count addition. Root cause: CPU struct forced to 64 bytes (alignas(16) padding) while HLSL consumed tightly packed 52 bytes (13 uint32 fields). Removed `alignas(16)` and restored tight packing; updated static_assert to 52. Verified 3 draws rendered (DepthPrePass + ShaderPass) in single-frame run (`oxyrun async -- -f 1 -v 3`). Added log confirmation (`RenderPass SoA metadata available: records=3`). Historical record (07:55Z) retains earlier transient 56-byte note; current authoritative size is 52 bytes.
2025-09-04T08:25Z  RenderPass Draw Issuance Refactor: Eliminated duplicated per-pass SoA draw loops (DepthPrePass & ShaderPass). Added `RenderPass::IssueDrawCallsFromMetadata` helper with explicit indexed vs non-indexed branches (no combined count variable). Updated both passes to invoke helper; removed previously introduced lazy variable `draw_vertex_or_index_count`. Improves maintainability ahead of partition map & potential indirect draw args integration. Sets groundwork for Task 9 (interface refactor) and reduces risk of divergence in future passes.
2025-09-04T08:40Z  Partition Map Scaffolding (Task 11 partial): Added `PreparedSceneFrame::PartitionRange` and backing `partitions_cpu_soa_` in `Renderer`. Finalization now emits a single opaque range `[0, draw_count)` when draws exist (bit 0 mask). Published via spans on `PreparedSceneFrame` and logged at verbosity 3 (`Partition[i]: mask=0x1 range=[0,n]`). This establishes structure for future multi-pass segmentation (shadow/transparency). No sorting or multi-range logic yet; Task 11 moved to In Progress. Next: introduce sorting key draft & deterministic hash prior to multi-range expansion.
2025-09-04T09:05Z  Sorting + Multi-Range Partition Implementation (Tasks 16 & 11 advancement): Implemented stable sort of `draw_metadata_cpu_soa_` using `DrawSortingKey` (order: pass_mask, material_index, vertex_srv, index_srv, tiebreak by original index). Added pre-sort and post-sort 64-bit FNV-1a hashes (`DrawOrderSort: pre=0x... post=0x...`). Replaced single-range scaffold with O(n) contiguous partition construction grouping identical `pass_mask` segments after sorting. Published post-sort byte span and new partitions via `PreparedSceneFrame`. Logging: existing finalize summary retained; new verbosity 2 line summarizing hashes, draw count, partition count, key bytes. Updated task statuses: Task 16 -> In Progress, Task 11 now includes multi-range logic (still only opaque in sample). Acceptance: deterministic ordering verified by equal pre/post hash when no key fields change; hash will shift once additional pass bits/material permutations introduced. Follow-up: add timing metrics for sort, integrate transparency/shadow pass bits to exercise multi-range (>1) validation, add unit tests for partition coverage & non-overlap, and prepare indirect draw args generation once ordering stable.
2025-09-04T09:15Z  Sorting Timing Metric Added: Instrumented `FinalizeScenePrepSoA` with `t_sort_begin/end` around stable sort + permutation + partition build. Added `last_sort_time_` (microseconds) member and included `sort_time_us=` field in `DrawOrderSort` verbosity 2 log line. This isolates ordering overhead from overall finalize time for future optimization (e.g., key packing, radix sort, indirect args generation). No behavior change to ordering or partition counts (still single opaque partition in sample). Next: capture baseline sort_time_us across multiple frame sizes and integrate into forthcoming performance baseline task (Task 26).
2025-09-04T09:30Z  AoS Final Removal Phase: Deleted `RenderItem.*` and `RenderItemsList.*` sources/headers plus all engine includes; passes, renderer, and context now exclusively depend on SoA (`PreparedSceneFrame`). Removed legacy mirror assignment of draw metadata slot into `bindless_indices_slot` in `Renderer::UpdateDrawMetaDataSlotIfChanged`; shaders already consume `bindless_draw_meta_data_slot`. Left `bindless_indices_slot` in `SceneConstants` for one grace cycle; scheduled removal deadline (2025-09-15) noted in TODO. Pending follow-up: delete AoS-specific tests & README sections (or rewrite examples to SoA), then prune `bindless_indices_slot` field and associated HLSL once confirmed unused. Task status updates: Tasks 3,4 marked Done; Task 6 closer to completion (partition & indirect args still outstanding); Task 31 partial (legacy slot deprecation in progress).
2025-09-04T09:40Z  Bindless Slot Harmonization: Fully removed `bindless_indices_slot` from C++ (`SceneConstants`) and HLSL (`DepthPrePass.hlsl`, `FullScreenTriangle.hlsl`). Renamed shader field `bindless_draw_meta_data_slot` -> `bindless_draw_metadata_slot` for consistency with C++ naming. Updated `Renderer::UpdateDrawMetaDataSlotIfChanged` comments; eliminated obsolete TODO. Pending doc sync: search & replace references in docs (`bindless_conventions.md`, `upload.md`, `multi_draw_implementation.md`, `implementation_plan.md`). Next: update documentation and adjust task statuses (Task 31 progresses; add doc cleanup subtasks).
2025-09-04T09:55Z  Work Log Sync & Status Refresh: Consolidated AoS removal & slot harmonization outcomes. Updated task status table (AoS tasks 3–5 & RenderItem recompute logic 29 now Done; documentation cleanup (22) set IP due to stale references), clarified remaining scope for Task 6 (partition multi-range extension, indirect args prototype, third-pass decision). Added revised Focused Next Steps emphasizing: (1) doc/reference scrub, (2) partition multi-range + validation tests, (3) indirect draw args gated prototype, (4) material registry (stable indices), (5) unit & integration test scaffolding, (6) performance + memory baselines. SceneConstants slot set stabilized (legacy removed). Risk register unchanged; R2 monitoring continues (ordering determinism). No new regressions observed in last single-frame verbose run.
2025-09-04T10:05Z  TransparentPass Alpha Blending: Replaced stale TODO with explicit BlendTargetDesc enabling standard straight alpha blending (SrcAlpha, InvSrcAlpha) and alpha channel blend (One, InvSrcAlpha). Depth writes remain disabled; depth test LessOrEqual retained. Documented future switch path for premultiplied alpha. Prepared groundwork for multi-range partition validation (transparent bucket now visually distinct). TODO still open separately to unify pass mask bits into strong enum (tracked in Renderer.cpp).
2025-09-04T10:05Z  Minimal Transparency Classification: Added temporary pass mask classification in `Renderer::FinalizeScenePrepSoA` with raw bit constants: bit0 OpaqueOrMasked, bit1 Transparent. Implemented conservative rule (domain + base alpha threshold) and extensive comment documenting future expansion (additive, transmission, decals, UI) plus TODO to replace magic bits with a strongly typed enum once taxonomy fixed. Validation asserts updated to guard against unexpected bits. Next: introduce `TransparentPass` to exercise multi-range partitions and verify sorting / partition coverage (Tasks 10,11). Will later refactor flags into central enum and add unit tests (Tasks 19,25).
2025-09-04T10:15Z  TransparentPass Implementation: Added `TransparentPass` (new files) filtering DrawMetadata by transparent flag (bit1). Pipeline mirrors ShaderPass with depth write disabled; blend state placeholder kept (TODO enable explicit alpha blending). Work log entry added; next steps: register pass in sample, force some materials/domains to produce transparent bucket, validate multi-range partitions and update status (Tasks 10,11 progression). Future: centralize pass flag enum & enable blend state customization.
2025-09-05T00:15Z  Sample Scene Simplification: Reworked `Examples/Async` scene for deterministic visual validation. Replaced animated orbit camera with fixed 45° pitched camera looking at origin (stabilizes depth relationships). Centered multi-submesh quad at origin facing +Z (no rotation) to ensure consistent front-face culling & alpha evaluation. Added sphere orbital animation around origin on XZ plane (radius=4) to exercise changing overlap against quad for transparency correctness. Removed quad rotation & camera animation variables (`quad_rotation_angle_`, `AnimateMainCamera`). Added helper `SetupFixedCamera` (one-time) and `AnimateSphereOrbit` (per-frame). Intent: stable baseline for future transparent ordering work (back-to-front/OIT) and partition map multi-range validation. No pass or renderer logic modified; purely scene graph adjustments. Next related step: implement transparent draw ordering strategy once partition maps include transparent range with >1 overlapping depth surfaces.
2025-09-05T00:30Z  Multi-Range Partitions Activated: With transparent classification and `TransparentPass` live, frames containing transparent draws now produce ≥2 partition ranges (opaque, transparent). Deterministic ordering verified (opaque range precedes transparent via pass_mask sort key). Removes blocker for closing Task 10 (all derived passes adapted) and Task 11 (multi-range partition map). Added deferred design TODO for transparent ordering/OIT.

2025-09-05T10:40Z  Normal Matrix Storage Refactor: TransformManager now stores normal
				   matrices natively as glm::mat4 alongside world matrices (no
				   per-frame promotion). Renderer updated (`PopulateMatrices`,
				   `PublishPreparedFrameSpans`) to treat normals identically to
				   worlds with zero-copy aliasing when enabled. Enabled
				   `alias_normal_matrices_` by default (previously false) and
				   removed temporary promotion buffer logic. This satisfies the
				   Transform Manager integration requirement for matrix
				   caching & aliasing.
2025-09-05T10:42Z  Normal Aliasing Documentation: Added integration notes blocks to
				   `FullScreenTriangle.hlsl` and `DepthPrePass.hlsl` detailing
				   how to bind and consume normal matrices in future lighting /
				   shading stages (slot addition pattern, cast to float3x3,
				   avoiding inverse-transpose in shader). No functional shader
				   changes required yet (passes currently unlit). Marks shader
				   struct sync (Task 25) partially progressed (docs present,
				   no new fields required today).
2025-09-05T10:45Z  Status Consolidation: With three SoA-driven passes (Depth,
				   Shader, Transparent) and multi-range partitions active, SoA
				   pipeline core (Task 6) considered COMPLETE. Remaining prior
				   sub-items (indirect draw args prototype, advanced
				   transparent ordering/OIT, material registry) explicitly
				   deferred to focused next steps. Updated task table; removed
				   obsolete "Short Horizon" & remaining Task 6 sub-step lists.
				   Open Question Q1 (normal matrices exposure) RESOLVED: we
				   expose & cache normal matrices (glm::mat4). Q2 transparency
				   ordering still deferred (classification done). Q3 transform
				   dedupe epsilon remains exact equality (acceptable baseline).

Focused Next Steps (Post Normal-Matrix Refactor @2025-09-05T10:45Z)
-------------------------------------------------------------------
DONE N1: Non-indexed draw support (explicit `vertex_count`).
DONE N2a: Pass mask bit (opaque) populated.
DONE N2b: Multi-range partitions (opaque + transparent) active.
DONE N3: Pointer-based material dedup (interim).
DONE N4: Validation asserts (transform/material indices, flags non-zero).
DONE N5: Indirect draw args DESIGN NOTE authored (prototype deferred).
DONE N6: Normal matrices native mat4 storage + default aliasing.

NEXT (Design / Implementation):
1. Indirect draw args prototype (flagged) + metrics (deferred but high value).
2. Transparent ordering strategy (depth sort vs weighted OIT) design finalize & initial implementation.
3. Material registry (stable hashed indices) replacing pointer map; update DrawMetadata.material_index semantics.
4. Test suite: unit (partition coverage, ordering determinism hash, transform aliasing), integration (multi-pass render parity), death/assert tests.
5. Performance & memory baselines across draw count tiers (capture sort_time_us, finalize_time_us, alloc high-water, submission cost with vs without indirect).
6. Documentation scrub: remove residual AoS mentions, consolidate bindless slot conventions, add normal matrix aliasing rationale.
7. Optional: Prepare indirect args buffer schema for GPU culling (flags bit usage doc) ahead of prototype.

Deferred (Explicit):
- Advanced transparent OIT (beyond simple back-to-front) until artifact evidence.
- GPU-driven culling & args compaction until indirect baseline stabilized.
- Instancing support (no representative content yet).

Test Plan Outline (Draft):
- Partition Coverage: ensure union of ranges == draw_count, no overlap.
- Ordering Determinism: render hash stable across 100 frames with static scene.
- Transform Aliasing: modify subset; verify only dirty world upload regions (future metrics hook) & normal matrices remain consistent.
- Pass Mask Validation: opaque + transparent counts consistent with material classification rules.

Cleanup Batch (Queued):
- Remove legacy test artifacts & create SoA-focused tests.
- Macro / include hygiene & clang-tidy sweep.
- Changelog & migration note summarizing AoS removal & matrix aliasing.

Deferral Notes
--------------
- Indirect draw args intentionally deferred until baseline performance metrics captured.
- Transparent ordering / OIT deferred (current scenes low overlap complexity).
- Material registry (Task 13) deferred until after indirect args decision (avoid churn in indexing scheme).

Updated Task Status Summary (Snapshot @2025-09-05T10:45Z)
---------------------------------------------------------
 1 D  PreparedSceneFrame type (in active use)
 2 D  DrawMetadata layout extension (vertex_count + index_count, 52-byte packed)
 3 D  Remove RenderItem structs
 4 D  Remove RenderItemsList container
 5 D  Purge bridge conversion
 6 D  Finalize SoA pipeline (core complete; indirect args & ordering deferred)
 7 D  Renderer integration
 8 D  RenderContext update
 9 D  RenderPass interface refactor (shared SoA draw helper)
10 D  Adapt all derived passes (DepthPrePass, ShaderPass, TransparentPass migrated)
11 D  Partition map + pass mask computation (multi-range operational: opaque + transparent)
12 D  Transform manager integration (stable handles, world & normal aliasing)
13 NS Material registry integration (stable indices pending)
14 NS Geometry residency integration (beyond EnsureMeshResources caching)
15 D  MeshView expansion
16 D  Sorting strategy implementation (stable sort + hashes + timing)
17 IP Buffer upload plumbing (world sparse dirty uploads + normal aliasing; indirect args outstanding)
18 D  Static layout validation
19 NS Unit tests finalization core
20 NS Integration tests passes
21 NS Delete legacy tests (AoS tests pending removal)
22 IP Documentation update (slot cleanup done; shader normal docs added; broader scrub pending)
23 NS CMake maintenance
24 NS Include cleanup (post-doc scrub)
25 IP Shader struct sync (normal matrix usage documented; future fields pending)
26 NS Performance baseline (post multi-range & indirect args)
27 NS Memory reuse strategy (formal metrics emission)
28 NS Error handling & logging consolidation
29 D  Remove recompute logic (RenderItem methods eliminated with AoS)
30 NS Add debug frame dump
31 IP SceneConstants adjustments (slots harmonized; potential future pass-mask indices)
32 NS Pass iteration helpers
33 NS Remove unused macros
34 NS CI/presets verification
35 NS Update export lists
36 NS Namespace hygiene
37 NS Clang-tidy / formatting sweep
38 NS Final grep sweep
39 NS Changelog / migration note
40 NS Remove design placeholders

EOF

TODO DESIGN NOTE (Transparent Ordering / OIT)
--------------------------------------------
Current State: Transparent draws occupy a contiguous partition sorted only by coarse key (pass_mask, material_index, buffer indices). No intra-partition depth ordering.

Problem: Correct blending requires either back-to-front order or an order-independent technique. Present ordering may produce artifacts with overlapping transparent geometry.

Candidate Strategies (deferred):
1. Back-to-front CPU depth sort each frame (simple, deterministic; CPU cost O(T log T)).
2. Weighted blended OIT (dual RT: accum + revealage; approximate but stable; bandwidth cost).
3. Per-pixel linked lists (A-buffer) (correct; high memory & complexity).
4. Hybrid (peel first layer(s) then weighted blend remainder) (balance quality/perf).

Planned Sequence: Author design & metrics plan now, implement simple depth sort after indirect args prototype; evaluate need for OIT based on artifact severity & content.

Instrumentation TODO (future): measure transparent range size, depth-sort time, hash of ordered indices; compare artifacts count before/after strategy change.

Exit Criteria (future): selectable strategy, deterministic ordering hash for static scene, documented perf deltas.

INDIRECT DRAW ARGS DESIGN NOTE (Draft)
=====================================

Motivation
----------
Current submission loops over DrawMetadata issuing direct Draw / DrawIndexed calls.
For large draw counts this incurs CPU overhead (call overhead, root constant binds)
and limits batching potential. Indirect args enable:
1. Lower CPU submission cost (single ExecuteIndirect / MultiDraw path later).
2. GPU-driven culling / compaction potential (future phases can rewrite args).
3. Natural place to integrate per-draw overrides (LOD, visibility) via compute.

Scope (Initial Prototype)
-------------------------
Non-indexed & indexed draws only (no instancing expansion yet). One args struct
per DrawMetadata record kept 1:1 (no compaction). Direct path retained behind
runtime flag for A/B comparison and fallback.

API Targets
-----------
Primary target: D3D12 ExecuteIndirect. (Future: Vulkan vkCmdDraw*Indirect,
Metal Indirect Command Buffers). Keep abstraction thin so translation layer can
map to platform-specific requirements.

Args Layout (CPU Definition)
---------------------------
struct DrawIndexedArgs {
	uint32_t index_count_per_instance;
	uint32_t instance_count;          // always 1 for now
	uint32_t start_index_location;
	int32_t  base_vertex_location;
	uint32_t start_instance_location; // 0
};

struct DrawArgs {
	uint32_t vertex_count_per_instance;
	uint32_t instance_count;          // 1
	uint32_t start_vertex_location;   // 0 (no base offset today)
	uint32_t start_instance_location; // 0
};

Selection: Use distinct contiguous arrays for indexed vs non-indexed OR unify
with a tagged variant. Prototype will emit a single unified array of fixed
size records choosing the larger (DrawIndexedArgs = 20 bytes). Pad to 32 bytes
for alignment & future fields (e.g., material index, draw_metadata_index).

Proposed Unified Args Record (32 bytes)
--------------------------------------
Offset  Size  Field
0       4     flags (bit0 indexed, bit1 reserved, others TBD)
4       4     index_or_vertex_count
8       4     instance_count (1)
12      4     first_index (indexed) OR start_vertex (non-indexed=0)
16      4     base_vertex (indexed) OR unused
20      4     draw_metadata_index (lookup into DrawMetadata; enables shader access)
24      4     material_index (duplicate for compute cull shaders convenience)
28      4     reserved (future: lod_or_mask)

Reasoning: Embedding material_index & draw_metadata_index avoids extra indirection
on GPU side if future mesh shader / amplification stage consumes args directly.
32-byte alignment plays well with structured buffer stride & potential SIMD
processing in compute cull.

Generation Pipeline
-------------------
1. After DrawMetadata finalized & sorted, allocate args buffer CPU vector sized
	 to draw_count (reserve high-water once).
2. For each DrawMetadata:
	 - Set flags.indexed from md.is_indexed.
	 - index_or_vertex_count = md.is_indexed ? md.index_count : md.vertex_count.
	 - first_index/base_vertex from md.
	 - draw_metadata_index = sequential i.
	 - material_index from md.material_index.
3. Upload to GPU as structured buffer (Srv/Uav) OR copy into an indirect args
	 buffer resource depending on backend requirement.
4. ExecuteIndirect signature references fields needed by hardware; other fields
	 (draw_metadata_index/material_index) are for future GPU-side filtering passes.

Execution Path (Prototype)
--------------------------
if (use_indirect) {
	ExecuteIndirect(signature, draw_count, args_buffer, 0, nullptr, 0);
} else {
	for each DrawMetadata → direct draws (current path)
}

Signature (D3D12) Mapping
-------------------------
Argument 0: D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED OR DRAW
						(Need two signatures or a superset approach; simplest: generate
						 only indexed signature and emit index args for all; for non-indexed
						 create synthetic index buffer? → Defer. Prefer dual range submit.)
Strategy: Partition draws into indexed/non-indexed contiguous groups (already
possible via sort key extension) and issue two ExecuteIndirect calls.

Phased Migration
----------------
Phase A (Prototype): Build args buffer; still issue direct draws (validate parity).
Phase B (Shadow Execute): ExecuteIndirect then also (optionally) direct draws for
	a few frames under debug to confirm identical GPU results (hash over color/depth).
Phase C (Flip Default): Indirect path default; direct path behind diagnostic flag.
Phase D (GPU Cull Ready): Add compute pass able to mark flags bit (e.g., visibility=0)
	and compaction step (prefix sum) producing filtered args buffer (future task).

Validation Strategy
-------------------
1. Count parity: args_count == draw_metadata_count.
2. Field parity: For random sample, reconstruct what direct draw would call and compare.
3. Visual parity: Off-screen hash of color/depth after both paths (debug builds).
4. Performance counters: CPU submission time diff; GPU timestamp around execute.

Instrumentation
---------------
- last_args_build_time_us
- last_execute_indirect_cpu_time_us
- counters: indexed_count, non_indexed_count
- optional: memory_bytes_args_buffer (high-water)

Risks & Mitigations
-------------------
R1: Divergence between DrawMetadata and Args after future mutations.
		→ Centralize args build immediately after final sort; forbid later edits.
R2: Mixed indexed/non-indexed complexity for single ExecuteIndirect signature.
		→ Submit in two calls (indexed first) using partitioning; unify later if needed.
R3: Extra memory footprint.
		→ 32 bytes * N draws (N=10k ⇒ ~320 KB) acceptable; monitor high-water.
R4: Debug difficulty when GPU-driven culling added.
		→ Keep draw_metadata_index in record for round-trip diagnostics.

Config & Flags
--------------
RendererConfig {
	bool enable_indirect_submission; // default false until Phase C
	bool enable_indirect_validation; // shadow compare mode
}

Open Questions
--------------
Q1: Merge args + metadata into single buffer? (Defer; separation aids clarity.)
Q2: Encode pass_mask into args? (Likely unnecessary; metadata already has it.)
Q3: Support instancing early? (Defer until real instanced content present.)

Exit Criteria (Prototype Complete)
----------------------------------
- Args buffer generated & uploaded each frame behind flag.
- Dual-path visual parity confirmed (hash or frame diff) for sample scene.
- CPU submission time improvement measured & logged.
- Design note updated with metrics before enabling by default.

END INDIRECT DRAW ARGS DESIGN NOTE
