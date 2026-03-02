# PakGen Supersession Implementation Plan (Execution Locked)

This is the execution tracker for `design/pakgen_supersession.md`.

## 1. Rules of Engagement

1. No shortcut implementations that bypass `ImportJob -> Pipeline`.
2. No hidden scope changes: any spec delta must be reflected first in the design docs.
3. Progress is evidence-based only (files, tests, diagnostics, docs).
4. Domain parity is mandatory: `texture`, `buffer`, `material`, `geometry`, `scene` move as first-class peers.
5. Manual validation must be trimmed to non-schema-enforceable rules only.

## 2. Mandatory Architecture Gates

1. Descriptor domains are independent and explicit:
   - `texture-descriptor`
   - `buffer-descriptor`
   - `material-descriptor`
   - `geometry-descriptor`
   - `scene-descriptor`
2. Scene import path must not implicitly cook geometry/material/texture/buffer.
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
| P1 | pending | 12% | Shared descriptor infrastructure | Common schema validation + diagnostics helpers integrated for descriptor domains |
| P2 | done | 10% | Texture descriptor domain | `texture-descriptor` implemented end-to-end with schema/tests |
| P3 | pending | 8% | Buffer descriptor domain | `buffer-descriptor` implemented end-to-end with schema/tests |
| P4 | done | 14% | Material descriptor domain | `material-descriptor` implemented end-to-end with schema/tests |
| P5 | pending | 16% | Geometry descriptor domain | `geometry-descriptor` implemented end-to-end with schema/tests |
| P6 | pending | 16% | Scene descriptor domain | `scene-descriptor` implemented end-to-end with schema/tests |
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
2. `src/Oxygen/Cooker/Import/Schemas/oxygen.buffer-descriptor.schema.json`
3. `src/Oxygen/Cooker/Import/Schemas/oxygen.material-descriptor.schema.json`
4. `src/Oxygen/Cooker/Import/Schemas/oxygen.geometry-descriptor.schema.json`
5. `src/Oxygen/Cooker/Import/Schemas/oxygen.scene-descriptor.schema.json`

## 5.2 New Import Domain Surfaces (Representative)

1. `src/Oxygen/Cooker/Import/TextureDescriptorImportSettings.h`
2. `src/Oxygen/Cooker/Import/BufferDescriptorImportSettings.h`
3. `src/Oxygen/Cooker/Import/MaterialDescriptorImportSettings.h`
4. `src/Oxygen/Cooker/Import/GeometryDescriptorImportSettings.h`
5. `src/Oxygen/Cooker/Import/SceneDescriptorImportSettings.h`
6. `src/Oxygen/Cooker/Import/*DescriptorImportRequestBuilder.h`
7. `src/Oxygen/Cooker/Import/Internal/*DescriptorImportRequestBuilder.cpp`
8. `src/Oxygen/Cooker/Import/Internal/Jobs/*DescriptorImportJob.h/.cpp`
9. `src/Oxygen/Cooker/Import/Internal/Pipelines/*DescriptorImportPipeline.h/.cpp`

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

1. Standardize descriptor schema validation through shared helper(s):
   - reuse `Import/Internal/Utils/JsonSchemaValidation.h`
   - map schema diagnostics to import diagnostics by domain
2. Add shared descriptor document loader/parsing helpers (UTF-8, JSON parse error shaping, source path/object path conventions).
3. Add shared reference-resolution helpers for mounted/inflight assets/resources.
4. Remove redundant manual validation already enforceable via schemas.

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

## P3: Buffer Descriptor Domain

Tasks:

1. Add schema for buffer descriptor contract.
2. Add request-builder/job/pipeline domain path.
3. Reuse existing buffer cook path (`BufferPipeline`, emitters/index wiring).
4. Add manifest `type: "buffer-descriptor"` integration and defaults.
5. Add tests for valid/invalid descriptors and emitted resource indexing.

Acceptance:

1. Buffer descriptor imports produce deterministic `buffers.table`/`buffers.data` outputs.

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
4. P1, P3, and P5-P10 are `pending`.
5. Computed progress snapshot: `28.0%`.

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
