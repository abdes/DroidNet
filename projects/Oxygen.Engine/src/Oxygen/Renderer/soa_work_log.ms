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

Open Questions (Active Only)
----------------------------
Q2: Need separate transparency ordering / OIT strategy? (Classification present; ordering deferred.)
Q3: Transform dedupe epsilon threshold? (Currently exact bitwise equality; reassess when animated scene count grows.)

Resolved Questions (Historical)
-------------------------------
Q1: Normal matrices exposure → Expose & cache normal matrices (glm::mat4) alongside world (no inverse-transpose in shader).

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
2025-09-04T00:00Z Init: baseline 3 draws; AoS bridge active.
2025-09-04T00:10Z PSF scaffolding added (headers + CMake).
2025-09-04T00:25Z DrawMetadata extended (material_slot, transform_index).
2025-09-04T00:50Z SoA finalize skeleton adds world/normal spans (no metadata yet).
2025-09-04T01:20Z Finalize respects filtered indices; metrics + high-water tracking.
2025-09-04T01:40Z Plan pivot: integrate PSF into context before full metadata.
2025-09-04T01:55Z PSF pointer wired into RenderContext; timing instrumentation added.
2025-09-04T02:15Z Minimal metadata population (transform_index only) + size parity.
2025-09-04T02:30Z Full metadata copied for parity; PSF exposes bytes.
2025-09-04T02:45Z DepthPrePass gains provisional SoA path (counts pending).
2025-09-04T03:00Z Plan: direct generation of DrawMetadata from ScenePrep (submeshes).
2025-09-04T03:20Z Direct DrawMetadata generation (per MeshView); legacy copy removed.
2025-09-04T03:35Z Added real vertex/index bindless indices; debug sample logging.
2025-09-04T04:05Z Index counts populated; DepthPrePass issues SoA DrawIndexed.
2025-09-04T04:25Z DepthPrePass now SoA-only (legacy fallback removed).
2025-09-04T04:40Z Material constants SoA + (legacy) material_index population.
2025-09-04T04:55Z Fix material generation identifier; single-frame validation.
2025-09-04T05:05Z Status refresh; tasks updated post dual-path removal.
2025-09-04T05:25Z Material dedup via pointer map (interim solution).
2025-09-04T05:45Z Pass mask flag (opaque) + validation asserts added.
2025-09-04T06:05Z Temp non-indexed support (index_count repurposed) [superseded].
2025-09-04T07:55Z Added explicit vertex_count field; unified draw logic.
2025-09-04T06:35Z ShaderPass migrated to SoA draws.
2025-09-04T07:05Z Mesh pipeline shader filename issue; temporary Mesh.hlsl added.
2025-09-04T08:10Z DrawMetadata stride fix (tightly packed 52 bytes).
2025-09-04T08:25Z Central helper IssueDrawCallsFromMetadata introduced.
2025-09-04T08:40Z Partition scaffolding: single opaque range published.
2025-09-04T09:05Z Stable sort + multi-range partition + order hashing.
2025-09-04T09:15Z Sort timing metric added (sort_time_us).
2025-09-04T09:30Z Removed RenderItem/AoS types; SoA exclusive.
2025-09-04T09:40Z Removed bindless_indices_slot; naming harmonized.
2025-09-04T09:55Z Sync: AoS removal & slot cleanup recorded.
2025-09-04T10:05Z Transparent blend state defined (alpha); groundwork for partitions.
2025-09-04T10:05Z Transparent classification bits (opaque, transparent) added.
2025-09-04T10:15Z TransparentPass implemented (bit1 filter).
2025-09-05T00:15Z Sample scene stabilized (fixed camera + orbit sphere).
2025-09-05T00:30Z Multi-range partitions active (opaque + transparent).
2025-09-05T10:40Z Normal matrices stored natively; aliasing enabled.
2025-09-05T10:42Z Added normal aliasing shader docs.
2025-09-05T10:45Z Core SoA pipeline complete (3 passes, partitions); open items deferred.
2025-09-05T11:00Z MaterialRegistry introduced (stable handles start at 1).
2025-09-05T11:05Z Removed per-frame MaterialUploadCache (registry sole source).
2025-09-05T11:10Z Removed world_transform from RenderItemData (handle-based lookup).
2025-09-05T11:15Z Added MaterialRegistry unit tests (handles, sentinel, lookup).
2025-09-05T11:30Z DrawMetadata now stores stable MaterialRegistry handles (field later renamed material_handle); added migration asserts.
2025-09-05T11:45Z Breaking rename: DrawMetadata.material_index -> material_handle across C++/HLSL; docs/tests pending full scrub.
2025-09-05T12:05Z Const-correctness: FinalizeScenePrepSoA / GenerateDrawMetadataAndMaterials now mutate ScenePrepState (material registry) safely.
2025-09-05T12:10Z Material constants buffer realigned to stable MaterialHandle indices (removed per-frame packed mapping causing wrong materials on GPU); visual validation passed.
2025-09-05T12:25Z GeometryRegistry extended with mesh-level GetOrRegisterMesh (stable vertex/index buffer indices via callback provisioning).
2025-09-05T12:35Z Removed obsolete GeometryResidencyCache (code + docs); ScenePrepState now owns persistent GeometryRegistry.
2025-09-05T12:45Z Renderer integrated mesh registration through GeometryRegistry; DrawMetadata now uses stable geometry handle derived indices.
2025-09-05T12:55Z Temporary instrumentation (per-mesh & per-frame geometry registration logs) added for validation.
2025-09-05T13:05Z Removed temporary geometry registration debug logging (noise reduction after validation).

Current Active Roadmap (Post Geometry Residency Integration @2025-09-05T13:05Z)
-----------------------------------------------------------------------
Completed (Foundation Snapshot): Non-indexed draws, pass mask bits (opaque + transparent), multi-range partitions, validation asserts,
normal matrix aliasing & caching, stable MaterialRegistry handles in DrawMetadata, removal of per-frame packed material mapping,
world_transform field removal, initial sorting + partition map + order hashing, shader struct sync, design notes (Indirect Args, OIT),
stable GeometryRegistry (assets + mesh-level) with cache removal & renderer integration.

Upcoming (Actionable):
1. Indirect Draw Args Prototype: Implement unified 32-byte record build + optional shadow validation path; capture args_build_time_us.
2. Transparent Ordering Baseline: Add back-to-front CPU depth sort over transparent partition (hash + timing instrumentation).
3. Test Suite Expansion: Partition coverage/non-overlap, ordering determinism (hash), transform aliasing correctness, handle stability across frames, death/assert scenarios.
4. Performance & Memory Baselines: Record finalize_time_us, sort_time_us, args_build_time_us, high-water vector sizes before enabling indirect by default.
5. Documentation Scrub: Purge residual AoS references; add handle semantics (transform/material), sentinel rules, matrix aliasing rationale, sparse material constants alignment note.
6. Indirect Args GPU Culling Prep (Optional): Specify visibility flag bits & compaction sequence (defer until indirect prototype stable).

Planned Optimization (Post-Prototypes): Optional packed MaterialConstants indirection (handle->packed index) if sparse handle space causes significant memory overhead; current sparse-aligned approach favored for correctness & simplicity.

Deferred (Explicit): Advanced transparent OIT, GPU-driven culling/compaction, instancing support (no representative content yet).

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

Active Task Status Summary (Transitional Tasks Pruned @2025-09-05T13:05Z)
-----------------------------------------------------------------------
14 CM Geometry residency integration (stable registry + renderer usage; pending future MeshResourceManager for upload/eviction separation)
17 IP Buffer upload plumbing (indirect args & sparse world upload optimization pending)
19 NS Unit tests finalization core (SoA pipeline coverage)
20 NS Integration tests passes (multi-pass parity & ordering)
21 NS Delete legacy tests (remove remaining AoS-era tests & fixtures)
22 IP Documentation update (scrub + handle semantics + aliasing rationale)
23 NS CMake maintenance
24 NS Include cleanup (post-doc scrub)
25 IP Shader struct sync (future fields & alignment audits; current docs added)
26 NS Performance baseline (capture metrics before indirect path flip)
27 NS Memory reuse strategy (formal high-water tracking emission)
28 NS Error handling & logging consolidation
30 NS Add debug frame dump (deterministic JSON / hashable export)
31 IP SceneConstants adjustments (future pass-mask indices / indirect args slots)
32 NS Pass iteration helpers (iterator utilities over partitions)
33 NS Remove unused macros
34 NS CI/presets verification (multi-preset SoA validation)
35 NS Update export lists (public headers post-prune)
36 NS Namespace hygiene
37 NS Clang-tidy / formatting sweep
38 NS Final grep sweep
39 NS Changelog / migration note (AoS removal & handle integration)
40 NS Remove design placeholders (obsolete TODO clusters)

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
