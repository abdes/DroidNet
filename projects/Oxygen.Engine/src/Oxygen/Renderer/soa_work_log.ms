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

Focused Next Steps (Revised @2025-09-04T09:55Z)
------------------------------------------------
DONE N1: Non-indexed draw support (explicit `vertex_count`).
DONE N2a: Basic pass mask bit (opaque) populated.
IP   N2b: Partition multi-range refinement (currently single opaque range; need ≥2 distinct masks to validate segmentation logic + tests).
DONE N3: Pointer-based material dedup (will evolve to stable registry / Task 13).
DONE N4: Validation asserts (transform/material indices, flags non-zero).
OPEN N5: Indirect draw args design note (buffer layout, alignment, transition plan, compatibility with direct path) – to be authored before prototype.

Immediate (ordered):
1. Documentation & markdown scrub (Task 22 IP): remove `bindless_indices_slot` references; update DrawMetadata description (vertex_count/index_count fields; 52-byte packed) and slot naming consistency.
2. Partition multi-range exercise: introduce a synthetic second pass mask (e.g., transparency flag) on a subset or test harness; validate partition ranges (coverage, non-overlap, ordering) with forthcoming unit tests (Tasks 11,19).
3. Indirect draw args prototype (Task 17 IP): generate args buffer parallel to DrawMetadata (flag-gated); maintain current direct submission to ensure parity; capture timing deltas.
4. Material registry integration (Task 13 NS): replace per-frame pointer map with stable index allocation & hash-based reuse; update dedupe stats.
5. Unit & integration test scaffolding (Tasks 19–21 NS): tests for DrawMetadata size/layout, stable sort determinism (hash), partition correctness, material dedup invariants; remove legacy AoS tests (Task 21).
6. Performance & memory baselines (Tasks 26–27 NS): record finalize_time_us, sort_time_us across N= {10,100,1k,10k} draws; track allocations & high-water reservations.
7. SceneConstants future adjustments (Task 31 IP): evaluate need for pass mask or partition indices in constants or keep CPU-side only.

Revised Task 6 Exit Criteria (remaining to close Task 6):
 - Multi-range partition (≥2 masks) implemented & validated by tests.
 - Indirect draw args prototype gated & validated (optional for exit if deferred explicitly).
 - Third geometry-relevant pass migrated OR explicitly documented that only two are required for current sample set.
 - All AoS artifacts removed (DONE) and docs reflect SoA-only pipeline (pending doc scrub).

Remaining Task 6 Sub-Steps Tracking:
 - 6.b Partition map expansion (multi-range) → IP.
 - 6.d Extended parity asserts for any new fields (defer until indirect args integration decision).
 - 6.f Timing metrics DONE.

Deferral Notes
--------------
- Partition map & sorting (Tasks 11 & 16) intentionally delayed until at least one pass fully SoA to avoid churn in ordering assumptions.
- Material / geometry integration (Tasks 13/14) waits on DrawMetadata serialization pattern being locked.

Execution Order Snapshot
------------------------
Current Focus: Task 6 (complete remaining sub-steps)
Next: Tasks 7 & 8 (RenderContext + initial pass refactor)
Then: Task 10 (broader pass adaptation) → Task 11 (partition map) → Task 16 (sorting) → Tasks 3–5 (remove AoS once all passes converted and parity validated).

Updated Task Status Summary (Snapshot @2025-09-04T09:55Z)
----------------------------------------------------------
 1 D  PreparedSceneFrame type (in active use)
 2 D  DrawMetadata layout extension (vertex_count + index_count, 52-byte packed)
 3 D  Remove RenderItem structs
 4 D  Remove RenderItemsList container
 5 D  Purge bridge conversion
 6 IP Finalize SoA pipeline (pending: multi-range partitions, indirect args decision, third-pass scope)
 7 D  Renderer integration
 8 D  RenderContext update
 9 D  RenderPass interface refactor (shared SoA draw helper)
10 IP Adapt all derived passes (DepthPrePass & ShaderPass migrated; additional passes TBD)
11 IP Partition map + pass mask computation (single opaque range; multi-range planned)
12 NS Transform manager integration
13 NS Material registry integration (stable indices pending)
14 NS Geometry residency integration (beyond EnsureMeshResources caching)
15 D  MeshView expansion
16 D  Sorting strategy implementation (stable sort + hashes + timing)
17 IP Buffer upload plumbing (indirect args prototype outstanding)
18 D  Static layout validation
19 NS Unit tests finalization core
20 NS Integration tests passes
21 NS Delete legacy tests (AoS tests pending removal)
22 IP Documentation update (slot cleanup & SoA-only docs pending)
23 NS CMake maintenance
24 NS Include cleanup (post-doc scrub)
25 IP Shader struct sync (future fields e.g., pass/material tables)
26 NS Performance baseline (post multi-range & indirect args)
27 NS Memory reuse strategy (formal metrics emission)
28 NS Error handling & logging consolidation
29 D  Remove recompute logic (RenderItem methods eliminated with AoS)
30 NS Add debug frame dump
31 IP SceneConstants adjustments (slot harmonized; potential pass-mask indices)
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
