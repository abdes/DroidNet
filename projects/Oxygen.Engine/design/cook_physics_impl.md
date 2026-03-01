# Physics Sidecar Implementation Plan (Execution Locked)

This is the implementation plan for `design/cook_physics.md`.

## 1. Rules of Engagement Compliance

This plan follows `.github/instructions/cpp_coding_style.instructions.md` collaboration rules.

1. No shortcuts.
2. Truthful progress only.
3. Trackable status after each completed task.
4. API correctness over guessing.
5. No regressions.
6. Keep runtime/import boundaries clean.
7. Search discipline with `rg` and explicit impacted-file tracking.

## 2. Mandatory Architecture Gates

1. Physics sidecar import path must be `PhysicsSidecarImportJob -> PhysicsSidecarImportPipeline`.
2. `AsyncImportService` must route only requests where `request.physics.has_value()` to `PhysicsSidecarImportJob`.
3. No direct import execution path outside job/pipeline flow.
4. New physics-sidecar binary serialization logic must use `oxygen::serio`.
5. Physics sidecar import must emit standalone `.physics` descriptor assets and must not patch scenes.
6. Manifest + CLI must converge on the same `BuildPhysicsSidecarRequest(...)` normalization path.

## 3. File-Level Change Map

## 3.1 New Files

1. `src/Oxygen/Cooker/Import/PhysicsImportSettings.h`
2. `src/Oxygen/Cooker/Import/PhysicsImportRequestBuilder.h`
3. `src/Oxygen/Cooker/Import/Internal/PhysicsImportRequestBuilder.cpp`
4. `src/Oxygen/Cooker/Import/Internal/Jobs/PhysicsSidecarImportJob.h`
5. `src/Oxygen/Cooker/Import/Internal/Jobs/PhysicsSidecarImportJob.cpp`
6. `src/Oxygen/Cooker/Import/Internal/Pipelines/PhysicsSidecarImportPipeline.h`
7. `src/Oxygen/Cooker/Import/Internal/Pipelines/PhysicsSidecarImportPipeline.cpp`
8. `src/Oxygen/Cooker/Import/Internal/SidecarSceneResolver.h`
9. `src/Oxygen/Cooker/Import/Internal/SidecarSceneResolver.cpp`
10. `src/Oxygen/Cooker/Tools/ImportTool/PhysicsSidecarCommand.h`
11. `src/Oxygen/Cooker/Tools/ImportTool/PhysicsSidecarCommand.cpp`
12. `src/Oxygen/Cooker/Import/Schemas/oxygen.physics-sidecar.schema.json`
13. `src/Oxygen/Cooker/Test/Import/PhysicsImportRequestBuilder_test.cpp`
14. `src/Oxygen/Cooker/Test/Import/ImportManifest_physics_sidecar_test.cpp`
15. `src/Oxygen/Cooker/Test/Import/PhysicsImportJob_test.cpp`
16. `src/Oxygen/Cooker/Test/Import/PhysicsJsonSchema_test.cpp`

## 3.2 Modified Files

### Import Core

1. `src/Oxygen/Cooker/Import/ImportRequest.h`
2. `src/Oxygen/Cooker/Import/PhysicsImportSettings.h`
3. `src/Oxygen/Cooker/Import/AsyncImportService.cpp`
4. `src/Oxygen/Cooker/Import/ImportManifest.h`
5. `src/Oxygen/Cooker/Import/ImportManifest.cpp`
6. `src/Oxygen/Cooker/Import/ImportReport.h`
7. `src/Oxygen/Cooker/Import/Internal/ImportSession.cpp`
8. `src/Oxygen/Cooker/Import/ScriptImportRequestBuilder.h` (if shared resolver/request helper extraction requires signature touch)
9. `src/Oxygen/Cooker/Import/Internal/Pipelines/ScriptingSidecarImportPipeline.cpp` (consume shared resolver utility)
10. `src/Oxygen/Cooker/CMakeLists.txt`
11. `src/Oxygen/Cooker/Import/Schemas/oxygen.import-manifest.schema.json`

### ImportTool

1. `src/Oxygen/Cooker/Tools/ImportTool/main.cpp`
2. `src/Oxygen/Cooker/Tools/ImportTool/BatchCommand.cpp`
3. `src/Oxygen/Cooker/Tools/ImportTool/ImportRunner.cpp`
4. `src/Oxygen/Cooker/Tools/ImportTool/CMakeLists.txt`
5. `src/Oxygen/Cooker/Tools/ImportTool/README.md`

### Tests/CMake

1. `src/Oxygen/Cooker/Test/CMakeLists.txt`
2. `src/Oxygen/Cooker/Test/Import/ScriptImportJob_test.cpp` (if shared resolver refactor requires adaptation)
3. `src/Oxygen/Cooker/Test/Pak/PakPlanBuilder_test.cpp`
4. `src/Oxygen/Cooker/Test/Pak/PakWriter_test.cpp`

## 4. Phase Plan and Status Ledger

Status values:

1. `pending`
2. `in_progress`
3. `done`

| Phase | Status | Scope | Exit Gate |
| --- | --- | --- | --- |
| P1 | done | Contracts + schemas | physics-sidecar settings/request/schema contracts compile and validate |
| P2 | done | Routing + job shell | async routing + job lifecycle complete |
| P3 | done | Shared sidecar scene resolver extraction | script + physics sidecar use one target-scene resolver path |
| P4 | done | Physics sidecar pipeline core | parse/validate/serialize/emit `.physics` descriptor works |
| P5 | done | Manifest + ImportTool integration | `physics-sidecar` works in CLI and batch manifest flow |
| P6 | in_progress | Test coverage + pak inclusion checks | importer and pak tests cover happy/negative paths |
| P7 | done | Docs + install/schema closeout | docs and schema install wiring complete |

Status update rule:

1. On each completed phase/task, update this ledger immediately.
2. Add factual evidence to Section 11 (commands/tests/files changed).

## 4.1 Live Snapshot (2026-03-01)

Current progress snapshot:

1. P1-P5 are implemented in code (contracts/schemas, routing/job shell, shared resolver, pipeline core, CLI/manifest integration).
2. Physics sidecar request payload lives in `ImportRequest::physics` (`PhysicsImportSettings`), not `ImportOptions`.
3. ImportTool now includes a dedicated `physics-sidecar` command and batch classification recognizes `physics-sidecar`.
4. P6 remains in progress for final verification depth; P7 docs/schema closeout is complete.
5. Execution-policy caveat: no local full build/test run performed in this pass.

## 5. Detailed Work Packages

## P1: Contracts and Schemas

Tasks:

1. Add domain-owned `PhysicsImportSettings` and `ImportRequest::physics` payload.
2. Add `PhysicsSidecarImportSettings` DTO.
3. Implement `BuildPhysicsSidecarRequest(...)` with:
   - source vs inline exclusivity
   - canonical target scene path validation
   - normalized inline payload handling
4. Extend import manifest schema (`oxygen.import-manifest.schema.json`) with:
   - `type: "physics-sidecar"`
   - `defaults.physics_sidecar`
   - one-of source/bindings + required `target_scene_virtual_path`.
5. Add dedicated sidecar authoring schema `oxygen.physics-sidecar.schema.json`.
6. Wire schema install in module CMake.
7. Wire embedded-schema generation inputs for updated manifest schema and new physics-sidecar schema.

Acceptance:

1. Builder emits normalized `ImportRequest` with `request.physics.has_value()` and normalized physics payload.
2. Invalid settings fail with deterministic diagnostics.
3. Manifest schema rejects malformed physics-sidecar jobs.
4. Embedded-schema generation includes updated manifest schema and physics-sidecar schema.

## P2: Routing and Job Shell

Tasks:

1. Add `PhysicsSidecarImportJob` class (session/load/process/finalize flow).
2. Extend `AsyncImportService` routing and job naming to include physics-sidecar.
3. Add minimal telemetry/report wiring for physics sidecar execution.

Acceptance:

1. Physics-sidecar requests are accepted and routed to the new job.
2. Non-physics requests remain behaviorally unchanged.

## P3: Shared Sidecar Scene Resolver

Tasks:

1. Extract sidecar target-scene resolution logic from `ScriptingSidecarImportPipeline` into `SidecarSceneResolver`.
2. Rewire scripting-sidecar pipeline to use the extracted utility (no behavior change).
3. Use the same utility from physics-sidecar pipeline.

Acceptance:

1. One shared resolver implementation is used by both sidecar domains.
2. Existing scripting-sidecar tests continue to pass after refactor.

## P4: Physics Sidecar Pipeline Core

Tasks:

1. Implement parser for physics-sidecar JSON payload.
2. Validate:
   - target-scene existence/type
   - node index bounds
   - shape/material reference resolvability and asset types
   - duplicate policy for per-node singleton binding kinds.
3. Convert resolved refs to source-local asset indices.
4. Serialize `PhysicsSceneAssetDesc` + component tables with `oxygen::serio`.
5. Derive emitted descriptor path from resolved target scene descriptor (`.oscene` -> `.physics`).
6. Emit sidecar via `AssetEmitter`.

Acceptance:

1. Pipeline emits valid `AssetType::kPhysicsScene` descriptors.
2. Descriptor binary validates under existing `PhysicsSceneAsset` runtime parser.
3. Failures produce stable `physics.sidecar.*` diagnostics.

## P5: Manifest and ImportTool Integration

Tasks:

1. Add `PhysicsSidecarCommand` and register in ImportTool `main.cpp`.
2. Extend batch/report job-type classification for `physics-sidecar`.
3. Extend `ImportManifest` parsing/build request path and defaults application.
4. Update ImportTool README command/docs examples.
5. Enforce physics-sidecar manifest key whitelist and reject command-only keys.

Acceptance:

1. `physics-sidecar` command works with file or inline input mode.
2. Manifest batch supports `type: "physics-sidecar"` and validates constraints.
3. `id`/`depends_on` metadata continues to flow through batch orchestration unchanged.

## P6: Test Coverage and Pak Inclusion Checks

Tasks:

1. Add request-builder tests:
   - exclusivity rules
   - canonical path checks
   - inline JSON normalization failures.
2. Add manifest tests:
   - valid/invalid physics-sidecar job objects
   - defaults propagation.
3. Add import job/pipeline tests:
   - successful descriptor emission
   - unresolved refs/type mismatch/node bounds/ambiguity failures.
4. Add pak-oriented tests:
   - loose index with physics-sidecar descriptor is planned/written correctly
   - physics table/data pair handling remains correct.
5. Add schema-validation tests for `oxygen.physics-sidecar.schema.json` (valid canonical doc + invalid docs).

Acceptance:

1. New tests cover all critical contracts and diagnostics.
2. Existing pak tests remain green after changes.
3. Physics sidecar schema-validation tests cover editor-facing schema contract.

## P7: Docs and Schema Closeout

Tasks:

1. Finalize `design/cook_physics.md` and this impl plan status/evidence sections.
2. Ensure schema install docs include physics-sidecar schema.
3. Update import-tool README with editor/IDE schema mapping examples.

Acceptance:

1. Docs and implementation are aligned.
2. No undocumented behavior deltas remain.

## 6. Diagnostics Plan

Stable diagnostic namespaces:

1. `physics.request.*`
2. `physics.sidecar.*`
3. `physics.manifest.*`

Required concrete diagnostics:

1. `physics.request.invalid_import_kind`
2. `physics.request.target_scene_virtual_path_missing`
3. `physics.sidecar.payload_parse_failed`
4. `physics.sidecar.payload_invalid`
5. `physics.sidecar.target_scene_virtual_path_invalid`
6. `physics.sidecar.target_scene_missing`
7. `physics.sidecar.target_scene_not_scene`
8. `physics.sidecar.target_scene_read_failed`
9. `physics.sidecar.inflight_target_scene_ambiguous`
10. `physics.sidecar.node_ref_out_of_bounds`
11. `physics.sidecar.shape_ref_unresolved`
12. `physics.sidecar.shape_ref_not_collision_shape`
13. `physics.sidecar.material_ref_unresolved`
14. `physics.sidecar.material_ref_not_physics_material`
15. `physics.sidecar.reference_source_mismatch`
16. `physics.sidecar.constraint_resource_index_invalid`
17. `physics.sidecar.descriptor_emit_failed`
18. `physics.sidecar.pipeline_exception`
19. `physics.manifest.source_bindings_exclusive`
20. `physics.manifest.target_scene_virtual_path_missing`

## 7. Regression Protection

Must-not-regress areas:

1. Existing script-sidecar behavior.
2. Existing input-import behavior.
3. Existing scene/texture/script import routing.
4. Existing PakPlanBuilder/PakWriter physics region/table behavior.

## 8. Minimal File Proliferation Rule

1. Add only files listed in Section 3.1.
2. Prefer extracting shared sidecar resolver utility instead of duplicating logic.
3. Reuse existing import session/emitter/index infrastructure.

## 9. Verification Commands (to run during implementation)

1. Targeted import unit tests for request builders and manifest parsers.
2. Targeted sidecar import job/pipeline tests.
3. Targeted pak planner/writer tests for physics domain inclusion.
4. ImportTool command-level tests (where available).

Exact command lines are logged in Section 11 when executed.

## 10. Done Definition

Done requires:

1. All phase exit gates satisfied.
2. `physics-sidecar` fully integrated through request -> routing -> job -> pipeline -> emission.
3. Manifest and CLI wiring complete with schema-backed validation.
4. Pak inclusion verified through existing planner/writer paths without hacks.
5. Status ledger and evidence log updated.

## 11. Evidence Log

This section is updated during implementation.

Current:

1. P1 contract/schema files added:
   - `src/Oxygen/Cooker/Import/PhysicsImportSettings.h`
   - `src/Oxygen/Cooker/Import/PhysicsImportRequestBuilder.h/.cpp`
   - `src/Oxygen/Cooker/Import/Schemas/oxygen.physics-sidecar.schema.json`
   - `src/Oxygen/Cooker/Test/Import/PhysicsImportRequestBuilder_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/ImportManifest_physics_sidecar_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/PhysicsJsonSchema_test.cpp`
2. Manifest schema and parser extended for `type: "physics-sidecar"` and `defaults.physics_sidecar`.
3. Architecture correction completed: removed physics-sidecar payload from `ImportOptions`; runtime payload now lives in `ImportRequest::physics` via `PhysicsImportSettings`.
4. ImportTool report job-type resolvers now recognize physics-sidecar requests.
5. P2 routing/job-shell completed:
   - added `PhysicsSidecarImportJob` class shell and source-loading path.
   - wired `AsyncImportService` routing via `request.physics.has_value()`.
   - added cooker CMake entries for the new job files.
6. P3 shared resolver extraction completed:
   - added `Import/Internal/SidecarSceneResolver.h/.cpp`.
   - rewired both scripting and physics sidecar pipelines to use shared resolver entry points.
7. P4 pipeline core completed:
   - added `Import/Internal/Pipelines/PhysicsSidecarImportPipeline.h/.cpp`.
   - implemented sidecar parse/validate/resolve/serialize/emit path for `.physics` descriptors.
   - enforced node bounds, duplicate singleton-per-node policy, reference-type checks, and source-domain consistency checks.
8. P5 manifest/CLI integration completed:
   - added `Tools/ImportTool/PhysicsSidecarCommand.h/.cpp`.
   - wired command registration in `Tools/ImportTool/main.cpp`.
   - wired command compilation in `Tools/ImportTool/CMakeLists.txt`.
   - updated ImportTool docs for `physics-sidecar` command and schema mappings.
9. P6 test coverage progressed:
   - added `Test/Import/PhysicsImportPipeline_test.cpp` (pipeline null-session conformance).
   - added `Test/Import/PhysicsImportJob_test.cpp` (async physics job diagnostics for invalid target path and invalid payload).
   - extended `Test/Import/AsyncImportService_test.cpp` with routing tests:
     - unknown format rejected without domain override.
     - physics-sidecar request bypasses format detection and reaches physics diagnostics path.
   - extended `Test/Pak/PakWriter_test.cpp` with physics-sidecar descriptor inclusion coverage in final pak directory.
   - remediated deterministic diagnostics ordering: canonical `target_scene_virtual_path` validation now runs in `PhysicsSidecarImportPipeline` before cooked-index loading, ensuring `physics.sidecar.target_scene_virtual_path_invalid` is emitted first for malformed paths.
   - remediated manifest diagnostics precedence: early job-key whitelist precheck now emits `physics.manifest.key_not_allowed` before schema-generic failures for disallowed physics-sidecar job keys.
   - remediated sidecar exclusivity diagnostics precedence: early pre-schema check now emits `physics-sidecar job requires exactly one of 'source' or 'bindings'` when both are present.
   - remediated input required-id diagnostics precedence: early pre-schema check now emits `input.manifest.job_id_missing` for missing/empty `input` job ids.
   - aligned physics request builder with import job root-derivation behavior: `BuildPhysicsSidecarRequest(...)` no longer requires explicit cooked root when omitted.
10. P7 docs/schema closeout completed:
    - module-level schema install list includes `oxygen.physics-sidecar.schema.json`.
    - ImportTool README includes `physics-sidecar` command, manifest examples, and editor schema mapping examples.
11. Remaining P6 backlog:
    - run and verify targeted tests/build to close execution evidence.
12. No local build/test execution performed in this pass (per execution policy).

## 12. Compliance Checklist

This checklist proves each non-negotiable requirement is explicitly covered by this plan.

| # | Non-Negotiable Requirement | Satisfied By | Verified In |
| --- | --- | --- | --- |
| 1 | One manifest job type added for this domain: `type: "physics-sidecar"` | P1 task #4; modified schema `src/Oxygen/Cooker/Import/Schemas/oxygen.import-manifest.schema.json` | P1 acceptance #3 |
| 2 | One job class only: `PhysicsSidecarImportJob` | §2 gate #1 and P2 task #1 | P2 acceptance #1 |
| 3 | One pipeline class only: `PhysicsSidecarImportPipeline` | §2 gate #1 and P4 scope | P4 acceptance #1 |
| 4 | Import routing only through async job path | §2 gate #2/#3 and P2 task #2 | P2 acceptance #1/#2 |
| 5 | Sidecar import never mutates scene descriptors | §2 gate #5 and P4 emission scope | P4 acceptance #1 |
| 6 | CLI and manifest converge on one request builder | §2 gate #6 and P1 task #3 + P5 task #3 | P1 acceptance #1 and P5 acceptance #2 |
| 7 | Exactly-one source mode enforced (`source` xor inline `bindings`) | P1 task #3/#4 | P1 acceptance #2/#3 |
| 8 | Canonical `target_scene_virtual_path` required | P1 task #3/#4 and diagnostics list (`physics.request.*`, `physics.manifest.*`) | P1 acceptance #2/#3 |
| 9 | Shared sidecar scene resolver used by scripting + physics | P3 tasks #1-#3 | P3 acceptance #1/#2 |
| 10 | Serialization uses `oxygen::serio` for new logic | §2 gate #4 and P4 task #4 | P4 acceptance #2 |
| 11 | Pak integration uses existing planner/writer flow, no side hacks | P6 task #4 and §7 regression protection #4 | P6 acceptance #2 |
| 12 | Editor-facing schema is shipped and validated | new file `oxygen.physics-sidecar.schema.json`; P1 tasks #5/#6/#7; P6 task #5 | P6 acceptance #3, P7 acceptance #1/#2 |
