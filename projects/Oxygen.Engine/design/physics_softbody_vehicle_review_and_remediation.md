# Oxygen Physics Review: Soft Bodies + Vehicles (Jolt) and Remediation Plan

Date: 2026-03-03
Audience: Junior engine/gameplay developer implementing fixes under senior review.

## 1. Scope and Goal

This review covered:

- Backend-agnostic physics APIs under `src/Oxygen/Physics`
- Jolt backend implementation under `src/Oxygen/Physics/Jolt`
- Scene/physics hydration bridge in `Examples/DemoShell/Services/SceneLoaderService.cpp`
- Scripting API/bindings under `src/Oxygen/Scripting/Bindings/Packs/Physics`
- Physics sidecar/pak formats in `src/Oxygen/Data/PakFormat_physics.h`
- Cooker + format docs: `design/cook_physics.md`, `design/pak_physics.md`
- Demo content under `Examples/Content/scenes/physics_domains`

Goal:

- Explain what is good, what is bad, and what is currently breaking trust.
- Provide a prioritized implementation plan that will make vehicles and soft bodies robust, correct, and script-ergonomic.

## 2. Executive Summary

### Good

- Domain separation is mostly clean: handles (`WorldId`, `BodyId`, `AggregateId`) and API boundaries are sane.
- Jolt vehicle uses real `VehicleConstraint` / `WheeledVehicleController` primitives.
- Soft-body API surface exists and is integrated through the same aggregate contracts.
- Sidecar/cooker validation catches many structural authoring errors early.

### Bad

- Vehicle authoring contract is not truly authoritative at runtime (resource blobs validated but ignored).
- Vehicle topology is inferred by scene hierarchy heuristics, not explicit authored wheel data.
- Scripting ergonomics are poor for scene-driven aggregates (demo script uses handle-resolution hacks).
- Control-input docs and runtime contract are not fully aligned.

### Disastrous

- Rigid-body/collider hydration seeds body pose from **local** node transform while backend expects **world** pose.
  This breaks all child-node rigid bodies and is the strongest root-cause candidate for vehicle non-movement.
- Aggregate mapping rules conflict with vehicle usage; loader works around this with ancestor fallback and can skip mapping entirely.
- Demo is not a valid end-to-end proof:
  - soft body binding table is empty;
  - vehicle points to a hinge joint resource;
  - script uses fallback force/hard-coded node scans to compensate.

## 3. Authoritative Transform-Space Contract (No Hacks)

This is the missing policy that must be treated as non-negotiable.

### 3.1 Facts from engine phase ordering

1. `kSceneMutation` executes before `kTransformPropagation` (see `AsyncEngine` phase order).
2. Scene world transforms are finalized in `kTransformPropagation` (via `Scene::Update()` in `Renderer::OnTransformPropagation`).
3. Therefore, reading live world transforms during scene hydration or other pre-propagation work is not phase-safe.

### 3.2 Contract

1. **Physics descriptor pose contract**: `BodyDesc.initial_position` / `initial_rotation` are always world-space at the physics API boundary.
2. **Default pre-propagation rule** (`kGameplay`, `kSceneMutation`, load/hydration paths): do not call `GetWorldPosition()/GetWorldRotation()` unless a hydration transform barrier has been established.
3. **Hydration exception (explicitly allowed)**: during hydration only, scene hydration coordinator calls `ResolveHydrationTransforms` (internally one `Scene::Update()`; repeated calls in same hydration window are idempotent no-ops) to establish world-transform readiness.
4. **After barrier**: hydration code may consume `GetWorld*` values as authoritative for seeding world-space physics descriptors.
5. **Post-propagation rule** (`>= kTransformPropagation`): world-transform reads are allowed for runtime queries/telemetry.
6. **Guardrail**: add phase-aware checks so pre-propagation world queries in hydration paths fail fast unless the hydration barrier token is active.
7. **One policy only**: no fallback mixing of “try world, else local.” That hides phase bugs and creates nondeterminism.

### 3.3 Implementation shape (engine-grade)

1. Add a hydration-only transform barrier helper named `ResolveHydrationTransforms` that:
   - validates phase/context,
   - calls `Scene::Update()` exactly once,
   - is owned/invoked only by the scene hydration coordinator (not arbitrary scripts/modules),
   - serializes per-scene invocation so parallel `kSceneMutation` handlers cannot race it,
   - exposes a short-lived readiness token scoped to the hydration call stack.
2. Use world-space reads everywhere sidecar hydration seeds physics descriptors once barrier token is active (rigid, collider, character, soft body, vehicle wheel anchors).
3. Add guard assertions so hydration world reads without a readiness token fail fast, and invalidate token use after hydration exits.
4. Keep a deterministic local-hierarchy resolver as fallback utility for tooling/tests, but not as runtime mixed fallback.
5. Add parity test:
   - hydration barrier world pose, vs
   - subsequent `kTransformPropagation` world pose,
   requiring near-equality for nested hierarchies.

## 4. Evidence Highlights (Primary)

- `SceneLoaderService.cpp`:
  - Vehicle constraint resources are validated for format, then ignored (`HydrateVehicleBindings`).
  - Wheel list is discovered via `CollectDescendantRigidBodyNodeIndices(...)`.
  - Vehicle aggregate mapping climbs ancestors to avoid node mapping conflicts and may skip mapping.
  - Rigid body/collider `initial_position` and `initial_rotation` are read from local transform.
- `JoltBodies.cpp`:
  - `BodyCreationSettings` uses `desc.initial_position`/`initial_rotation` as world-space values.
- `AsyncEngine.cpp` + `Renderer.cpp`:
  - `kSceneMutation` runs before `kTransformPropagation`.
  - `Scene::Update()` that resolves world transforms runs in `Renderer::OnTransformPropagation`.
- `PhysicsModule.cpp`:
  - `RegisterNodeAggregateMapping` rejects nodes already mapped to rigid body/character.
- `physics_domains.physics-sidecar.json`:
  - `soft_bodies` is empty.
  - vehicle `constraint_ref` points to hinge resource (`park_hinge_joint_a.opres`).
- `physics_domains_controller.lua`:
  - multi-path vehicle lookup and scene scan fallback.
  - extra fallback force for movement.
- `PhysicsVehicleBindings.cpp` vs `Scripting .../design.md`:
  - control-input contract has drift between implementation and docs.
- `JoltWorld.cpp` + `Converters.h` + `JoltBodies.cpp`:
  - object layers are effectively static vs moving only; authored collision layer/mask are not applied to body creation/filtering.
- `JoltSoftBodies.cpp`:
  - procedural cube generation path only (`sCreateCube`).
  - `anchor_body_id` returns `kNotImplemented`.

## 5. Remediation Plan (Implementation Backlog)

Implement in order. Do not skip a lower index item.

---

### P0.0 Establish and enforce transform-readiness contract (new blocker)

Problem:

- Current hydration logic mixes local/world reads without a phase-safe policy.

Files:

- `src/Oxygen/PhysicsModule/PhaseContracts.md`
- `Examples/DemoShell/Services/SceneLoaderService.cpp`
- `Examples/DemoShell/Services/SceneLoaderService.h` (or new helper file)
- `src/Oxygen/PhysicsModule/Test/` or dedicated service tests

Steps:

1. Document the contract from Section 3 in runtime-facing phase docs.
2. Implement `ResolveHydrationTransforms` as a hydration-coordinator API only (SceneLoader-side usage), never as a general module/script utility.
3. Enforce one-shot-per-scene behavior during hydration with explicit re-entry policy: repeated calls for the same scene in the same hydration window are idempotent no-ops returning the same readiness state.
4. Route all sidecar hydration pose seeding through world-space reads after the barrier.
5. Add explicit diagnostics/asserts for forbidden pre-propagation world reads without hydration barrier token.
6. Add parity test vs post-propagation world cache for nested nodes, plus a re-entry/concurrency safety test for the barrier.

Done when:

1. Hydration has an explicit one-shot world-transform readiness barrier.
2. Barrier ownership, lifetime, and re-entry semantics are documented and enforced.
3. There is one canonical path for pre-propagation physics seeding (barrier then world-read).
4. Tests prove deterministic parity with post-propagation scene world transforms.

---

### P0.1 Fix body hydration world/local transform bug (blocker)

Problem:

- Child rigid bodies are spawned at local-space positions while Jolt expects world-space body pose.

Files:

- `Examples/DemoShell/Services/SceneLoaderService.cpp`
- `src/Oxygen/PhysicsModule/ScenePhysics.cpp` (optional centralization helper)
- Tests to add in `src/Oxygen/PhysicsModule/Test/` and/or DemoShell tests

Steps:

1. Replace ad-hoc local/world fallback with P0.0 barrier flow; seed world-space `desc.initial_position` / `desc.initial_rotation` from world transforms after barrier.
2. Keep character path behavior consistent (already world-seeded through ScenePhysics helper).
3. Add regression test:
   - parent node translated away from origin;
   - child rigid body node with local offset;
   - assert created body world position equals parent+local world result.

Done when:

1. Nested rigid bodies spawn at correct world pose.
2. Vehicle wheel offset computation uses physically correct relative positions.
3. New regression test fails on old code and passes with fix.

---

### P0.2 Remove vehicle aggregate mapping hack and enforce clean ownership contract

Problem:

- Vehicle root node often cannot be mapped due rigid-body conflict; loader uses ancestor fallback and may skip mapping.

Files:

- `src/Oxygen/PhysicsModule/PhysicsModule.cpp`
- `src/Oxygen/Physics/AggregateMappingModel.md`
- `Examples/DemoShell/Services/SceneLoaderService.cpp`
- `src/Oxygen/PhysicsModule/Test/PhysicsModule_sync_test.cpp`

Steps:

1. Allow explicitly-supported dual mapping for vehicle root nodes (body + aggregate) according to contract.
2. Delete ancestor-climb fallback in loader; map vehicle aggregate to chassis node deterministically.
3. Add explicit test: chassis node with rigid body + vehicle aggregate mapping succeeds and is retrievable.

Done when:

1. `physics.vehicle.get_exact(chassis_node)` resolves directly.
2. Demo script no longer needs node scan fallback.
3. Mapping skip warnings disappear for valid authored vehicles.

---

### P0.3 Make constraint resources authoritative (no fake validation-only contract)

Problem:

- Vehicle/joint resource blobs are treated as required, but runtime does not materialize from payload.

Files:

- `Examples/DemoShell/Services/SceneLoaderService.cpp`
- `src/Oxygen/Physics/Jolt/JoltVehicles.cpp` (add settings ingestion path)
- `src/Oxygen/Physics/Jolt/JoltJoints.cpp`
- `src/Oxygen/Data/PakFormat_physics.h`
- `src/Oxygen/Core/Meta/Physics/PakPhysics.inc`
- `design/pak_physics.md`, `design/cook_physics.md`

Steps:

1. Lock policy to **resource-authoritative runtime**: vehicle/joint blobs must be deserialized and applied; no optional bypass path.
2. Make payload type validation unambiguous:
   - joints accept only constraint payload format;
   - vehicles accept only `kJoltVehicleConstraintBinary` payload format (new explicit `PhysicsResourceFormat` value).
3. Remove runtime behavior where payload validity is checked but default settings are used anyway.
4. Update cooker/schema/docs so authoring contract exactly matches runtime behavior.
5. Add negative tests: wrong payload family for vehicle/joint fails deterministically with explicit diagnostics.

Done when:

1. Sidecar resource fields for vehicle/joint are mandatory and truly consumed.
2. Wrong payload format is rejected before backend creation.
3. No “validated but using defaults” behavior remains.

---

### P0.4 Replace vehicle wheel topology heuristic with explicit authored topology

Problem:

- Wheel selection by descendant rigid-body scan is fragile and non-ergonomic.

Files:

- `src/Oxygen/Data/PakFormat_physics.h`
- `src/Oxygen/Core/Meta/Physics/PakPhysics.inc`
- `src/Oxygen/Cooker/Import/Internal/Pipelines/PhysicsSidecarImportPipeline.cpp`
- `Examples/DemoShell/Services/SceneLoaderService.cpp`
- `design/cook_physics.md`, `design/pak_physics.md`
- `Examples/Content/scenes/physics_domains/physics_domains.physics-sidecar.json`

Steps:

1. Extend vehicle binding schema with explicit wheel entries (not bare indices), including:
   - `node_index`,
   - `axle_index` + `side` (`left`/`right`) fields,
   - deterministic order semantics consumed by runtime exactly as authored.
2. Validate in cooker:
   - wheel nodes exist;
   - wheel nodes map to rigid bodies;
   - wheels are distinct from chassis;
   - duplicate wheel nodes/roles are rejected;
   - each vehicle declares at least 2 wheel entries.
3. Define ABI path for new wheel metadata using existing reserved bytes in `VehicleBindingRecord`:
   - add `wheel_table_offset` + `wheel_count` in reserved space while preserving record size and static asserts,
   - add a compact wheel-entry component table (`kVehicleWheelBinding`) with entries: vehicle node, wheel node, axle index, side,
   - require `wheel_count > 0`; no descendant-scan fallback.
4. Hydration must consume authored wheel list directly, never infer from subtree.

Done when:

1. Vehicle setup is deterministic from data.
2. Wheel role mapping is explicit and stable across content edits.
3. Scene hierarchy changes no longer silently change wheel topology.

---

### P0.5 Repair demo to be a truthful end-to-end sample

Problem:

- Demo claims all domains, but soft body is absent and vehicle resource is a hinge placeholder.

Files:

- `Examples/Content/scenes/physics_domains/physics_domains.physics-sidecar.json`
- `Examples/Content/scenes/physics_domains/import-manifest.json`
- `Examples/Content/scenes/physics_domains/README.md`
- `Examples/Content/scenes/physics_domains/physics_domains_controller.lua`

Steps:

1. Add actual `soft_bodies` binding entry for `SoftBody` node.
2. Author and reference a real vehicle resource that matches P0.3 runtime-consumed payload contract.
3. Remove script fallback hacks:
   - no candidate path scan;
   - no fallback force injection;
   - resolve vehicle handle through `physics.vehicle.get_exact(chassis_node)`.
4. Update README to reflect reality only.

Done when:

1. Input and programmatic control move the vehicle without script hacks.
2. Soft-body object exists in physics and updates visibly.
3. Demo README matches runtime behavior.

---

### P0.6 Fix scripting API/doc drift and stabilize control schema

Problem:

- Vehicle control-input contract is not yet documented as a strict single-source schema.

Files:

- `src/Oxygen/Scripting/Bindings/Packs/Physics/PhysicsVehicleBindings.cpp`
- `src/Oxygen/Scripting/Bindings/Packs/Physics/design.md`
- `src/Oxygen/Scripting/Test/Bindings_physics_vehicle_test.cpp`
- `src/Oxygen/PhysicsModule/PhaseContracts.md`

Steps:

1. Set canonical field to `forward`.
2. Define full control-input contract (field names, units, and ranges), including explicit clamp/validation behavior:
   - `forward` in `[-1, 1]`,
   - `steering` in `[-1, 1]`,
   - `brake` in `[0, 1]`,
   - `hand_brake` as bool or `[0, 1]` normalized scalar.
3. Define phase-latching semantics consistent with engine order (`kFixedSimulation` before `kGameplay`):
   - writes before `kFixedSimulation` affect the current fixed step,
   - writes in/after `kGameplay` apply to the next fixed step.
4. Add tests for range clamping, unknown-field rejection, and phase-latching behavior.
5. Update docs to match implementation.

Done when:

1. Script authors cannot silently fail due naming mismatch.
2. Control inputs have deterministic range and frame-phase behavior.
3. Documentation and runtime behavior are aligned.

---

### P1.1 Honor authored collision layers/masks in runtime

Problem:

- Body/query descriptors carry layer/mask, but Jolt integration mostly ignores them.

Files:

- `src/Oxygen/Physics/Jolt/Converters.h`
- `src/Oxygen/Physics/Jolt/JoltWorld.cpp`
- `src/Oxygen/Physics/Jolt/JoltBodies.cpp`
- `src/Oxygen/Physics/Jolt/JoltQueries.cpp`

Steps:

1. Introduce object-layer mapping from authored `collision_layer`.
2. Apply mask filtering in broadphase/object filters and query collectors.
3. Keep default static/moving fallback only when no authored filter exists.

Done when:

1. Changing sidecar layer/mask changes collision/query behavior predictably.
2. Query collision_mask fields are functional.

---

### P1.2 Add authored soft-body payload path (not procedural-only)

Problem:

- `kJoltSoftBodySharedSettingsBinary` exists in format, but runtime path is missing.

Files:

- `src/Oxygen/Data/PakFormat_physics.h`
- `src/Oxygen/Cooker/Import/Internal/Pipelines/PhysicsSidecarImportPipeline.cpp`
- `Examples/DemoShell/Services/SceneLoaderService.cpp`
- `src/Oxygen/Physics/Jolt/JoltSoftBodies.cpp`
- `design/cook_physics.md`, `design/pak_physics.md`

Steps:

1. Add explicit soft-body payload reference in sidecar binding data (single canonical location; no implicit alternate source).
2. Validate resource format is `kJoltSoftBodySharedSettingsBinary`.
3. Use reserved bytes in `SoftBodyBindingRecord` to store `settings_resource_index` while preserving record size/static asserts; document this explicitly in pak/cook specs.
4. Deserialize into Jolt shared settings during soft-body creation.
5. Keep procedural cube only as explicit fallback/prototype mode behind an explicit non-shipping flag.

Done when:

1. Authored soft-body topology/material settings can be cooked and loaded.
2. Soft-body runtime path matches declared format capabilities.
3. Shipping content does not silently fall back to procedural cube when authored payload is missing/invalid.

---

### P1.3 Improve script ergonomics for scene-authored aggregates

Problem:

- Scene-authored aggregate lookup remained inconsistent across domains and
  encouraged ad-hoc traversal logic in scripts.

Files:

- `src/Oxygen/Scripting/Bindings/Packs/Physics/PhysicsVehicleBindings.cpp`
- `src/Oxygen/Scripting/Bindings/Packs/Physics/PhysicsSoftBodyBindings.cpp`
- `src/Oxygen/Scripting/Bindings/Packs/Physics/design.md`
- `src/Oxygen/Scripting/Test/Bindings_physics_vehicle_test.cpp`
- `src/Oxygen/Scripting/Test/Bindings_physics_soft_body_test.cpp`
- `src/Oxygen/Scripting/Test/Bindings_physics_surface_test.cpp`

Steps:

1. Provide explicit lookup APIs with non-ambiguous behavior:
   - strict exact lookup (`get_exact(node)`) with no traversal;
   - opt-in ancestor traversal (`find_in_ancestors(node)`) as a distinct call.
2. Apply the same explicit lookup pair to both vehicle and soft-body APIs;
   remove implicit/ambiguous `get(node)` lookup surface.
3. Update scripting docs/tests to the explicit lookup contract.

Done when:

1. Typical gameplay scripts can use vehicle/soft body APIs without scene graph scanning.
2. Lookup APIs do not hide mapping problems via implicit traversal.

---

### P2.1 Add end-to-end acceptance tests for hydration + control behavior

Problem:

- Existing tests are mostly API/contract-level and do not catch current scene-hydration failures.

Files:

- DemoShell/service tests (or equivalent integration harness)
- `src/Oxygen/PhysicsModule/Test/` + `src/Oxygen/Scripting/Test/`

Steps:

1. Add sidecar-driven integration tests:
   - nested rigid body world-pose correctness;
   - vehicle control input leads to measurable chassis movement;
   - vehicle control written in `kGameplay` affects next fixed step (not current);
   - vehicle aggregate lookup by chassis node;
   - soft-body sidecar binding creates valid aggregate.
2. Add a “contract drift” test for docs/schema/runtime field compatibility.

Done when:

1. The current demo failure mode is caught automatically in CI.

## 6. Implementation Order (Strict)

1. P0.0
2. P0.1
3. P0.2
4. P0.3
5. P0.4
6. P0.5
7. P0.6
8. P1.1
9. P1.2
10. P1.3
11. P2.1

## 7. Validation Gate (No False Completion)

Do not mark this effort complete until all are true:

1. Vehicle in `physics_domains` responds to input and moves without fallback force.
2. Vehicle moves with programmatic `set_control_input` only.
3. Soft-body is actually hydrated from sidecar and visible in simulation.
4. No runtime warnings indicating fallback materialization for required resources.
5. Automated tests cover at least:
   - world/local pose regression,
   - hydration-barrier pose parity with post-propagation world transform,
   - vehicle movement acceptance,
   - sidecar aggregate mapping correctness,
   - vehicle control range + phase-latching semantics,
   - payload-family mismatch rejection for vehicle/joint/soft-body resources.

## 8. Current Status

- This is now an implementation-tracking document, not design-only.
- `P1.1` implementation landed and was validated green:
  - `JoltWorld` now resolves authored body `collision_layer` / `collision_mask`
    to runtime object layers and applies mask-aware pair filtering.
  - `JoltBodies` now requests object layers from `JoltWorld` instead of static
    vs moving hard-coding.
  - `JoltQueries` now applies `collision_mask` through Jolt object-layer
    filtering for raycast/sweep/overlap.
  - Coverage includes query mask filtering, collision-mask contact blocking,
    world collision-filter pair blocking, and invalid collision-layer rejection.
- `P1.2` implementation landed and was validated green:
  - sidecar/cooker/runtime soft-body settings resource path is wired through
    `settings_ref` -> `settings_resource_index` -> `SoftBodyDesc.settings_blob`.
  - `PakFormatSerioLoaders` now deserializes
    `SoftBodyBindingRecord.settings_resource_index`.
  - Jolt soft-body runtime now persists authored settings payload per aggregate
    for material-topology rebuilds; procedural rebuild is prototype-only.
  - cook/pak docs updated to document required `settings_ref` and
    `settings_resource_index` contract.
- `P1.3` implementation landed and was validated green:
  - `physics.soft_body` now exposes explicit lookup APIs
    `get_exact(node)` and `find_in_ancestors(node)` (matching vehicle lookup
    shape), with no implicit `get(node)` traversal surface.
  - scripting surface tests now assert explicit soft-body lookup API exposure.
  - soft-body binding tests now cover invalid-node behavior for
    `get_exact`/`find_in_ancestors`.
  - scripting physics binding design doc now documents explicit lookup calls for
    vehicle and soft-body APIs.
- `P2.1` implementation is in progress:
  - fixed Jolt vehicle collision-tester layer selection:
    collision queries now use chassis object-layer defaults (pair-filter-aware)
    instead of hard-coded object layer `0`, eliminating silent no-contact
    behavior under authored collision-layer mappings.
  - added backend-agnostic API acceptance coverage for programmatic vehicle
    control (`VehicleControlInputProducesMotionIfSupported`).
  - added Jolt domain acceptance coverage for programmatic vehicle control:
    `SetControlInput` + stepping now must produce measurable chassis motion.
  - strengthened `PhysicsModule` sync integration coverage for vehicle/soft-body
    mapping round-trips (`node -> aggregate -> node`) and vehicle control-input
    propagation into backend state.
  - added scripting integration coverage with `PhysicsModule + FakePhysicsSystem`
    for vehicle control phase contracts:
    gameplay `set_control_input` propagates to backend, fixed-simulation
    `set_control_input` is rejected, and authored mapping lookup semantics are
    explicit (`get_exact` non-traversal vs `find_in_ancestors` traversal).
  - added matching scripting integration coverage for soft-body lookup
    semantics (`get_exact` non-traversal and `find_in_ancestors` ancestor
    traversal) against live module aggregate mappings.
  - this closes part of the regression signal for “vehicle does not move
    programmatically” and mapping drift, but full hydration/service
    sidecar-driven acceptance coverage is still pending.
- Validation delta:
  - `P1.3` and the current `P2.1` acceptance test additions were validated green
    in user-run build/test passes.
  - Remaining `P2.1` gap is scoped to hydration/service sidecar end-to-end
    acceptance tests that directly exercise `SceneLoaderService` hydration flow.
