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
| P1 | in_progress | P11 | Physics layout foundation | materials/shapes/resources under `Physics/...`; sidecars co-located with target scenes |
| P2 | pending | P1 | Physics resource descriptor domain | `physics-resource-descriptor` end-to-end with `.opres` |
| P3 | pending | P1 | Physics material descriptor domain | `physics-material-descriptor` end-to-end (`.opmat`) |
| P4 | pending | P2, P3 | Collision shape descriptor domain | `collision-shape-descriptor` end-to-end (`.ocshape`) |
| P5 | blocked | P2, P3, P4 | Physics sidecar v2 upgrade | full binding-family support with virtual refs |
| P6 | blocked | P2, P3, P4, P5 | Manifest + DAG integration | job types/defaults/key checks/dependency collection |
| P7 | blocked | P2, P3, P4, P5, P6 | Schema embed/install integration | all physics schemas generated and installed |
| P8 | blocked | P2, P3, P4, P5, P6 | Diagnostics hardening | stable diagnostic set and precedence behavior |
| P9 | blocked | P1, P2, P3, P4, P5, P6, P7, P8 | Test matrix closure | domain + integration + pak tests complete |
| P10 | pending | P9 | Parity and docs closeout | PakGen parity evidence and documentation finalization |

## 3.1 Strict Execution Order

This is the mandatory execution order for closure:

1. P0
2. P11
3. P1
4. P2 + P3 (parallel allowed)
5. P4
6. P5
7. P6
8. P7 + P8 (parallel allowed)
9. P9
10. P10

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
3. P1 `in_progress`:
   - implementation evidence:
     - `src/Oxygen/Cooker/Loose/LooseCookedLayout.h` now models dedicated physics layout roots/subdirs:
       - scene sidecars emitted beside target scenes (`<scene_dir>/<scene_stem>.opscene`)
       - `Physics/Resources` for `physics.table` and `physics.data`
       - `Physics/Materials` and `Physics/Shapes` directory mapping hooks
     - `DescriptorDirFor(AssetType::kPhysicsScene)` follows target-scene co-location rules.
     - `PhysicsTableRelPath()` and `PhysicsDataRelPath()` now resolve under `Physics/Resources`.
     - `src/Oxygen/Cooker/Test/Loose/LooseCookedLayout_test.cpp` expectations updated to canonical `Physics/...` paths.
   - remaining:
     - user-run validation required to close P1 (`no build/test execution by agent` rule).
4. P2 `pending`:
   - gap evidence:
     - no `physics-resource-descriptor` schema exists under `src/Oxygen/Cooker/Import/Schemas`.
     - no resource descriptor request-builder/job/pipeline files under `src/Oxygen/Cooker/Import/Internal`.
5. P3 `pending`:
   - gap evidence:
     - no `oxygen.physics-material-descriptor.schema.json` exists.
     - no dedicated physics-material descriptor request-builder/job/pipeline exists.
6. P4 `pending`:
   - gap evidence:
     - no `oxygen.collision-shape-descriptor.schema.json` exists.
     - no collision-shape descriptor request-builder/job/pipeline exists.
7. P5 `blocked`:
   - evidence:
     - sidecar request-builder/job/pipeline/tests exist (`PhysicsImportRequestBuilder`, `PhysicsSidecarImportJob`, `PhysicsSidecarImportPipeline`, related tests).
   - gap evidence:
     - sidecar schema still uses legacy fields (`shape_virtual_path`, `material_virtual_path`, `constraint_resource_index`) instead of full virtual-ref contract.
8. P6 `blocked`:
   - evidence:
     - manifest includes `physics-sidecar` type and key-whitelist checks.
   - gap evidence:
     - manifest does not yet include `physics-resource-descriptor`, `physics-material-descriptor`, `collision-shape-descriptor` job types/defaults.
9. P7 `blocked`:
   - evidence:
     - CMake embed/install includes `oxygen.physics-sidecar.schema.json`.
   - gap evidence:
     - embed/install wiring missing the three new physics descriptor schemas required by P2-P4.
10. P8 `blocked`:
    - evidence:
      - strong `physics.sidecar.*` and `physics.manifest.*` diagnostics are present.
    - gap evidence:
      - `physics.resource.*`, `physics.material.*`, `physics.shape.*` namespaces are not present because domains are not implemented yet.
11. P9 `blocked`:
    - evidence:
      - sidecar schema/request/job/pipeline/manifest tests exist.
    - gap evidence:
      - no test coverage yet for physics resource/material/shape domains or their DAG integration.
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

## P1 (in progress)

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
3. Remaining closure requirement:
   - user-run validation required to mark P1 `done` (`no build/test execution
     by agent` rule).
