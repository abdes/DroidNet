# Comprehensive Physics Cooking Implementation Plan

This plan executes `design/cook_physics.md`.
It replaces the earlier sidecar-only implementation scope.

## 1. Scope Lock (Task Start Gate)

Goal:

1. Deliver full physics cooking support across resources, materials, shapes, and sidecars through descriptor domains and manifest DAG orchestration.

In scope:

1. New descriptor domains and schemas.
2. Request/job/pipeline integration.
3. Resolver/emitter internals.
4. Layout normalization under `Physics/...` for materials/shapes/resources, with sidecars co-located to target scenes.
5. Validation/diagnostics hardening.
6. Test and parity closure.

Out of scope:

1. Runtime simulation behavior changes.
2. FBX/glTF automatic physics extraction.

Exit gate:

1. All phases below are `done` with file+test evidence.

## 2. Rules of Engagement

1. No shortcut paths outside `ImportJob -> Pipeline`.
2. No false completion claims.
3. Design/doc updates precede any scope shifts.
4. Schema-first validation is mandatory.
5. Manual validation only for non-schema-enforceable checks.

## 3. Phase Ledger

Status values:

1. `pending`
2. `in_progress`
3. `done`
4. `blocked`

| Phase | Status | Depends On | Scope | Exit Gate |
| --- | --- | --- | --- | --- |
| P0 | done | none | Scope correction and plan reset | sidecar-only plan replaced by comprehensive plan/spec |
| P11 | done | P0 | Contract finalization vs canonical physics headers | design/schema/implementation contracts are aligned and validation evidence is captured |
| P1 | done | P11 | Physics layout foundation | materials/shapes/resources under `Physics/...`; sidecars co-located with target scenes |
| P2 | done | P1 | Physics resource descriptor domain | `physics-resource-descriptor` end-to-end with `.opres` |
| P3 | done | P1 | Physics material descriptor domain | `physics-material-descriptor` end-to-end (`.opmat`) |
| P4 | done | P2, P3 | Collision shape descriptor domain | `collision-shape-descriptor` end-to-end (`.ocshape`) |
| P5 | done | P2, P3, P4 | Physics sidecar v2 upgrade | full binding-family support with virtual refs |
| P6 | done | P2, P3, P4, P5 | Manifest + DAG integration | job types/defaults/key checks/dependency collection |
| P7 | done | P2, P3, P4, P5, P6 | Schema embed/install integration | all physics schemas generated and installed |
| P8 | done | P2, P3, P4, P5, P6 | Diagnostics hardening | stable diagnostic set and precedence behavior |
| P9 | in_progress | P1, P2, P3, P4, P5, P6, P7, P8 | Test matrix closure | domain + integration + pak tests complete |
| P10 | pending | P9 | Parity and docs closeout | PakGen parity evidence and documentation finalization |

## 3.1 Strict Execution Order

This is the mandatory execution order for closure:

1. P0 [done]
2. P11 [done]
3. P1 [done]
4. P2 + P3 (parallel allowed) [done]
5. P4 [done]
6. P5 [done]
7. P6 [done]
8. P7 [done]
9. P8
10. P9
11. P10

## 4. Detailed Phase Work

## P0: Scope Correction and Plan Reset

Tasks:

1. replace sidecar-only design with comprehensive physics design.
2. replace sidecar-only implementation tracker with phased comprehensive tracker.

Evidence:

1. `design/cook_physics.md`
2. `design/cook_physics_impl.md`

## P11: Contract Finalization Gate

This phase is a pre-domain gate and must be executed immediately after P0.

Tasks:

1. Verify canonical physics contracts between:
   - `design/pak_physics.md`
   - `src/Oxygen/Data/PakFormat_physics.h`
2. Update:
   - `design/cook_physics.md` contracts
   - schema expectations
   - test expectations
   to match canonical format contracts.
3. Record finalization evidence in this log.

Acceptance:

1. Physics design docs present one canonical final contract vs `PakFormat_physics.h`.
2. Any schema/test/implementation updates performed in this gate (for already-existing physics import surfaces) are aligned to the finalized contracts.

Current execution note:

1. Canonical contract updates have been applied to the physics design docs.
2. Joint world-attachment authoring is supported in schema and cooker implementation.
3. External validation has been provided by the user (`all green`) and recorded in the Evidence Log.

## P1: Physics Layout Foundation

Tasks:

1. Update loose-cooked layout contract for dedicated physics root:
   - `Physics/Materials`
   - `Physics/Shapes`
   - `Physics/Resources`
2. Ensure emitters and relpath helpers use the new contract.
3. Ensure sidecar output location is exactly co-located with target scene (`<scene_dir>/<scene_stem>.opscene`).
4. Ensure index registration and virtual-path helpers remain deterministic.

Acceptance:

1. No physics descriptor is emitted under generic `Descriptors/*`.
2. `physics.table` + `physics.data` live under `Physics/Resources`.

## P2: Physics Resource Descriptor Domain

Tasks:

1. Add settings/request builder/job/pipeline for `physics-resource-descriptor`.
2. Add schema: `oxygen.physics-resource-descriptor.schema.json`.
3. Implement payload ingestion to `physics.table`/`physics.data`.
4. Implement `.opres` sidecar emission and parser.
5. Implement virtual-path resolution contract for `.opres`.
6. Integrate manifest and ImportTool command.
7. Align dedup/collision behavior with universal importer policy (`ImportOptions::dedup_collision_policy`) and enforce canonical `.opres` path uniqueness per deduped physics resource index.

Acceptance:

1. Canonical import emits table/data + `.opres`.
2. Dedupe and collision policy behavior is deterministic and diagnosed.

## P3: Physics Material Descriptor Domain

Tasks:

1. Add settings/request builder/job/pipeline for `physics-material-descriptor`.
2. Add schema: `oxygen.physics-material-descriptor.schema.json`.
3. Map descriptor to `PhysicsMaterialAssetDesc`.
4. Emit deterministic `.opmat` under `Physics/Materials`.
5. Integrate manifest and ImportTool command.

Acceptance:

1. Canonical descriptor imports successfully and emits `.opmat`.
2. Schema/manual validation failures produce `physics.material.*` diagnostics.

## P4: Collision Shape Descriptor Domain

Tasks:

1. Add settings/request builder/job/pipeline for `collision-shape-descriptor`.
2. Add schema: `oxygen.collision-shape-descriptor.schema.json`.
3. Implement shape-type discriminated mapping to `CollisionShapeAssetDesc`.
4. Resolve `material_ref` -> `.opmat`.
5. Resolve `payload_ref` -> `.opres` -> `PhysicsResourceDesc` index/type.
6. Emit deterministic `.ocshape` under `Physics/Shapes`.
7. Integrate manifest and ImportTool command.

Acceptance:

1. Primitive and payload-backed shapes import correctly.
2. Ref/type mismatch failures are deterministic and diagnosed.

## P5: Physics Sidecar v2 Upgrade

Tasks:

1. Expand `oxygen.physics-sidecar.schema.json` for full binding families.
2. Replace numeric resource-index authoring with virtual-path refs (`constraint_ref`).
3. Update sidecar pipeline to resolve refs:
   - `shape_ref` -> `.ocshape`
   - `material_ref` -> `.opmat`
   - `constraint_ref` -> `.opres`
4. Validate node bounds and singleton binding rules.
5. Emit `.opscene` exactly beside target `.oscene`.

Acceptance:

1. Full binding-family payload imports successfully.
2. No implicit dependency cooking occurs.
3. No version number attached to types or constants
4. No legacy or backward compat shims left. Oxygen has a strict "live at the edge" policy.

## P6: Manifest and DAG Integration

Tasks:

1. Extend import-manifest schema/types/defaults for all physics domains.
2. Enforce strict per-type key whitelists.
3. Implement dependency collector rules for physics refs.
4. Merge explicit + inferred dependencies deterministically.
5. Enforce consistent defaults/override precedence:
   - manifest defaults < job settings < CLI explicit overrides
6. Add cycle/ambiguity diagnostics.

Acceptance:

1. Multi-domain physics manifests execute deterministically.
2. Dependency errors are explicit and stable.

## P7: Schema Embed/Install Integration

Tasks:

1. Wire all new physics schemas into build-time embed generation.
2. Wire module-owned install entries for all physics schemas.
3. Verify install naming consistency.

Acceptance:

1. All physics schemas are available as generated embedded headers.
2. Installed schema set is complete and consistent.

## P8: Diagnostics Hardening

Tasks:

1. Stabilize domain diagnostic namespaces:
   - `physics.resource.*`
   - `physics.material.*`
   - `physics.shape.*`
   - `physics.sidecar.*`
   - `physics.manifest.*`
2. Ensure deterministic precedence where multiple failures are possible.
3. Limit redundant noise while keeping author-helpful messages.

Acceptance:

1. Diagnostics are stable, bounded, and actionable.

## P9: Test Matrix Closure

Tasks:

1. Schema tests for all physics schemas.
2. Request builder tests for all physics domains.
3. Job/pipeline tests for success and canonical failures.
4. Manifest orchestration tests (defaults/overrides/deps).
5. End-to-end integration tests for scene+physics workflows.
6. Pak planner/writer inclusion tests for physics outputs.

Acceptance:

1. Test coverage demonstrates contract correctness and orchestration behavior.

## P10: Parity and Docs Closeout

Tasks:

1. Validate representative parity scenarios against legacy PakGen physics content coverage.
2. Update docs/examples for full physics descriptor workflows.
3. Mark phases complete only with explicit evidence.

Acceptance:

1. No remaining PakGen-only physics gap in scoped feature set.
2. Docs/spec/plan/status are consistent.

## 5. Risks and Mitigations

1. Cross-mount ref ambiguity:
   - Mitigation: deterministic precedence + ambiguity diagnostic hard fail.
2. Resource dedupe collisions:
   - Mitigation: content-hash + format identity, virtual-path collision checks.
3. Layout migration regressions:
   - Mitigation: focused layout tests and index path assertions.
4. Sidecar dependency race conditions:
   - Mitigation: dependency collector + explicit DAG ordering validation.

## 6. Evidence Log

## Phase Completion Audit (2026-03-02)

1. P0 `done`:
   - evidence: `design/cook_physics.md`, `design/cook_physics_impl.md`.
2. P11 `done`:
   - evidence:
     - docs: `design/pak_physics.md`, `design/cook_physics.md`.
     - implementation/schema/test: `src/Oxygen/Cooker/Import/Internal/Pipelines/PhysicsSidecarImportPipeline.cpp`, `src/Oxygen/Cooker/Import/Schemas/oxygen.physics-sidecar.schema.json`, `src/Oxygen/Cooker/Test/Import/PhysicsJsonSchema_test.cpp`.
     - canonical header correction: `src/Oxygen/Data/PakFormat_physics.h` (`PhysicsComponentTableDesc` comment corrected to 20 bytes to match `static_assert(sizeof(... ) == 20)`).
   - closure evidence:
     - user-reported external validation: `all green`.
3. P1 `done`:
   - implementation evidence:
     - `src/Oxygen/Cooker/Loose/LooseCookedLayout.h` now models dedicated physics layout roots/subdirs:
       - scene sidecars emitted beside target scenes (`<scene_dir>/<scene_stem>.opscene`)
       - `Physics/Resources` for `physics.table` and `physics.data`
       - `Physics/Materials` and `Physics/Shapes` directory mapping hooks
     - `DescriptorDirFor(AssetType::kPhysicsScene)` follows target-scene co-location rules.
     - `PhysicsTableRelPath()` and `PhysicsDataRelPath()` now resolve under `Physics/Resources`.
     - `src/Oxygen/Cooker/Test/Loose/LooseCookedLayout_test.cpp` expectations updated to canonical `Physics/...` paths.
   - closure evidence:
     - physics/scene extension usage in import pipelines and scene-descriptor
       dependency inference now resolves through
       `LooseCookedLayout` extension constants (no local hardcoded extension
       strings in those paths).
     - `LooseCookedLayout` now defines explicit physics descriptor extension
       constants and relpath/virtual-path helpers for:
       `*.opmat`, `*.ocshape`, `*.opres`.
     - `src/Oxygen/Cooker/Test/Loose/LooseCookedLayout_test.cpp` expanded with
       assertions for the new physics descriptor file-name/relpath/virtual-path
       helpers.
4. P2 `done`:
   - implementation evidence:
     - schema: `src/Oxygen/Cooker/Import/Schemas/oxygen.physics-resource-descriptor.schema.json`.
     - request ingress: `src/Oxygen/Cooker/Import/PhysicsResourceDescriptorImportSettings.h`,
       `src/Oxygen/Cooker/Import/PhysicsResourceDescriptorImportRequestBuilder.h`,
       `src/Oxygen/Cooker/Import/Internal/PhysicsResourceDescriptorImportRequestBuilder.cpp`.
     - execution: `src/Oxygen/Cooker/Import/Internal/Jobs/PhysicsResourceDescriptorImportJob.h/.cpp`,
       `src/Oxygen/Cooker/Import/Internal/Pipelines/PhysicsResourceImportPipeline.h/.cpp`.
     - emit/sidecar: `src/Oxygen/Cooker/Import/Internal/Emitters/PhysicsResourceEmitter.h/.cpp`,
       `src/Oxygen/Cooker/Import/Internal/Utils/PhysicsResourceDescriptorSidecar.h/.cpp`,
       `ResourceDescriptorEmitter` `.opres` support.
     - session/registry wiring: `ImportSession`, `ResourceTableAggregator`, `ResourceTableRegistry`.
   - validation evidence:
     - tests added:
       - `src/Oxygen/Cooker/Test/Import/PhysicsResourceDescriptorJsonSchema_test.cpp`
       - `src/Oxygen/Cooker/Test/Import/PhysicsResourceDescriptorImportRequestBuilder_test.cpp`
       - `src/Oxygen/Cooker/Test/Import/PhysicsResourceDescriptorImportJob_test.cpp`
       - `src/Oxygen/Cooker/Test/Import/ImportManifest_physics_resource_descriptor_test.cpp`
   - closure evidence:
     - user-reported external validation: `all green`.
5. P3 `done`:
   - implementation evidence:
     - schema: `src/Oxygen/Cooker/Import/Schemas/oxygen.physics-material-descriptor.schema.json`.
     - request ingress: `src/Oxygen/Cooker/Import/PhysicsMaterialDescriptorImportSettings.h`,
       `src/Oxygen/Cooker/Import/PhysicsMaterialDescriptorImportRequestBuilder.h`,
       `src/Oxygen/Cooker/Import/Internal/PhysicsMaterialDescriptorImportRequestBuilder.cpp`.
     - execution: `src/Oxygen/Cooker/Import/Internal/Jobs/PhysicsMaterialDescriptorImportJob.h/.cpp`,
       `src/Oxygen/Cooker/Import/Internal/Pipelines/PhysicsMaterialImportPipeline.h/.cpp`.
     - output mapping to `PhysicsMaterialAssetDesc` and `.opmat` emission via `AssetEmitter`.
   - validation evidence:
     - tests added:
       - `src/Oxygen/Cooker/Test/Import/PhysicsMaterialDescriptorJsonSchema_test.cpp`
       - `src/Oxygen/Cooker/Test/Import/PhysicsMaterialDescriptorImportRequestBuilder_test.cpp`
       - `src/Oxygen/Cooker/Test/Import/PhysicsMaterialDescriptorImportJob_test.cpp`
       - `src/Oxygen/Cooker/Test/Import/ImportManifest_physics_material_descriptor_test.cpp`
   - closure evidence:
     - user-reported external validation: `all green`.
6. P4 `done`:
   - implementation evidence:
     - schema:
       - `src/Oxygen/Cooker/Import/Schemas/oxygen.collision-shape-descriptor.schema.json`
     - request ingress:
       - `src/Oxygen/Cooker/Import/CollisionShapeDescriptorImportSettings.h`
       - `src/Oxygen/Cooker/Import/CollisionShapeDescriptorImportRequestBuilder.h`
       - `src/Oxygen/Cooker/Import/Internal/CollisionShapeDescriptorImportRequestBuilder.cpp`
     - runtime/job/pipeline:
       - `src/Oxygen/Cooker/Import/Internal/Jobs/CollisionShapeDescriptorImportJob.h/.cpp`
       - `src/Oxygen/Cooker/Import/Internal/Pipelines/CollisionShapeImportPipeline.h/.cpp`
     - manifest/tool routing:
       - `src/Oxygen/Cooker/Import/ImportManifest.h/.cpp`
       - `src/Oxygen/Cooker/Import/Schemas/oxygen.import-manifest.schema.json`
       - `src/Oxygen/Cooker/Import/AsyncImportService.cpp`
       - `src/Oxygen/Cooker/Tools/ImportTool/BatchCommand.cpp`
     - tests:
       - `src/Oxygen/Cooker/Test/Import/CollisionShapeDescriptorJsonSchema_test.cpp`
       - `src/Oxygen/Cooker/Test/Import/CollisionShapeDescriptorImportRequestBuilder_test.cpp`
       - `src/Oxygen/Cooker/Test/Import/CollisionShapeDescriptorImportJob_test.cpp`
       - `src/Oxygen/Cooker/Test/Import/ImportManifest_collision_shape_descriptor_test.cpp`
       - `src/Oxygen/Cooker/Test/CMakeLists.txt` target
         `Oxygen.Cooker.AsyncImportCollisionShapeDescriptor.Tests`
   - closure evidence:
     - user-reported external validation: `all green`.
7. P5 `done`:
   - implementation evidence:
     - schema migrated to virtual-ref contract for all binding families:
       - `src/Oxygen/Cooker/Import/Schemas/oxygen.physics-sidecar.schema.json`
       - fields now use `shape_ref`, `material_ref`, `constraint_ref`
         (legacy `*_virtual_path` and numeric `constraint_resource_index`
         authoring removed from schema contract).
     - sidecar pipeline migrated to virtual-ref resolution:
       - `src/Oxygen/Cooker/Import/Internal/Pipelines/PhysicsSidecarImportPipeline.cpp`
       - `shape_ref` -> collision-shape asset index
       - `material_ref` -> physics-material asset index
       - `constraint_ref` -> `.opres` sidecar parse ->
         `PhysicsResourceDesc` index with `jolt_constraint_binary` format
         enforcement.
     - node-bound and singleton invariants preserved:
       - singleton-per-node validation remains for
         `rigid_bodies`/`characters`/`soft_bodies`/`vehicles`/`aggregates`
       - node bounds validation enforced across all binding families.
     - `.opscene` emission remains co-located with target `.oscene`:
       - sidecar relpath derived by replacing target scene extension only.
   - validation evidence:
     - schema tests updated:
       - `src/Oxygen/Cooker/Test/Import/PhysicsJsonSchema_test.cpp`
         now validates all seven binding families and rejects legacy field names.
     - request/manifest fixture payloads updated to virtual-ref fields:
       - `src/Oxygen/Cooker/Test/Import/PhysicsImportRequestBuilder_test.cpp`
       - `src/Oxygen/Cooker/Test/Import/ImportManifest_physics_sidecar_test.cpp`
     - content fixture updated:
       - `Examples/Content/full-import/showcase_scene.physics-sidecar.json`
   - closure evidence:
     - no new versioned type names/constants introduced in the sidecar-v2 path.
     - no legacy-sidecar field shims retained in schema/pipeline parsing.
8. P6 `done`:
   - implementation evidence:
     - `src/Oxygen/Cooker/Tools/ImportTool/BatchCommand.cpp`
       now adds physics-domain DAG inference based on canonical virtual refs:
       - producer indexing from manifest jobs for:
         - `physics-resource-descriptor` (`.opres`)
         - `physics-material-descriptor` (`.opmat`)
         - `collision-shape-descriptor` (`.ocshape`)
         - scene producers (`scene-descriptor`, `fbx`, `gltf`) for sidecar target scene refs
       - consumer ref extraction for:
         - `collision-shape-descriptor` (`material_ref`, `payload_ref`)
         - `physics-sidecar` (`target_scene_virtual_path`, `shape_ref`, `material_ref`, `constraint_ref`)
       - deterministic merge of explicit `depends_on` + inferred producer edges
         for scheduling (deduped and order-stable).
       - diagnostics:
         - `physics.manifest.dependency_inference_failed`
         - `physics.manifest.dependency_unresolved`
         - `physics.manifest.dependency_ambiguous`
         - `physics.manifest.dependency_missing_target`
         - `physics.manifest.dependency_cycle`
       - tests added:
         - `src/Oxygen/Cooker/Test/Import/BatchCommand_physics_dag_test.cpp`
           covering:
           - unresolved inferred physics refs
           - ambiguous inferred producer refs
           - physics dependency cycle diagnostic
           - non-physics cycle diagnostic regression guard
         - `src/Oxygen/Cooker/Test/CMakeLists.txt` target:
           `Oxygen.Cooker.ImportToolBatchDag.Tests`
       - precedence alignment in batch request preparation:
         - CLI cooked-root override now takes highest priority over
           manifest defaults and per-job settings.
   - closure evidence:
     - mixed multi-domain physics manifest import validated externally by user
       (`All Green`) against `Examples/Content/full-import/import-manifest.json`.
     - runtime scene load + deferred physics hydration validated externally by
       user (`All Green`) in RenderScene after DAG/sidecar integration
       remediations.
     - deterministic dependency diagnostics remain covered by
       `BatchCommand_physics_dag_test.cpp` cases for unresolved, ambiguous, and
       cycle paths.
9. P7 `done`:
   - evidence:
     - build-time embed wiring (`oxygen_embed_json_schemas`) includes:
       - `oxygen.physics-sidecar.schema.json`
       - `oxygen.physics-resource-descriptor.schema.json`
       - `oxygen.physics-material-descriptor.schema.json`
       - `oxygen.collision-shape-descriptor.schema.json`
       in `src/Oxygen/Cooker/CMakeLists.txt`.
     - module-owned install wiring (`install(FILES ... DESTINATION .../schemas)`)
       includes the same four physics schemas in
       `src/Oxygen/Cooker/CMakeLists.txt`.
   - closure evidence:
     - no root-project install script dependency for these schema entries;
       ownership remains in module CMake as required.
10. P8 `done`:
    - evidence:
      - `ImportSession` diagnostic sink now de-duplicates exact repeats and
        emits logging from a stable local copy under lock:
        - `src/Oxygen/Cooker/Import/Internal/ImportSession.h`
        - `src/Oxygen/Cooker/Import/Internal/ImportSession.cpp`
      - `BatchCommand` physics DAG diagnostics now suppress duplicate unresolved,
        ambiguous, and missing-target emissions per job/ref key:
        - `src/Oxygen/Cooker/Tools/ImportTool/BatchCommand.cpp`
      - test coverage added/extended:
        - `src/Oxygen/Cooker/Test/Import/ImportSession_test.cpp`
          - `ImportSessionTest.AddDiagnosticDuplicateSuppressed`
        - `src/Oxygen/Cooker/Test/Import/BatchCommand_physics_dag_test.cpp`
          - duplicate unresolved ref diagnostics are bounded
          - duplicate ambiguous ref diagnostics are bounded
          - duplicate missing explicit dependency diagnostics are bounded
          - physics duplicate-job-id namespace diagnostic uses
            `physics.manifest.job_id_duplicate`
    - closure evidence:
      - user-reported validation: `All Green` for P8 test updates.
11. P9 `in_progress`:
     - evidence:
      - sidecar + physics resource/material schema/request/job/manifest tests
        exist in tree.
      - collision-shape schema/request/job/manifest tests now exist in tree.
     - gap evidence:
      - scene+physics integration matrix is still pending.
12. P10 `pending`:
    - gap evidence:
      - no recorded full parity evidence for the complete four-domain physics authoring model.

## P0

1. Replaced sidecar-only physics design/plan with comprehensive scope:
   - `design/cook_physics.md`
   - `design/cook_physics_impl.md`

Build/test execution in this pass:

1. Not run (design/docs pass only).

## P11 (done)

1. Canonical physics contract updates applied in docs:
   - `design/pak_physics.md` updated to align with `PakFormat_physics.h`.
   - `design/cook_physics.md` updated with final contract content.
2. Joint world-attachment implementation remediation applied:
   - `src/Oxygen/Cooker/Import/Internal/Pipelines/PhysicsSidecarImportPipeline.cpp`
     supports world-attached joint parsing/validation (`node_index_b` as `null`/`"world"` -> sentinel).
   - `src/Oxygen/Cooker/Import/Schemas/oxygen.physics-sidecar.schema.json`
     accepts `node_index_b` as index, `null`, or `"world"`.
   - `src/Oxygen/Cooker/Test/Import/PhysicsJsonSchema_test.cpp`
     adds coverage for joint world-attachment forms.
3. Canonical ABI comment consistency remediation:
   - `src/Oxygen/Data/PakFormat_physics.h`
     updated `PhysicsComponentTableDesc` size comment to 20 bytes, matching `static_assert(sizeof(PhysicsComponentTableDesc) == 20)`.
4. Validation status:
   - build/test execution is intentionally not recorded by the agent in this flow.
   - closure achieved via user-run targeted validation confirmation (`all green`).
5. Compliance correction:
   - previous agent-side test execution claims were removed.
   - P11 is closed using user-run validation evidence only.

## P1 (done)

1. Layout foundation remediation applied:
   - `src/Oxygen/Cooker/Loose/LooseCookedLayout.h`
     introduces dedicated physics root/subdir model (`physics_dir`,
      `physics_materials_subdir`, `physics_shapes_subdir`,
      `physics_resources_subdir`), with physics sidecars co-located to target scenes.
   - `DescriptorDirFor(AssetType::kPhysicsScene)` now follows
     target-scene co-location (`<scene_dir>/<scene_stem>.opscene`).
   - `DescriptorDirFor(AssetType::kPhysicsMaterial)` now resolves to
     `Physics/Materials`.
   - `DescriptorDirFor(AssetType::kCollisionShape)` now resolves to
     `Physics/Shapes`.
   - `PhysicsTableRelPath()` and `PhysicsDataRelPath()` now resolve to
     `Physics/Resources/physics.table` and `Physics/Resources/physics.data`.
2. Test expectation updates:
   - `src/Oxygen/Cooker/Test/Loose/LooseCookedLayout_test.cpp` updated to
     canonical `Physics/...` path assertions.
3. Additional hardening applied for extension/path consistency:
   - `src/Oxygen/Cooker/Loose/LooseCookedLayout.h`
     now provides explicit helpers for:
     - `PhysicsMaterialDescriptorFileName/RelPath/VirtualPath`
     - `CollisionShapeDescriptorFileName/RelPath/VirtualPath`
     - `PhysicsResourceDescriptorFileName/RelPath/VirtualPath`
   - `src/Oxygen/Cooker/Import/Internal/Pipelines/PhysicsSidecarImportPipeline.cpp`
     now uses layout extension constants when pairing `.oscene` -> `.opscene`.
   - `src/Oxygen/Cooker/Import/Internal/Jobs/SceneDescriptorImportJob.cpp`
     now uses layout extension constants for descriptor-type inference.
   - `src/Oxygen/Cooker/Test/Loose/LooseCookedLayout_test.cpp`
     now covers these new helpers.

## P2 (done)

1. Physics resource descriptor domain implementation landed:
   - schema:
     - `src/Oxygen/Cooker/Import/Schemas/oxygen.physics-resource-descriptor.schema.json`
   - request ingestion:
     - `src/Oxygen/Cooker/Import/PhysicsResourceDescriptorImportSettings.h`
     - `src/Oxygen/Cooker/Import/PhysicsResourceDescriptorImportRequestBuilder.h`
     - `src/Oxygen/Cooker/Import/Internal/PhysicsResourceDescriptorImportRequestBuilder.cpp`
   - runtime/job/pipeline:
     - `src/Oxygen/Cooker/Import/Internal/Jobs/PhysicsResourceDescriptorImportJob.h/.cpp`
     - `src/Oxygen/Cooker/Import/Internal/Pipelines/PhysicsResourceImportPipeline.h/.cpp`
   - emit + sidecar:
     - `src/Oxygen/Cooker/Import/Internal/Emitters/PhysicsResourceEmitter.h/.cpp`
     - `src/Oxygen/Cooker/Import/Internal/Utils/PhysicsResourceDescriptorSidecar.h/.cpp`
     - `.opres` emission support in
       `src/Oxygen/Cooker/Import/Internal/Emitters/ResourceDescriptorEmitter.h/.cpp`
     - canonical `.opres` dedupe-path enforcement in
       `src/Oxygen/Cooker/Import/Internal/ResourceTableRegistry.h/.cpp` and
       `src/Oxygen/Cooker/Import/Internal/Jobs/PhysicsResourceDescriptorImportJob.cpp`
       (cross-run scan + in-process canonical claim by resource index)
   - session/registry integration:
     - `src/Oxygen/Cooker/Import/Internal/ImportSession.h/.cpp`
     - `src/Oxygen/Cooker/Import/Internal/ResourceTableAggregator.h`
     - `src/Oxygen/Cooker/Import/Internal/ResourceTableRegistry.h/.cpp`
2. Manifest/tool routing integrated:
   - `src/Oxygen/Cooker/Import/ImportManifest.h/.cpp`
   - `src/Oxygen/Cooker/Import/Schemas/oxygen.import-manifest.schema.json`
   - `src/Oxygen/Cooker/Import/AsyncImportService.cpp`
   - `src/Oxygen/Cooker/Tools/ImportTool/BatchCommand.cpp`
3. Test coverage added:
   - `src/Oxygen/Cooker/Test/Import/PhysicsResourceDescriptorJsonSchema_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/PhysicsResourceDescriptorImportRequestBuilder_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/PhysicsResourceDescriptorImportJob_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/ImportManifest_physics_resource_descriptor_test.cpp`
4. Closure evidence:
   - user-reported validation: `All Green`.

## P3 (done)

1. Physics material descriptor domain implementation landed:
   - schema:
     - `src/Oxygen/Cooker/Import/Schemas/oxygen.physics-material-descriptor.schema.json`
   - request ingestion:
     - `src/Oxygen/Cooker/Import/PhysicsMaterialDescriptorImportSettings.h`
     - `src/Oxygen/Cooker/Import/PhysicsMaterialDescriptorImportRequestBuilder.h`
     - `src/Oxygen/Cooker/Import/Internal/PhysicsMaterialDescriptorImportRequestBuilder.cpp`
   - runtime/job/pipeline:
     - `src/Oxygen/Cooker/Import/Internal/Jobs/PhysicsMaterialDescriptorImportJob.h/.cpp`
     - `src/Oxygen/Cooker/Import/Internal/Pipelines/PhysicsMaterialImportPipeline.h/.cpp`
   - emission:
     - `.opmat` asset emission through `AssetEmitter` path in
       `PhysicsMaterialDescriptorImportJob`.
   - schema-first cleanup:
     - redundant post-schema enum-guard checks removed from
       `PhysicsMaterialDescriptorImportJob` (combine-mode parsing is now
       schema-trusting and single-path).
2. Manifest/tool routing integrated:
   - `src/Oxygen/Cooker/Import/ImportManifest.h/.cpp`
   - `src/Oxygen/Cooker/Import/Schemas/oxygen.import-manifest.schema.json`
   - `src/Oxygen/Cooker/Import/AsyncImportService.cpp`
   - `src/Oxygen/Cooker/Tools/ImportTool/BatchCommand.cpp`
3. Test coverage added:
   - `src/Oxygen/Cooker/Test/Import/PhysicsMaterialDescriptorJsonSchema_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/PhysicsMaterialDescriptorImportRequestBuilder_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/PhysicsMaterialDescriptorImportJob_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/ImportManifest_physics_material_descriptor_test.cpp`
4. Closure evidence:
   - user-reported validation: `All Green`.

## P4 (done)

1. Collision shape descriptor domain implementation landed:
   - schema:
     - `src/Oxygen/Cooker/Import/Schemas/oxygen.collision-shape-descriptor.schema.json`
   - request ingestion:
     - `src/Oxygen/Cooker/Import/CollisionShapeDescriptorImportSettings.h`
     - `src/Oxygen/Cooker/Import/CollisionShapeDescriptorImportRequestBuilder.h`
     - `src/Oxygen/Cooker/Import/Internal/CollisionShapeDescriptorImportRequestBuilder.cpp`
   - runtime/job/pipeline:
     - `src/Oxygen/Cooker/Import/Internal/Jobs/CollisionShapeDescriptorImportJob.h/.cpp`
     - `src/Oxygen/Cooker/Import/Internal/Pipelines/CollisionShapeImportPipeline.h/.cpp`
   - resolver/ref integration:
     - `material_ref` resolution to `/.cooked/Physics/Materials/*.opmat`
       in the target source domain.
     - payload-backed `payload_ref` resolution to
       `/.cooked/Physics/Resources/*.opres` with sidecar parse and
       `jolt_shape_binary` format enforcement.
   - emission:
     - deterministic `.ocshape` emission under `Physics/Shapes`.
2. Manifest/tool routing integrated:
   - `src/Oxygen/Cooker/Import/ImportManifest.h/.cpp`
   - `src/Oxygen/Cooker/Import/Schemas/oxygen.import-manifest.schema.json`
   - `src/Oxygen/Cooker/Import/AsyncImportService.cpp`
   - `src/Oxygen/Cooker/Tools/ImportTool/BatchCommand.cpp`
3. Test coverage added:
   - `src/Oxygen/Cooker/Test/Import/CollisionShapeDescriptorJsonSchema_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/CollisionShapeDescriptorImportRequestBuilder_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/CollisionShapeDescriptorImportJob_test.cpp`
   - `src/Oxygen/Cooker/Test/Import/ImportManifest_collision_shape_descriptor_test.cpp`
   - `src/Oxygen/Cooker/Test/CMakeLists.txt` target:
     `Oxygen.Cooker.AsyncImportCollisionShapeDescriptor.Tests`
4. Closure evidence:
   - user-reported validation: `All Green`.

## P5 (done)

1. Physics sidecar v2 contract migration landed:
   - schema:
     - `src/Oxygen/Cooker/Import/Schemas/oxygen.physics-sidecar.schema.json`
       now models all seven binding families using:
       - `shape_ref`
       - `material_ref`
       - `constraint_ref`
   - legacy authoring fields removed from schema:
       - `shape_virtual_path`
       - `material_virtual_path`
       - `constraint_resource_index`
2. Sidecar pipeline ref resolution migration landed:
   - `src/Oxygen/Cooker/Import/Internal/Pipelines/PhysicsSidecarImportPipeline.cpp`
   - rigid/collider/character resolution:
     - `shape_ref` -> `AssetType::kCollisionShape`
     - `material_ref` -> `AssetType::kPhysicsMaterial`
   - joint/vehicle resolution:
     - `constraint_ref` -> `.opres` parse ->
       `ResourceIndexT constraint_resource_index`
     - `PhysicsResourceFormat::kJoltConstraintBinary` enforced.
3. Invariants and emission behavior:
   - node bounds and singleton-per-node rules remain enforced.
   - sidecar emission remains co-located with target scene:
     - `<scene_dir>/<scene_stem>.opscene`.
4. Test/fixture updates:
   - `src/Oxygen/Cooker/Test/Import/PhysicsJsonSchema_test.cpp`
     - canonical payload now exercises all seven families.
     - explicit rejection test for legacy field names added.
   - `src/Oxygen/Cooker/Test/Import/PhysicsImportRequestBuilder_test.cpp`
     updated to virtual-ref payload fields.
   - `src/Oxygen/Cooker/Test/Import/ImportManifest_physics_sidecar_test.cpp`
     updated inline bindings to virtual-ref payload fields.
   - `Examples/Content/full-import/showcase_scene.physics-sidecar.json`
     migrated to virtual-ref fields.
5. Closure evidence:
   - no new versioned sidecar-v2 type names or constants introduced.
   - no legacy-sidecar parsing path retained for removed field names.

## P6 (done)

1. Manifest + DAG integration implementation landed:
   - `src/Oxygen/Cooker/Tools/ImportTool/BatchCommand.cpp`
     - producer indexing covers:
       - `physics-resource-descriptor` (`.opres`)
       - `physics-material-descriptor` (`.opmat`)
       - `collision-shape-descriptor` (`.ocshape`)
       - scene producers (`scene-descriptor`, `fbx`, `gltf`)
     - consumer extraction covers:
       - collision-shape refs: `material_ref`, `payload_ref`
       - sidecar refs: `target_scene_virtual_path`, `shape_ref`,
         `material_ref`, `constraint_ref`
     - explicit + inferred dependency merge is deterministic and deduped.
     - CLI override precedence is enforced:
       - manifest defaults < job settings < CLI overrides.
2. Dependency diagnostics are explicit and stable:
   - `physics.manifest.dependency_inference_failed`
   - `physics.manifest.dependency_unresolved`
   - `physics.manifest.dependency_ambiguous`
   - `physics.manifest.dependency_missing_target`
   - `physics.manifest.dependency_cycle`
3. Test coverage:
   - `src/Oxygen/Cooker/Test/Import/BatchCommand_physics_dag_test.cpp`
     - unresolved inferred refs
     - ambiguous inferred producers
     - physics dependency cycle
     - non-physics cycle regression guard
4. Runtime hardening completed during phase validation:
   - same-cooked-root sidecar producer fence integration in BatchCommand
     to stabilize sidecar index resolution ordering.
   - Jolt adapter guard/normalization for rigid-body and character rotations:
     - `src/Oxygen/Physics/Jolt/Converters.h`
     - `src/Oxygen/Physics/Jolt/JoltBodies.cpp`
     - `src/Oxygen/Physics/Jolt/JoltCharacters.cpp`
   - scene-loader rigid-body failure diagnostics enriched:
     - `Examples/DemoShell/Services/SceneLoaderService.cpp`
5. Closure evidence:
   - user-reported validation: `All Green` for
     `Examples/Content/full-import/import-manifest.json`.
   - user-reported validation: `All Green` for RenderScene physics hydration
     and floor/ball interaction after descriptor remediations.

## P7 (done)

1. Schema embed integration verified:
   - `src/Oxygen/Cooker/CMakeLists.txt` `oxygen_embed_json_schemas(...)`
     includes:
     - `oxygen.physics-sidecar.schema.json`
     - `oxygen.physics-resource-descriptor.schema.json`
     - `oxygen.physics-material-descriptor.schema.json`
     - `oxygen.collision-shape-descriptor.schema.json`
2. Schema install integration verified (module-owned):
   - `src/Oxygen/Cooker/CMakeLists.txt` `_oxygen_cooker_schema_files` and
     `install(FILES ... DESTINATION ${OXYGEN_INSTALL_DATA}/schemas ...)`
     include the same four physics schemas.
3. Closure evidence:
   - P7 scope has no remaining code gap; embed + install wiring for physics
     schemas is complete and centralized in module CMake.

## P8 (done)

1. Diagnostics noise hardening landed:
   - session-level duplicate suppression:
     - `src/Oxygen/Cooker/Import/Internal/ImportSession.h`
     - `src/Oxygen/Cooker/Import/Internal/ImportSession.cpp`
   - batch DAG duplicate suppression for physics diagnostics:
     - unresolved refs (`physics.manifest.dependency_unresolved`)
     - ambiguous refs (`physics.manifest.dependency_ambiguous`)
     - missing explicit targets (`physics.manifest.dependency_missing_target`)
     in:
     - `src/Oxygen/Cooker/Tools/ImportTool/BatchCommand.cpp`
2. Diagnostic namespace stabilization refinement:
   - duplicate job-id diagnostic now uses physics namespace for physics batches:
     - `physics.manifest.job_id_duplicate`
     and retains legacy namespace for non-physics-only batches:
     - `input.manifest.job_id_duplicate`
3. Test coverage added/expanded:
   - `src/Oxygen/Cooker/Test/Import/ImportSession_test.cpp`
     - `ImportSessionTest.AddDiagnosticDuplicateSuppressed`
   - `src/Oxygen/Cooker/Test/Import/BatchCommand_physics_dag_test.cpp`
     - `PhysicsSidecarDuplicateUnresolvedRefsEmitSingleDependencyUnresolvedDiagnostic`
     - `PhysicsSidecarDuplicateAmbiguousRefsEmitSingleDependencyAmbiguousDiagnostic`
     - `DuplicateMissingDependencyIdEmitsSingleMissingTargetDiagnostic`
     - `PhysicsDuplicateJobIdsUsePhysicsManifestDiagnosticNamespace`
4. Validation status:
   - build/test execution was not run by the agent (per user constraints).
   - user-reported validation: `All Green`.

## P9 (in_progress)

1. Entry criteria:
   - P1..P8 closed.
2. Active objective:
   - close remaining test-matrix deltas across:
     - schema tests for all physics schemas
     - request builder tests for all physics domains
     - job/pipeline success + canonical failures
     - manifest orchestration tests (defaults/overrides/deps)
     - scene+physics integration and pak inclusion assertions
3. Validation mode:
   - build/test execution is user-run only in this flow.
