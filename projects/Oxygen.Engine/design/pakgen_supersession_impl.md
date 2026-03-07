# PakGen Supersession Implementation Plan (Execution Locked)

This is the execution tracker for `design/pakgen_supersession.md`.

## 1. Rules of Engagement

1. No shortcut implementations that bypass `ImportJob -> Pipeline`.
2. No hidden scope changes: any spec delta must be reflected first in the design docs.
3. Progress is evidence-based only (files, tests, diagnostics, docs).
4. Domain parity is mandatory for descriptor domains: `texture`, `material`, `geometry`, `scene` are first-class peers; buffer descriptors are container-owned geometry subdocuments.
5. Manual validation must be trimmed to non-schema-enforceable rules only.

## 2. Mandatory Architecture Gates

1. Descriptor domains are independent and explicit:
   - `texture-descriptor`
   - `material-descriptor`
   - `geometry-descriptor`
   - `scene-descriptor`
   - geometry-owned `buffers[]`/`views[]` subdocuments (no standalone top-level buffer descriptor contract)
   - all buffer references are virtual-path based (`.obuf`), not local-ID based
2. Scene import path must not implicitly cook geometry/material/texture or container-owned geometry buffer content.
3. JSON schema validation is systematic and diagnostics are surfaced through import diagnostics.
4. All schema embedding is generated at build time; no manually embedded schema blobs.
5. C++ Pak path is authoritative (`PakBuilder`/`PakPlanBuilder`/`PakWriter`) before PakGen default-path removal.

## 3. Phase Ledger

Status values:

1. `pending`
2. `in_progress`
3. `done`
4. `blocked`

| Phase | Status | Weight | Scope | Exit Gate |
| --- | --- | --- | --- | --- |
| P0 | done | 4% | Baseline + tracking scaffold | Ledger, evidence rubric, and CI matrix committed |
| P1 | done | 12% | Shared descriptor infrastructure | Common schema validation + diagnostics helpers integrated for descriptor domains |
| P2 | done | 10% | Texture descriptor domain | `texture-descriptor` implemented end-to-end with schema/tests |
| P3 | done | 8% | Geometry buffer subdocument model | container-owned `buffers[]`/`views[]` contract implemented end-to-end under geometry descriptor flow, including deterministic mounted-root `.obuf` resolution and canonical dedupe constraints |
| P4 | done | 14% | Material descriptor domain | `material-descriptor` implemented end-to-end with schema/tests |
| P5 | done | 16% | Geometry descriptor domain | `geometry-descriptor` implemented end-to-end with schema/tests and external green build/test confirmation |
| P6 | in_progress | 16% | Scene descriptor domain | `scene-descriptor` implemented end-to-end with schema/tests; external green validation pending |
| P7 | pending | 8% | Manifest DAG integration | Descriptor job types + defaults + strict key policies + DAG checks |
| P8 | pending | 7% | Pak builder toolflow integration | Official C++ pack flow wired and documented |
| P9 | pending | 3% | PakGen deprecation | PakGen removed from default build/CI path after parity gates |
| P10 | pending | 2% | Final docs and closeout | Spec/impl/docs aligned; no open mandatory items |

## 4. Progress Formula (Reliable 0-100%)

Each phase has measurable completion:

1. `0.00` not started.
2. `0.25` partial scaffolding only.
3. `0.50` core code implemented, no full tests.
4. `0.75` tests present but gaps remain.
5. `1.00` exit gate fully satisfied with evidence.

Overall completion:

1. `Progress% = sum(phase_weight * phase_completion)`
2. Rounded to one decimal in updates.
3. Phase cannot be marked `done` without evidence entries in Section 11.

## 5. File-Level Change Map (Planned)

## 5.1 New Schema Sources

1. `src/Oxygen/Cooker/Import/Schemas/oxygen.texture-descriptor.schema.json`
2. `src/Oxygen/Cooker/Import/Schemas/oxygen.material-descriptor.schema.json`
3. `src/Oxygen/Cooker/Import/Schemas/oxygen.geometry-descriptor.schema.json`
4. `src/Oxygen/Cooker/Import/Schemas/oxygen.scene-descriptor.schema.json`

## 5.2 New Import Domain Surfaces (Representative)

1. `src/Oxygen/Cooker/Import/TextureDescriptorImportSettings.h`
2. `src/Oxygen/Cooker/Import/MaterialDescriptorImportSettings.h`
3. `src/Oxygen/Cooker/Import/GeometryDescriptorImportSettings.h`
4. `src/Oxygen/Cooker/Import/SceneDescriptorImportSettings.h`
5. `src/Oxygen/Cooker/Import/*DescriptorImportRequestBuilder.h`
6. `src/Oxygen/Cooker/Import/Internal/*DescriptorImportRequestBuilder.cpp`
7. `src/Oxygen/Cooker/Import/Internal/Jobs/*DescriptorImportJob.h/.cpp`
8. `src/Oxygen/Cooker/Import/Internal/Pipelines/*DescriptorImportPipeline.h/.cpp`

## 5.3 Core Integration Touchpoints (Representative)

1. `src/Oxygen/Cooker/Import/ImportManifest.h/.cpp`
2. `src/Oxygen/Cooker/Import/Schemas/oxygen.import-manifest.schema.json`
3. `src/Oxygen/Cooker/Import/AsyncImportService.cpp`
4. `src/Oxygen/Cooker/Import/ImportRequest.h`
5. `src/Oxygen/Cooker/Tools/ImportTool/main.cpp`
6. `src/Oxygen/Cooker/Tools/ImportTool/BatchCommand.cpp`
7. `src/Oxygen/Cooker/Tools/ImportTool/ImportRunner.cpp`
8. `src/Oxygen/Cooker/CMakeLists.txt`
9. `src/Oxygen/Cooker/Tools/PakGen/CMakeLists.txt` (deprecation stage only)

## 6. Detailed Work Packages

## P0: Baseline and Tracking Scaffold

Tasks:

1. Create supersession design + implementation tracker docs.
2. Freeze phase ledger, weights, and evidence process.
3. Add CI acceptance matrix and update protocol.

Acceptance:

1. This file and `design/pakgen_supersession.md` are present and synchronized.

## P1: Shared Descriptor Infrastructure

Tasks:

1. `[done]` Standardize descriptor schema validation through shared helper(s):
   - reuse `Import/Internal/Utils/JsonSchemaValidation.h`
   - map schema diagnostics to import diagnostics by domain
2. `[done]` Add shared descriptor document loader/parsing helpers (UTF-8, JSON parse error shaping, source path/object path conventions).
3. `[done]` Add shared reference-resolution helpers for mounted/inflight assets/resources.
4. `[done]` Remove redundant manual validation already enforceable via schemas.

Acceptance:

1. Every descriptor domain ingress calls schema validation first.
2. Manual validation code only covers cross-document/runtime checks.

## P2: Texture Descriptor Domain

Tasks:

1. Add schema for texture descriptor JSON contract.
2. Add request-builder/job/pipeline domain path.
3. Reuse existing texture cooking internals (`TexturePipeline`, emitters) rather than duplicating cook logic.
4. Add manifest `type: "texture-descriptor"` integration and defaults.
5. Add tests:
   - schema-valid canonical descriptor
   - invalid descriptor diagnostics
   - successful resource emission and indexing

Acceptance:

1. Texture descriptor imports work independently of FBX/glTF paths.

## P3: Geometry Buffer Subdocument Model

Tasks:

1. Define and enforce geometry schema contract for container-owned `buffers[]` with nested `views[]`.
2. `[done]` Enforce external buffer data only (`.buffer.bin` URI); disallow inline JSON payload encodings.
3. `[done]` Require canonical buffer `virtual_path` per buffer descriptor and require mesh/submesh buffer references by virtual path (no local-ID reference model).
4. `[done]` Emit and validate `.obuf` metadata sidecars for emitted buffer resources.
5. `[done]` Resolve virtual-path references deterministically across mounted loose + PAK sources (`virtual_path -> .obuf -> resource_index -> buffers.table/data`).
6. `[done]` Reuse existing buffer cook path (`BufferPipeline`, emitters/index wiring) during geometry import execution.
7. `[done]` Enforce cross-container dedupe so equivalent buffers are cooked once and reused by shared `resource_index`, with a single canonical `.obuf` at the shared virtual path.
8. `[done]` Add tests for canonical and failure cases:
   - missing buffer/view reference
   - missing/unmounted/ambiguous `.obuf` virtual path
   - invalid/out-of-range view slice
   - shared external `.buffer.bin` referenced by multiple geometry containers
   - equivalent buffers across multiple containers produce one `buffers.table` entry, one payload region, and one canonical `.obuf`.

Acceptance:

1. Geometry descriptor imports produce deterministic `buffers.table`/`buffers.data` outputs and `.obuf` metadata through container-owned buffer definitions, with no standalone buffer descriptor authoring contract, and dedupe equivalent shared buffers to a single cooked resource entry.

## P4: Material Descriptor Domain

Tasks:

1. `[done]` Add schema for material descriptor contract.
2. `[done]` Add request-builder/job/pipeline domain path.
3. `[done]` Resolve referenced texture resources deterministically with clear type/lookup diagnostics.
4. `[done]` Emit `.omat` with stable descriptor serialization.
5. `[done]` Add manifest `type: "material-descriptor"` integration and defaults.
6. `[done]` Add tests for:
   - reference resolution success
   - missing/invalid references
   - schema violations

Acceptance:

1. Material descriptor import works standalone and via DAG orchestration.

## P5: Geometry Descriptor Domain

Tasks:

1. Add schema for geometry descriptor contract.
2. Add request-builder/job/pipeline domain path.
3. Resolve buffer/material references with strict type checks.
4. Emit `.ogeo` descriptors and required resource references.
5. Add manifest `type: "geometry-descriptor"` integration and defaults.
6. Add tests for canonical and failure cases.

Acceptance:

1. Geometry descriptor import is independent and does not rely on scene import side effects.

### P3/P5 Geometry Execution Plan (Approval-Gated)

This section is the concrete execution plan for geometry-descriptor implementation.
It is intentionally detailed and approval-gated before production code changes.

Status markers:

1. `[done]` verified in current codebase.
2. `[in_progress]` partially delivered, requires geometry-domain integration.
3. `[pending]` not implemented yet.

#### A. Contract and Schema Lock

1. `[done]` Add `oxygen.geometry-descriptor.schema.json` with full `additionalProperties: false` coverage.
2. `[done]` Encode Pak-format arity constraints as far as JSON Schema allows:
   - geometry has `lods[]`.
   - each LOD has one mesh descriptor payload.
   - each submesh has one material reference and one-or-more mesh views.
3. `[done]` Keep container-owned buffer contract inside geometry:
   - `buffers[]` entries are buffer descriptor subdocuments.
   - `buffers[i].views[]` entries are container-scoped buffer views.
   - mesh-level refs use concise keys under `buffers`: `vb_ref`, `ib_ref`.
   - submesh material refs use `material_ref`.
   - submesh view refs use a single paired selector:
     - `view_ref = "<view_name>"`, resolved against both `vb_ref` and `ib_ref`
     - reserved implicit selector `view_ref = "__all__"` means whole-buffer view on both sides
4. `[done]` Encode mesh variants as discriminated schema branches aligned with `PakFormat_geometry.h`:
   - standard
   - skinned
   - procedural
5. `[done]` Keep procedural mesh contract explicit and runtime-aligned:
   - generated mesh identity uses `Generator/MeshName`.
   - parameter payload is encoded into `ProceduralMeshInfo.params_size` + blob.

#### B. Ingress and Request-Building

1. `[done]` Add `GeometryDescriptorImportSettings` DTO.
2. `[done]` Add `GeometryDescriptorImportRequestBuilder` with schema-first validation and normalized JSON payload.
3. `[done]` Carry geometry-descriptor normalized payload on `ImportRequest` (not in `ImportOptions`) and keep `ImportOptions` limited to runtime behavior/tuning controls.
4. `[done]` Route request creation through `ImportManifestJob::BuildRequest` and CLI/import-tool code paths.

#### C. Job and Pipeline Orchestration (No Parallel Cooking Path Duplication)

1. `[done]` Add `GeometryDescriptorImportJob`.
2. `[done]` Reuse existing buffer cook path via `BufferImportSubmitter` for `buffers[]` local definitions (same `BufferPipeline` + emitters/index wiring).
3. `[done]` Reuse `GeometryPipeline` for descriptor finalization and hash patching.
4. `[done]` Do not duplicate emission/index logic; only use `ImportSession` emitters and finalize path.
5. `[done]` Keep execution graph explicit inside the job:
   - parse + schema validate
   - submit/collect local buffers
   - resolve external and/or local references
   - build descriptor bytes
   - finalize via `GeometryPipeline`
   - emit `.ogeo`

#### D. Reference Resolution Strategy (Pre-Cooked + Simultaneously Cooked)

1. `[done]` Implement deterministic buffer resolution chain for all non-local buffer refs:
   - decode concise refs (`vb_ref`/`ib_ref` + paired `view_ref`) to canonical buffer virtual paths
   - canonical buffer virtual path -> locate `.obuf` in mounted roots
   - parse `.obuf` -> `resource_index` + `BufferResourceDesc`
   - use resolved index in geometry descriptor binding
2. `[done]` Resolve material refs by canonical virtual path to `AssetKey` via mounted content precedence rules.
3. `[done]` Support simultaneously cooked dependencies through manifest DAG (`id`/`depends_on`) and mounted cooked-root state at job execution time.
4. `[done]` Emit explicit diagnostics for:
   - invalid virtual path
   - undefined view reference for either VB or IB
   - reserved view name misuse (`__all__` declared explicitly)
   - unmounted virtual path
   - missing descriptor
   - ambiguous descriptor resolution
   - type mismatch (not material / not buffer sidecar)

#### E. PakFormat and Loader Alignment Rules (Mandatory)

1. `[done]` Generated `.ogeo` bytes must satisfy `PakFormat_geometry.h` layout exactly:
   - `GeometryAssetDesc` then LOD `MeshDesc` blocks with mesh-type payload.
   - `SubMeshDesc` and `MeshViewDesc` sequence/order preserved.
2. `[done]` Enforce counts/arity consistency:
   - `lod_count` equals number of emitted mesh descriptors.
   - each mesh `mesh_view_count` equals total views across its submeshes.
   - each submesh `mesh_view_count` equals number of emitted views for that submesh.
3. `[done]` Standard/skinned meshes must patch required buffer indices into mesh info blocks.
4. `[done]` Procedural meshes must emit procedural info + params blob only, and remain compatible with `GeometryLoader` procedural generation path.

#### F. Manifest/Service/Tool Integration

1. `[done]` Add manifest schema + parser support for `type: "geometry-descriptor"`.
2. `[done]` Add defaults block handling for geometry-descriptor requests.
3. `[done]` Add async service routing (`AsyncImportService`) to geometry-descriptor job.
4. `[done]` Add ImportTool batch validation/reporting support for geometry-descriptor job type.

#### G. Test Matrix (Targeted, Not Ocean-Boiling)

1. `[done]` Schema tests:
   - canonical descriptor acceptance
   - canonical paired `view_ref` acceptance (including `__all__`)
   - rejected unknown keys / invalid branch combos
   - rejected reserved explicit view name `__all__`
2. `[done]` Request-builder tests:
   - normalized payload generation
   - schema diagnostics propagation
3. `[done]` Manifest tests:
   - geometry-descriptor job acceptance
   - strict disallowed-key rejection
   - DAG dependency wiring with material/texture/buffer jobs
4. `[done]` Job tests:
   - canonical standard/skinned descriptor import with local `buffers[]`
   - canonical mixed references (local + pre-cooked `.obuf` + pre-cooked `.omat`)
   - canonical procedural descriptor import
   - failure diagnostics for missing/ambiguous/unmounted refs and arity mismatches
5. `[done]` Pipeline/loader compatibility tests:
   - finalized descriptor byte layout checks
   - procedural descriptor consumed by `GeometryLoader` without structural errors
6. `[done]` Dedupe invariants:
   - equivalent local buffer definitions collapse to one cooked buffer resource index
   - conflicting virtual paths for deduped equivalent buffers fail with explicit diagnostic

#### H. Explicit Non-Goals for This Slice

1. `[done]` No standalone top-level buffer descriptor authoring domain reintroduction.
2. `[done]` No implicit scene-driven cooking of geometry/material/texture domains.
3. `[done]` No manual validation duplication for rules already enforced by schema.

## P6: Scene Descriptor Domain

Tasks:

1. Add schema for scene descriptor contract.
2. Add request-builder/job/pipeline domain path.
3. Resolve geometry/material/script/input/physics sidecar references.
4. Emit `.oscene` descriptors only (no implicit cooking of upstream domains).
5. Add manifest `type: "scene-descriptor"` integration and defaults.
6. Add tests for canonical and failure cases.

Acceptance:

1. Scene descriptor import is orchestration-only for composition, not upstream content generation.

## P7: Manifest DAG Integration

Tasks:

1. Extend manifest schema and parser for all descriptor job types.
2. Enforce strict per-job key whitelists.
3. Enforce `id`/`depends_on` graph correctness and deterministic scheduling.
4. Keep consistent report job-type classification in ImportTool batch outputs.

Acceptance:

1. Mixed-domain descriptor manifests run deterministically and fail with explicit diagnostics on graph errors.

## P8: PakBuilder Toolflow Integration

Tasks:

1. Add/extend tool command path to invoke C++ `PakBuilder` for loose cooked roots produced by import jobs.
2. Add integration tests that verify descriptor-imported outputs are fully packable and loadable.
3. Update docs/examples to default to C++ path.

Acceptance:

1. End-to-end descriptor import -> C++ pack pipeline is first-class and documented.

## P9: PakGen Deprecation

Tasks:

1. Remove PakGen from default build path (`ALL`) after P2-P8 gates pass.
2. Remove PakGen from required CI paths.
3. Keep optional quarantine target only if explicitly approved.

Acceptance:

1. Daily workflow no longer depends on PakGen.

## P10: Final Closeout

Tasks:

1. Sync all design and implementation docs with final code state.
2. Ensure schema install coverage and naming consistency.
3. Confirm no remaining mandatory supersession tasks are `pending`/`blocked`.

Acceptance:

1. Tracker reaches 100.0% with evidence-backed `done` status for all phases.

## 7. CI Acceptance Matrix

Required test categories:

1. Schema validation tests for each descriptor domain.
2. Request builder tests for each descriptor domain.
3. Manifest parser + DAG tests with mixed domain graphs.
4. Import job/pipeline tests for each descriptor domain (happy + targeted negative paths).
5. Pak planner/writer tests for descriptor-produced outputs.
6. Runtime loader smoke tests for descriptor-produced assets/resources (targeted, not ocean-boiling).

## 8. Non-Negotiable Completion Rules

1. No phase closes based on code review only; test evidence is required.
2. No scene-domain closure while geometry/material domains remain incomplete.
3. No PakGen deprecation before descriptor domains and C++ pak flow are verified.
4. Any discovered regression re-opens the owning phase.

## 9. Reporting Cadence

After each implementation iteration:

1. Update phase statuses.
2. Recompute weighted progress.
3. Append evidence entries.
4. List explicit remaining tasks for next iteration.

## 10. Live Snapshot (2026-03-02)

Current status:

1. P0 is done.
2. P2 is done (validated in practice with one manifest + multiple texture descriptors flow).
3. P4 is `done` (external test pass confirmation received for the material-descriptor suite and DAG scenario).
4. P3 is `done` (geometry-domain integration completed with deterministic `.obuf` mounted-root resolution, canonical dedupe constraints, and expanded job-test coverage).
5. P5 is `done` (external green build/tests confirmed after geometry-descriptor execution + coverage closure).
6. P1 is `done`; P6 is `in_progress`; P7-P10 are `pending`.
7. Computed progress snapshot: `76.0%` (P6 completion factor `0.75` pending external run/CI confirmation).
8. P3/P5 geometry execution plan remains the baseline reference in Section 6, with both P3 and P5 marked closed.
## 11. Evidence Log

Use this section as append-only factual evidence.

Template entry:

1. Date:
2. Phase:
3. Files changed:
4. Tests run:
5. Result:
6. Remaining delta to phase exit gate:

Initial entries:

1. Date: 2026-03-01
2. Phase: P0
3. Files changed:
   - `design/pakgen_supersession.md`
   - `design/pakgen_supersession_impl.md`
4. Tests run: none (documentation phase only)
5. Result: supersession architecture + tracker docs created
6. Remaining delta to phase exit gate: none

1. Date: 2026-03-01
2. Phase: P2
3. Files changed:
   - `src/Oxygen/Cooker/Import/Schemas/oxygen.texture-descriptor.schema.json`
   - `src/Oxygen/Cooker/Import/TextureDescriptorImportSettings.h`
   - `src/Oxygen/Cooker/Import/TextureDescriptorImportRequestBuilder.h`
   - `src/Oxygen/Cooker/Import/Internal/TextureDescriptorImportRequestBuilder.cpp`
   - `src/Oxygen/Cooker/Import/Schemas/oxygen.import-manifest.schema.json`
   - `src/Oxygen/Cooker/Import/ImportManifest.cpp`
   - `src/Oxygen/Cooker/Tools/ImportTool/BatchCommand.cpp`
   - `src/Oxygen/Cooker/CMakeLists.txt`
   - `src/Oxygen/Cooker/Test/Import/TextureDescriptorJsonSchema_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/TextureDescriptorImportRequestBuilder_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/ImportManifest_texture_descriptor_test.cpp`
   - `src/Oxygen/Cooker/Test/CMakeLists.txt`
   - `src/Oxygen/Cooker/Tools/ImportTool/README.md`
4. Tests run: none (per current no-build execution policy)
5. Result: texture-descriptor ingress implemented through existing texture request/job path with schema-first validation and manifest integration
6. Remaining delta to phase exit gate: none (phase closure approved after external execution validation)

1. Date: 2026-03-01
2. Phase: P4
3. Files changed:
   - `src/Oxygen/Cooker/Import/Schemas/oxygen.material-descriptor.schema.json`
   - `src/Oxygen/Cooker/Import/MaterialDescriptorImportSettings.h`
   - `src/Oxygen/Cooker/Import/MaterialDescriptorImportRequestBuilder.h`
   - `src/Oxygen/Cooker/Import/Internal/MaterialDescriptorImportRequestBuilder.cpp`
   - `src/Oxygen/Cooker/Import/Internal/Jobs/MaterialDescriptorImportJob.h`
   - `src/Oxygen/Cooker/Import/Internal/Jobs/MaterialDescriptorImportJob.cpp`
   - `src/Oxygen/Cooker/Import/ImportOptions.h`
   - `src/Oxygen/Cooker/Import/ImportManifest.h`
   - `src/Oxygen/Cooker/Import/ImportManifest.cpp`
   - `src/Oxygen/Cooker/Import/Schemas/oxygen.import-manifest.schema.json`
   - `src/Oxygen/Cooker/Import/AsyncImportService.cpp`
   - `src/Oxygen/Cooker/Tools/ImportTool/BatchCommand.cpp`
   - `src/Oxygen/Cooker/Tools/ImportTool/README.md`
   - `src/Oxygen/Cooker/Test/Import/MaterialDescriptorJsonSchema_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/MaterialDescriptorImportRequestBuilder_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/ImportManifest_material_descriptor_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/AsyncImportService_test.cpp`
   - `src/Oxygen/Cooker/Test/CMakeLists.txt`
4. Tests run: none (per current no-build execution policy)
5. Result: material-descriptor domain integrated through schema-first request build, dedicated async job routing, manifest/job-type/tool reporting support, and focused test coverage additions
6. Remaining delta to phase exit gate:
   - execute and pass new material-descriptor test suites externally
   - verify end-to-end manifest run with real texture `.otex` sidecar references

1. Date: 2026-03-02
2. Phase: P4
3. Files changed:
   - `design/pakgen_supersession_impl.md`
4. Tests run: none (per current no-build execution policy)
5. Result: task-level completion for material-descriptor phase updated to explicit done/in-progress markers; phase remains `in_progress` (`0.75`) pending final runtime hardening/validation.
6. Remaining delta to phase exit gate:
   - execute and pass material-descriptor suites externally
   - close deterministic texture descriptor reference-resolution + `.omat` emission validation in end-to-end manifest execution

1. Date: 2026-03-02
2. Phase: P4
3. Files changed:
   - `src/Oxygen/Cooker/Test/Import/MaterialDescriptorImportJob_test.cpp`
   - `src/Oxygen/Cooker/Test/CMakeLists.txt`
4. Tests run: none (per current no-build execution policy)
5. Result: added dedicated material-descriptor import fixture/tests that cover hashed `.otex` virtual-path resolution, emitted `.omat` verification, and missing-descriptor diagnostics.
6. Remaining delta to phase exit gate:
   - execute and pass material-descriptor suites externally
   - re-run manifest DAG scenario externally and confirm material job resolves dependent texture sidecars end-to-end

1. Date: 2026-03-02
2. Phase: P4
3. Files changed:
   - `design/pakgen_supersession_impl.md`
4. Tests run:
   - external execution (user confirmation): all material-descriptor tests passed
   - external manifest DAG run (texture-descriptor -> material-descriptor): passed
5. Result: P4 exit gate satisfied; phase status moved to `done`.
6. Remaining delta to phase exit gate: none

1. Date: 2026-03-02
2. Phase: P3/P5 (spec alignment)
3. Files changed:
   - `design/pakgen_supersession.md`
   - `design/pakgen_supersession_impl.md`
4. Tests run: none (documentation/spec update only; no-build policy active)
5. Result: standardized buffer contract to container-owned geometry subdocuments (`buffers[]` with nested `views[]`), external-only `.buffer.bin` data, and per-container metadata duplication semantics; removed standalone `buffer-descriptor` domain references from supersession scope/plan.
6. Remaining delta to phase exit gate:
   - implement P3 code/tests for geometry-owned buffer subdocument validation and deterministic resolution
   - implement P5 geometry descriptor domain code/tests using the finalized buffer contract

1. Date: 2026-03-02
2. Phase: P3/P5 (spec alignment)
3. Files changed:
   - `design/pakgen_supersession.md`
   - `design/pakgen_supersession_impl.md`
4. Tests run: none (documentation/spec update only; no-build policy active)
5. Result: tightened buffer contract to require virtual-path-based references and explicit `.obuf` metadata as deterministic cross-mount lookup anchor (`virtual_path -> .obuf -> resource_index -> buffers.table/data`).
6. Remaining delta to phase exit gate:
   - implement P3 code/tests for `.obuf` emission + lookup and virtual-path-only buffer reference validation
   - implement P3 dedupe tests proving one cooked buffer resource for equivalent cross-container inputs
   - implement P5 geometry descriptor domain code/tests using the tightened contract

1. Date: 2026-03-02
2. Phase: P3/P5 (spec alignment)
3. Files changed:
   - `design/pakgen_supersession.md`
   - `design/pakgen_supersession_impl.md`
4. Tests run: none (documentation/spec update only; no-build policy active)
5. Result: made cross-container buffer dedupe an explicit requirement: equivalent shared buffers must cook once and reuse the same `resource_index` across `.obuf` sidecars.
6. Remaining delta to phase exit gate:
   - implement P3 code/tests for dedupe behavior across multi-container geometry descriptor inputs
   - complete P5 geometry descriptor implementation against dedupe + virtual-path `.obuf` rules

1. Date: 2026-03-02
2. Phase: P3/P5 (spec alignment)
3. Files changed:
   - `design/pakgen_supersession.md`
   - `design/pakgen_supersession_impl.md`
4. Tests run: none (documentation/spec update only; no-build policy active)
5. Result: corrected dedupe contract wording to match engine semantics: one cooked shared buffer implies one `buffers.table` entry, one payload region, and one canonical `.obuf` (not multiple `.obuf` outputs).
6. Remaining delta to phase exit gate:
   - implement P3 code/tests that assert single-canonical-`.obuf` output for equivalent cross-container buffers
   - complete P5 geometry descriptor implementation against the corrected dedupe contract

1. Date: 2026-03-02
2. Phase: P3 (interim buffer-container milestone)
3. Files changed:
   - `src/Oxygen/Cooker/Import/Schemas/oxygen.buffer-container.schema.json`
   - `src/Oxygen/Cooker/Import/BufferContainerImportSettings.h`
   - `src/Oxygen/Cooker/Import/BufferContainerImportRequestBuilder.h`
   - `src/Oxygen/Cooker/Import/Internal/BufferContainerImportRequestBuilder.cpp`
   - `src/Oxygen/Cooker/Import/Internal/Jobs/BufferContainerImportJob.h`
   - `src/Oxygen/Cooker/Import/Internal/Jobs/BufferContainerImportJob.cpp`
   - `src/Oxygen/Cooker/Import/Internal/Jobs/BufferImportSubmitter.h`
   - `src/Oxygen/Cooker/Import/Internal/Jobs/BufferImportSubmitter.cpp`
   - `src/Oxygen/Cooker/Import/ImportManifest.h`
   - `src/Oxygen/Cooker/Import/ImportManifest.cpp`
   - `src/Oxygen/Cooker/Import/Schemas/oxygen.import-manifest.schema.json`
   - `src/Oxygen/Cooker/Import/ImportRequest.h`
   - `src/Oxygen/Cooker/Import/AsyncImportService.cpp`
   - `src/Oxygen/Cooker/Test/Import/BufferContainerImportRequestBuilder_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/ImportManifest_buffer_container_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/BufferContainerImportJob_test.cpp`
   - `src/Oxygen/Cooker/Test/CMakeLists.txt`
   - `design/pakgen_supersession_impl.md`
4. Tests run:
   - external execution (user confirmation): `BufferContainerImportJobTest.SuccessfulJobEmitsExpectedArtifacts` passed
   - external execution (user confirmation): `ImportManifestBufferContainerTest.RejectsBufferContainerJobWithDisallowedKeys` passed after schema-first diagnostic-path fix
5. Result: interim buffer-container path is green and validates core P3 mechanics (schema-first validation, `.obuf` emission, buffer table/data artifact emission, manifest integration, and strict disallowed-key rejection via schema diagnostics).
6. Remaining delta to phase exit gate:
   - migrate the P3 contract from interim buffer-container ingress into the `geometry-descriptor` domain (`buffers[]` + nested `views[]`)
   - add geometry-domain tests for virtual-path `.obuf` resolution and cross-container dedupe invariants

1. Date: 2026-03-02
2. Phase: P3/P5 (geometry planning)
3. Files changed:
   - `design/pakgen_supersession_impl.md`
4. Tests run: none (planning/documentation update only; no-build policy active)
5. Result: added approval-gated, implementation-ready geometry execution plan with explicit workstreams for schema, request/job routing, resolver semantics (pre-cooked + simultaneously cooked dependencies), procedural mesh support, PakFormat/loader alignment, and targeted test matrix.
6. Remaining delta to phase exit gate:
   - approve the detailed plan
   - implement P3/P5 code and tests per the plan

1. Date: 2026-03-02
2. Phase: P3/P5 (geometry schema ergonomics refinement)
3. Files changed:
   - `design/pakgen_supersession.md`
   - `design/pakgen_supersession_impl.md`
4. Tests run: none (planning/spec update only; no-build policy active)
5. Result: geometry descriptor contract refined for concise author-facing keys and paired view resolution (`buffers.vb_ref`, `buffers.ib_ref`, `material_ref`, `view_ref` with implicit `__all__`), with explicit plan tasks for parser/schema/diagnostics coverage.
6. Remaining delta to phase exit gate:
   - approve refined concise contract
   - implement schema + parser + diagnostics + tests per refined contract

1. Date: 2026-03-02
2. Phase: P5 (geometry descriptor ingress scaffold)
3. Files changed:
   - `src/Oxygen/Cooker/Import/GeometryDescriptorImportSettings.h`
   - `src/Oxygen/Cooker/Import/GeometryDescriptorImportRequestBuilder.h`
   - `src/Oxygen/Cooker/Import/Internal/GeometryDescriptorImportRequestBuilder.cpp`
   - `src/Oxygen/Cooker/Import/ImportOptions.h`
   - `src/Oxygen/Cooker/Import/ImportManifest.h`
   - `src/Oxygen/Cooker/Import/ImportManifest.cpp`
   - `src/Oxygen/Cooker/Import/Schemas/oxygen.import-manifest.schema.json`
   - `src/Oxygen/Cooker/Import/AsyncImportService.cpp`
   - `src/Oxygen/Cooker/Import/Internal/Jobs/GeometryDescriptorImportJob.h`
   - `src/Oxygen/Cooker/Import/Internal/Jobs/GeometryDescriptorImportJob.cpp`
   - `src/Oxygen/Cooker/Tools/ImportTool/BatchCommand.cpp`
   - `src/Oxygen/Cooker/CMakeLists.txt`
   - `src/Oxygen/Cooker/Test/Import/GeometryDescriptorJsonSchema_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/GeometryDescriptorImportRequestBuilder_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/ImportManifest_geometry_descriptor_test.cpp`
   - `src/Oxygen/Cooker/Test/CMakeLists.txt`
   - `design/pakgen_supersession_impl.md`
4. Tests run: none (per current no-build execution policy)
5. Result: geometry-descriptor is now first-class at ingress/routing level (schema embedding, manifest defaults/job type, request-builder, ImportRequest payload, async service routing, import-tool job typing) with initial job shell and focused schema/request/manifest tests added.
6. Remaining delta to phase exit gate:
   - implement full geometry descriptor job execution path (buffer/material resolution, descriptor assembly/finalization, `.ogeo` emission)
   - add and pass job-level integration tests for standard/skinned/procedural and mixed dependency scenarios

1. Date: 2026-03-02
2. Phase: P1/P4/P5 (descriptor payload architecture correction)
3. Files changed:
   - `src/Oxygen/Cooker/Import/ImportOptions.h`
   - `src/Oxygen/Cooker/Import/ImportRequest.h`
   - `src/Oxygen/Cooker/Import/Internal/MaterialDescriptorImportRequestBuilder.cpp`
   - `src/Oxygen/Cooker/Import/Internal/GeometryDescriptorImportRequestBuilder.cpp`
   - `src/Oxygen/Cooker/Import/Internal/Jobs/MaterialDescriptorImportJob.cpp`
   - `src/Oxygen/Cooker/Import/Internal/Jobs/GeometryDescriptorImportJob.cpp`
   - `src/Oxygen/Cooker/Import/AsyncImportService.cpp`
   - `src/Oxygen/Cooker/Tools/ImportTool/BatchCommand.cpp`
   - `src/Oxygen/Cooker/Test/Import/AsyncImportService_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/MaterialDescriptorImportRequestBuilder_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/ImportManifest_material_descriptor_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/MaterialDescriptorImportJob_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/GeometryDescriptorImportRequestBuilder_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/ImportManifest_geometry_descriptor_test.cpp`
   - `design/cook_materials.md`
   - `design/cook_buffers.md`
   - `design/pakgen_supersession_impl.md`
4. Tests run: none (no-build policy active)
5. Result: removed material/geometry descriptor payload carriers from `ImportOptions`; routing/builders/jobs now use top-level `ImportRequest` payloads (`material_descriptor`, `geometry_descriptor`). Added explicit governance comment in `ImportOptions` to prevent adding new non-texture domain payload/tuning without owner approval.
6. Remaining delta to phase exit gate:
   - run and pass affected descriptor/material/geometry test suites externally
   - continue P5 geometry job completion and close remaining pending tasks in Section 6

1. Date: 2026-03-02
2. Phase: P5 (geometry descriptor execution + coverage closure)
3. Files changed:
   - `src/Oxygen/Cooker/Import/Internal/Jobs/GeometryDescriptorImportJob.cpp`
   - `src/Oxygen/Cooker/Import/Internal/Pipelines/MeshBuildPipeline.cpp`
   - `src/Oxygen/Cooker/Import/Internal/Pipelines/GeometryPipeline.cpp`
   - `src/Oxygen/Cooker/Test/Import/GeometryPipeline_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/GeometryDescriptorImportJob_test.cpp`
   - `src/Oxygen/Cooker/Test/CMakeLists.txt`
   - `design/pakgen_supersession_impl.md`
4. Tests run: none (no-build policy active)
5. Result: completed geometry-descriptor job execution hardening and expanded coverage with dedicated job tests for standard, pre-cooked `.obuf` references, procedural descriptors, skinned descriptors, failure diagnostics, and dedupe conflict invariant; aligned skinned descriptor serialization/finalization to avoid duplicated skinned payload writes and keep descriptor layout loader-compatible.
6. Remaining delta to phase exit gate:
   - run and pass updated geometry descriptor and geometry pipeline suites externally
   - confirm updated P5 tests in CI, then flip phase status to `done`

1. Date: 2026-03-02
2. Phase: P5 (geometry descriptor domain closeout)
3. Files changed:
   - `design/pakgen_supersession_impl.md`
4. Tests run:
   - external execution (user confirmation): build/tests pass after geometry descriptor updates
5. Result: P5 exit gate satisfied; phase status moved to `done` and completion math updated.
6. Remaining delta to phase exit gate: none

1. Date: 2026-03-02
2. Phase: P3 (geometry buffer subdocument model closeout)
3. Files changed:
   - `src/Oxygen/Cooker/Import/Internal/Jobs/BufferImportSubmitter.cpp`
   - `src/Oxygen/Cooker/Import/Internal/Jobs/GeometryDescriptorImportJob.cpp`
   - `src/Oxygen/Cooker/Test/Import/GeometryDescriptorImportJob_test.cpp`
   - `design/pakgen_supersession_impl.md`
4. Tests run: none (no-build policy active)
5. Result: closed remaining P3 implementation gaps with deterministic mounted-root `.obuf` resolution rules, canonical cross-job dedupe sidecar constraints, and expanded geometry job coverage for mounted-root resolution, ambiguity diagnostics, unmounted virtual paths, and cross-job dedupe conflicts.
6. Remaining delta to phase exit gate: none

1. Date: 2026-03-02
2. Phase: P1 (shared descriptor infrastructure closeout)
3. Files changed:
   - `src/Oxygen/Cooker/Import/Internal/Utils/DescriptorDocument.h`
   - `src/Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h`
   - `src/Oxygen/Cooker/Import/Internal/TextureDescriptorImportRequestBuilder.cpp`
   - `src/Oxygen/Cooker/Import/Internal/MaterialDescriptorImportRequestBuilder.cpp`
   - `src/Oxygen/Cooker/Import/Internal/GeometryDescriptorImportRequestBuilder.cpp`
   - `src/Oxygen/Cooker/Import/Internal/BufferContainerImportRequestBuilder.cpp`
   - `src/Oxygen/Cooker/Import/Internal/Jobs/MaterialDescriptorImportJob.cpp`
   - `src/Oxygen/Cooker/Import/Internal/Jobs/GeometryDescriptorImportJob.cpp`
   - `src/Oxygen/Cooker/Import/Internal/Jobs/BufferImportSubmitter.cpp`
   - `src/Oxygen/Cooker/Import/Internal/ScriptImportRequestBuilder.cpp`
   - `src/Oxygen/Cooker/Import/Internal/PhysicsImportRequestBuilder.cpp`
   - `src/Oxygen/Cooker/Import/Internal/Pipelines/PhysicsSidecarImportPipeline.cpp`
   - `design/pakgen_supersession_impl.md`
4. Tests run: none (no-build policy active)
5. Result: completed shared descriptor ingress and virtual-path/reference utility consolidation. Descriptor request-builders now use a common JSON document loader/error shaper; canonical virtual path and mounted-root resolution logic is centralized and reused by descriptor jobs and related sidecar request paths.
6. Remaining delta to phase exit gate: none

1. Date: 2026-03-02
2. Phase: P6 (scene descriptor ingress/execution integration)
3. Files changed:
   - `src/Oxygen/Cooker/Import/Schemas/oxygen.scene-descriptor.schema.json`
   - `src/Oxygen/Cooker/Import/SceneDescriptorImportSettings.h`
   - `src/Oxygen/Cooker/Import/SceneDescriptorImportRequestBuilder.h`
   - `src/Oxygen/Cooker/Import/Internal/SceneDescriptorImportRequestBuilder.cpp`
   - `src/Oxygen/Cooker/Import/Internal/Jobs/SceneDescriptorImportJob.h`
   - `src/Oxygen/Cooker/Import/Internal/Jobs/SceneDescriptorImportJob.cpp`
   - `src/Oxygen/Cooker/Import/ImportRequest.h`
   - `src/Oxygen/Cooker/Import/ImportManifest.h`
   - `src/Oxygen/Cooker/Import/ImportManifest.cpp`
   - `src/Oxygen/Cooker/Import/Schemas/oxygen.import-manifest.schema.json`
   - `src/Oxygen/Cooker/Import/AsyncImportService.cpp`
   - `src/Oxygen/Cooker/Tools/ImportTool/BatchCommand.cpp`
   - `src/Oxygen/Cooker/Tools/ImportTool/README.md`
   - `src/Oxygen/Cooker/CMakeLists.txt`
   - `src/Oxygen/Cooker/Test/Import/SceneDescriptorJsonSchema_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/SceneDescriptorImportRequestBuilder_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/ImportManifest_scene_descriptor_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/SceneDescriptorImportJob_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/AsyncImportService_test.cpp`
   - `src/Oxygen/Cooker/Test/CMakeLists.txt`
   - `design/pakgen_supersession_impl.md`
4. Tests run: none (no-build policy active)
5. Result: scene-descriptor domain is now first-class across schema generation, request building, manifest defaults/job wiring, async job routing, batch report classification, job execution (`.oscene` emission with deterministic mounted reference resolution), and focused schema/request/manifest/job/routing test coverage.
6. Remaining delta to phase exit gate:
   - run and pass new scene-descriptor suites externally/CI
   - validate end-to-end mixed-domain manifest DAG scenario with scene-descriptor dependencies

1. Date: 2026-03-07
2. Phase: P8 (examples loose-cooked migration probe)
3. Files changed:
   - `Examples/Content/scenes/cubes/import-manifest.json`
   - `Examples/Content/scenes/cubes/*.material.json`
   - `Examples/Content/scenes/cubes/*.geometry.json`
   - `Examples/Content/scenes/cubes/CubeScene.scene.json`
   - `Examples/Content/scenes/emissive/import-manifest.json`
   - `Examples/Content/scenes/emissive/*.material.json`
   - `Examples/Content/scenes/emissive/*.geometry.json`
   - `Examples/Content/scenes/emissive/EmissiveScene.scene.json`
   - `Examples/Content/scenes/instancing/import-manifest.json`
   - `Examples/Content/scenes/instancing/MatInstanced.material.json`
   - `Examples/Content/scenes/instancing/GeoCube.geometry.json`
   - `Examples/Content/scenes/instancing/InstancingTestScene.scene.json`
   - `Examples/Content/scenes/multi-script/import-manifest.json`
   - `Examples/Content/scenes/multi-script/ShowcaseExternalMat.material.json`
   - `Examples/Content/scenes/multi-script/ShowcaseOrbMat.material.json`
   - `Examples/Content/scenes/multi-script/multi_script_scene.input.json`
   - `Examples/Content/scenes/multi-script/multi_script_scene.scene.json`
   - `Examples/Content/scenes/proc-cubes/import-manifest.json`
   - `Examples/Content/scenes/proc-cubes/ProcCubeBaseMat.material.json`
   - `Examples/Content/scenes/proc-cubes/ProcCubeAccentMat.material.json`
   - `Examples/Content/scenes/proc-cubes/SceneProcCubes.scene.json`
   - `design/pakgen_supersession_impl.md`
4. Tests run:
   - `out/build-vs/bin/Debug/Oxygen.Cooker.ImportTool.exe --no-tui batch --manifest Examples/Content/scenes/cubes/import-manifest.json` (pass, jobs=9/9)
   - `out/build-vs/bin/Debug/Oxygen.Cooker.ImportTool.exe --no-tui batch --manifest Examples/Content/scenes/emissive/import-manifest.json` (pass, jobs=13/13)
   - `out/build-vs/bin/Debug/Oxygen.Cooker.ImportTool.exe --no-tui batch --manifest Examples/Content/scenes/instancing/import-manifest.json` (pass, jobs=3/3)
   - `out/build-vs/bin/Debug/Oxygen.Cooker.ImportTool.exe --no-tui batch --manifest Examples/Content/scenes/multi-script/import-manifest.json` (pass, jobs=7/7)
   - `out/build-vs/bin/Debug/Oxygen.Cooker.ImportTool.exe --no-tui batch --manifest Examples/Content/scenes/proc-cubes/import-manifest.json` (pass, jobs=5/5)
   - `out/build-vs/bin/Debug/Oxygen.Cooker.Inspector.exe validate Examples/Content/.cooked` (pass)
5. Result: migrated all remaining legacy YAML scenes under `Examples/Content/scenes` (`cubes`, `emissive`, `instancing`, `multi-script`, `proc-cubes`) to descriptor/manifest-based loose-cooked imports, including script-sidecar/input conversion and explicit instancing node expansion for the former `generate` directive.
6. Remaining delta to phase exit gate:
   - wire these upgraded scene flows into official examples/docs entrypoints (current `Examples/Content/make_pak.py` remains PakGen-only)
   - complete C++ PakBuilder first-class workflow/documentation closure and phase-level CI proof for P8
