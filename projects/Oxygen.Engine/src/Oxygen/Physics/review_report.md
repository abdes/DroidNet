# Oxygen Engine - Physics Architecture & Jolt Backend Review

**Date:** 2026-02-22
**Module Reviewed:** `Oxygen.Physics` & `Oxygen.PhysicsModule`
**Backend:** Jolt Physics Integration

## 1. Executive Summary

The physics architecture in the Oxygen Engine is highly modular, robust, and aligns well with modern AAA game engine design principles. By enforcing strict separation between the engine simulation loop (`PhysicsModule`) and the abstract physics backend (`IPhysicsSystem`), the engine achieves structural flexibility and performance. The integration of the **Jolt Physics** backend is executed cleanly, leveraging stable, data-driven handles (`BodyId`, `WorldId`) instead of raw pointers to expose physics objects. This prevents lifecycle management issues such as dangling pointers and enhances cache locality.

## 2. Architectural Design Analysis

### 2.1 Backend Abstraction (`IPhysicsSystem` & APIs)

The explicit abstraction of domains (Worlds, Bodies, Shapes, Joints, Queries, Characters) through discrete interfaces (`IBodyApi`, `IQueryApi`, etc.) is excellent.

- **Strengths::**
  - Eradicates explicit dependencies on Jolt headers inside the core scene and gameplay logic.
  - Adheres strictly to the `/GR-` (no RTTI) constraint of the Oxygen engine. Type safety is managed via enum tags and separate typed ID wrappers rather than dynamic casting.
  - Returns `PhysicsResult<T>` (via C++23 monadic types like `std::expected`) to force the caller into explicitly handling backend errors, leaving behind archaic exception handling and silent failures.

### 2.2 Phase-Based Synchronization (`PhysicsModule`)

The phase contract defined in `PhysicsModule` is one of the strongest assets of this architecture. Tying physics to the broader generic engine loop through precise phases guarantees deterministic execution and averts data-races:

1. **`kFixedSimulation`**: Steps the physics solver. Strictly computational, no scene mutability. Allowing physics solver multi-threading (via Jolt's Job System) to freely run here is very effective.
2. **`kGameplay`**: Defers scene structure modifications and pushes kinematic state to physics.
3. **`kSceneMutation`**: Pulls simulated physical poses (Dynamic, Characters) back to the Scene graph.

- **Strengths::**
  - This "pull/push" separation avoids the "frame-tearing" common in simpler physics integrations where transforms are updated piecemeal.
  - Clear "Single Authority" mapping. `BodyType::kStatic`/`kKinematic` enforces scene authority, while `kDynamic` enforces physics authority.
  - Enforced one-frame latency is standard practice for decoupling game logic from solver integration, ensuring CPU cache lines aren't heavily contended between scene and physics.

### 2.3 Binding & Memory Layout

The use of `ResourceTable` coupled with standard library `unordered_map` for cross-indexing sets up a cache-friendly structure.

- Side-tables mapping `scene::NodeHandle` to internal handles (`BodyId`, `CharacterId`) efficiently solve the object-lifecycle impedance mismatch between the Scene Graph and the Physics system.
- Reserving capacities based on scene metrics (`EstimateBindingReserve`) minimizes reallocation hitches during level loads.

### 2.4 Handles & Type Safety

By leveraging the `NamedType` struct (e.g., `WorldId`, `BodyId`, `CharacterId`, `ShapeId`), you ensure strict type and arithmetic safety.

- **Strengths::**
  - A persistent issue in rendering/physics bindings is accidentally passing a `BodyId` to a parameter expecting a `ShapeId` because both are underlying `uint32_t` IDs. `NamedType` guarantees a compile-time failure, greatly diminishing subtle bugs.
  - Handle properties follow semantic value semantics (Copying, Hashing, Comparing) without carrying lifecycle weight.

### 2.5 Collision Filtering Hierarchy

`CollisionLayers.h` combined with `CollisionFilter.h` models a two-tier broad-phase and narrow-phase filtering mechanic (mapping explicit typed layers to broader physical culling domains).

- **Strengths::**
  - Offloads the logic of identifying collision matrices out of the raw Jolt integration, making game-domain logic explicitly dictate "what" collides with "what."
  - Using `CollisionLayer` combined with `BroadPhaseLayer` culling acts as a severe pre-requisite for optimizing query and NarrowPhase testing.

### 2.6 Character Controllers & Scene Facade

The design of the Character Controller encapsulated inside `ScenePhysics::CharacterFacade` separates rigid body dynamics from command-authoritative kinematic bodies smoothly.

- **Strengths::**
  - `CharacterFacade::Move` explicitly delegates the kinematic movement to the integration backend while seamlessly bridging spatial synchronization to the exact node by pushing `ApplyWorldPoseToNode`.
  - Distinguishing characters from standard `kDynamic` or `kKinematic` shapes allows the engine to decouple movement intention from the solver's rigid reactions.

## 3. Jolt Integration Review

The Jolt Physics backend (`JoltBodies`, `JoltWorld`, `JoltCharacters`, `JoltQueries`, etc.) has been mapped seamlessly behind the `IPhysicsSystem` APIs.

### 3.1 Pointer Wrapping & Encapsulation

Jolt relies significantly on pointers for object lifetimes (`JPH::BodyID`, `JPH::BodyInterface`, `JPH::CharacterVirtual`). These are successfully shielded behind engine-local IDs (e.g., `BodyId`). This completely prevents proprietary `JPH` types from ever leaking into generic gameplay / front-end layers.

### 3.2 Coordinate System Integration

Oxygen operates on a `+Z UP, -Y forward` convention. The usage of `Converters.h` (marshalling `oxygen::Vec3`/`Quat` into `JPH::Vec3`/`JPH::Quat`) provides an unambiguous vector translation boundary. No ad-hoc conversions clutter the core integrations.

### 3.3 Shape State Caching

Within `JoltBodies.h`:

```cpp
struct BodyState final {
  JPH::RefConst<JPH::Shape> base_shape {};
  std::unordered_map<ShapeInstanceId, ShapeInstanceState> shape_instances {};
};
```

Storing a local map of `shape_instances` enables O(1) compound collision mutations. This bypasses the typical rigid limitations of destructively rebuilding the entire JPH physical hierarchy upon any composite shape modification.

### 3.4 Allocation-Free Queries (`IQueryApi`)

The query interface leverages `std::span` (e.g., `Sweep(WorldId, ..., std::span<query::SweepHit>)`) for bulk queries.

- **Strengths::**
  - Supplying pre-allocated buffers strictly from the caller enables zero-allocation physics sweeps/raycasts per frame. This directly counters standard pitfalls where engine physics queries fragment heap memory over repeated frame bursts.

### 3.5 Joint & Constraint Domain (`IJointApi`)

The introduction of the `IJointApi` and `JoltJoints` correctly implements the next phase of the domain separation roadmap.

- **Strengths::**
  - **Safe Body Acquisition:** Utilizing `JPH::BodyLockMultiWrite` when accessing `body_a` and `body_b` inside `CreateJoint` is best-practice. It prevents subtle race conditions where bodies could be mutated or destroyed concurrently by gameplay or physics background threading.
  - **Memory Management:** Anchoring the constraint instance `JPH::Ref<JPH::TwoBodyConstraint>` safely within a thread-safe `std::unordered_map` inside `JoltJoints` directly shields the client logic from `JPH` lifecycle and memory counting caveats.
  - **Extensible Factory:** The exhaustive `MakeConstraint` switch guarantees that `JointDesc` can effortlessly scale up with drives, limits, and motors as the engine matures.

## 4. Feature: Aggregate Core (Identity + Membership)

**Goal:** Support `1:N` and `N:1` simulation mappings with stable handle identity.

**API:**

- `AggregateId` and converters in `Handles.h` / `ToStringConverters.cpp`.
- `IAggregateApi` lifecycle/membership/flush contract.
- Contract doc: `src/Oxygen/Physics/AggregateMappingModel.md`.

**Backend (Jolt):**

- Implement `CreateAggregate/DestroyAggregate/AddMemberBody/RemoveMemberBody/GetMemberBodies/FlushStructuralChanges`.
- Expose non-null `IPhysicsSystem::Aggregates()`.

**Tests:**

- API contract tests for lifecycle/rebind/flush in `Physics_test.cpp`.
- Jolt domain tests for aggregate semantics.

**Integration:**

- PhysicsModule side tables support aggregate mappings without scene-type leakage into backend APIs.

**Status:**

- API/contracts: done
- Jolt backend: done
- Jolt domain tests: done
- Module integration tests: done

## 5. Feature: Articulation Domain

**Goal:** Support linked-body articulated structures with explicit topology ownership.

**API:**

- `ArticulationDesc`, `ArticulationLinkDesc`.
- `IArticulationApi`:
  - create/destroy
  - add/remove link
  - root/link queries
  - authority query
  - structural flush

**Backend (Jolt):**

- Implement articulation graph lifecycle and topology edits.
- Implement `GetAuthority` and `FlushStructuralChanges`.
- Expose non-null `IPhysicsSystem::Articulations()`.

**Tests:**

- API contract tests in `Physics_test.cpp`.
- Jolt articulation tests for lifecycle/link/authority/flush/error handling.

**Integration:**

- PhysicsModule bridge policy for articulation-owned nodes (simulation-authoritative baseline).

**Status:**

- API/contracts: done
- Jolt backend: done
- Jolt domain tests: done
- Module integration tests: done

## 6. Feature: Vehicle Domain

**Goal:** Support command-driven constrained multi-body vehicle simulation.

**API:**

- `VehicleDesc`, `VehicleControlInput`, `VehicleState`.
- `IVehicleApi`:
  - create/destroy
  - control input
  - state query
  - authority query
  - structural flush

**Backend (Jolt):**

- Implemented with real `JPH::VehicleConstraint` + `JPH::WheeledVehicleController` ownership per vehicle.
- Chassis/wheel topology from `VehicleDesc` is materialized into Jolt wheel settings at creation.
- `SetControlInput` drives controller input via `SetDriverInput(...)` and activates the chassis body.
- `GetState` is derived from runtime constraint state (`wheel->HasContact()`) and forward speed projection.
- `FlushStructuralChanges` now returns tracked create/destroy structural deltas without backend TODO.
- Not yet exposed by Oxygen API surface: drivetrain tuning, suspension parameter editing, wheel telemetry streams, and per-wheel contact query API.
- Exposes non-null `IPhysicsSystem::Vehicles()`.

**Tests:**

- API contract tests in `Physics_test.cpp`.
- Jolt vehicle tests for lifecycle/control/state/authority/flush/error handling.

**Integration:**

- PhysicsModule command-authoritative gameplay staging + simulation execution contract.

**Status:**

- API/contracts: done
- Jolt backend: baseline real implementation (`VehicleConstraint`/controller-backed)
- Jolt domain tests: API + runtime-state verified
- Module integration tests: done

## 7. Feature: Soft-Body Domain

**Goal:** Support deformable simulation objects with explicit material/state API.

**API:**

- `SoftBodyDesc`, `SoftBodyMaterialParams`, `SoftBodyState`.
- `ISoftBodyApi`:
  - create/destroy
  - material params
  - state query
  - authority query
  - structural flush

**Backend (Jolt):**

- Implemented actual `JPH::Body` generation using `JPH::SoftBodySharedSettings::sCreateCube` based on the requested cluster count.
- Soft body is safely registered to Oxygen's world identity bindings (`world->RegisterBody`).
- `SetMaterialParams` currently maps:
  - `damping -> MotionProperties::SetLinearDamping`
  - `stiffness -> SoftBodyMotionProperties::SetPressure`
- Runtime topology material updates (compliance/tether) are now deferred and applied in `FlushStructuralChanges(...)` through an internal soft-body rebuild pipeline.
- Creation-time topology authoring now maps:
  - edge/shear/bend compliance
  - tether mode and tether distance multiplier
  into `SoftBodySharedSettings::CreateConstraints(...)`.
- `SoftBodyDesc::anchor_body_id` is currently reported as `kNotImplemented` on Jolt pending skin/tether-based anchor integration (two-body rigid constraints are not valid for soft-body solver path).
- Expose non-null `IPhysicsSystem::SoftBodies()`.

**Tests:**

- API contract tests in `Physics_test.cpp`.
- Jolt soft-body tests for lifecycle/material/state/authority/flush/error handling.
- Step-boundary regression test for repeated `SetMaterialParams` calls.
- Deferred topology material-update tests for coalescing + flush-apply behavior.

**Integration:**

- PhysicsModule simulation-authoritative bridge policy for soft-body-owned nodes.

**Status:**

- API/contracts: done
- Jolt backend: real implementation (`CreateAndAddSoftBody` + creation-time compliance/tether mapping + runtime damping/stiffness updates + deferred topology rebuild on flush + safe `anchor_body_id` fallback to `kNotImplemented`)
- Jolt domain tests: done
- Module integration tests: pending

**Near-Term Follow-Ups (engine-grade completion):**

- [x] `physics-softbody-001` API expansion:
  - Added explicit compliance/tether authoring fields to `SoftBodyMaterialParams` / `SoftBodyDesc`.
  - Defined initial backend semantics in code contracts/comments.
- [x] `physics-softbody-002` Backend mapping:
  - Runtime topology compliance/tether updates are accepted in `SetMaterialParams(...)`, coalesced, and applied during `FlushStructuralChanges(...)` by rebuilding the soft-body with new shared settings.
  - Creation-time mapping remains implemented.
- [x] `physics-softbody-003` Verification:
  - Added Jolt contract/domain tests covering deferred topology updates, coalescing, and flush application behavior.
  - Added repeated-step regression coverage for non-topology runtime updates.
  - Module-level tests remain tracked under Section 8 when the soft-body scene bridge lands.

## 8. Feature: Scene Bridge Contracts (PhysicsModule)

**Goal:** Keep deterministic phase behavior and clear authority boundaries across all domains.

**API/Docs:**

- `PhysicsModule.h` class contract updated.
- `src/Oxygen/PhysicsModule/PhaseContracts.md`.
- `src/Oxygen/Physics/System/DomainSeparationContract.md`.

**Backend:**

- Backend domains remain scene-agnostic; bridge logic remains in module.

**Tests:**

- Existing PhysicsModule phase/authority tests remain baseline.
- Added domain-specific module bridge tests for aggregate extension routing:
  - vehicle command-authority baseline + lifecycle destroy paths,
  - soft-body simulation-authority baseline + lifecycle destroy paths.

**Integration:**

- Enforce:
  - `kGameplay`: stage/push command intents
  - `kFixedSimulation`: solver only
  - `kSceneMutation`: pull/apply simulation state

**Status:**

- Contract codification: done
- New-domain bridge implementation: done (vehicle/soft-body routing + flush)
- New-domain bridge tests: done (module sync tests)

## 9. Feature: Bounds/Collision Source-of-Truth

**Goal:** Enforce strict domain separation by sourcing collision topologies entirely from offline-cooked assets, preventing drift and eliminating runtime inference from render bounds.

**API/Data:**

- Render bounds remain exclusively owned by `Geometry`/`Renderable`.
- Collision data is decoupled into a dedicated `PhysicsSceneAssetDesc` sidecar asset (per PakFormat v7 spec).
- Pre-cooked `JPH::Shape` streams and configuration blobs are explicitly managed by `PhysicsResourceDesc`.
- Hydration contract is strict: physics sidecar contract violations are hard-fail scene-load errors (no runtime fallback derivation).

**Backend:**

- Jolt consumes collision descriptors directly from packed binary structures (zero-overhead).
- Backend strictly enforces the authored source-of-truth; fallback to render-bounds inference is prohibited.

**Tests:**

- Schema and container validation tests for new v7 structs.
- Contract tests covering strict hard-fail behavior for missing/mismatched/invalid physics sidecar data.
- Negative tests asserting hard-fails on identity mismatch, missing references, or hydration contract violations.

**Integration:**

- Separation of concerns must be strictly enforced across three architectural layers:
  1. **Data Module:** Defines the in-memory types (e.g., `PhysicsSceneAsset`) that map to the v7 `PakFormat`.
  2. **Content Module:** Provides the isolated deserializer (e.g., `PhysicsSceneLoader`) to read loose/PAK binaries into memory objects.
  3. **Hydration (Service/Module):** The base hydrator (e.g., `SceneLoaderService`) reads the primary `SceneAsset` first. Once complete, it loads the `PhysicsSceneAsset` and orchestrates physics attachment via `PhysicsModule` facade APIs (`ScenePhysics::AttachRigidBody`).
- `SceneNodeImpl` remains completely free of physics state, and the Jolt backend never accesses raw PAK or generic file streams.

**Status:**

- Ownership analysis & Architecture: done
- PakFormat v7 specification: done
- Hydration hard-fail contract decision: done
- Pak/hydration implementation: done (strict sidecar identity checks + runtime hard-fail contract + module-lifecycle-aware hydration for rigid-body/character bindings)
- Validation tests: pending (section-10 E2E coverage for authored shape/material and extended binding domains)

## 10. Feature: Product Integration (Demo + Scripting + Pak)

**Goal:** Deliver end-to-end developer-facing usage of the physics stack.

**Demo Tracks:**

- `RenderScene` YAML suite: multiple authored demo scenes switched at runtime, minimal C++ orchestration.
- C++ physics demo: one moderate-complexity procedural scenario using only simple generated meshes.

**RenderScene YAML Demo Scenes (`Physics Validation Playground`):**

- **Zone A - Contract Gate (Hydration Truth Test):**
  - Load a scene with a valid physics sidecar and verify all rigid-body and character bindings hydrate.
  - Provide negative launch variants (developer toggle/CLI flag) that intentionally use:
    - missing sidecar,
    - scene-key mismatch,
    - node-count mismatch.
  - Expected result: scene-load hard-fails immediately with explicit diagnostics (no fallback, no partial scene activation).

- **Zone B - Rigid Body Stack + Restitution Lane:**
  - Procedural tower of mixed primitive rigid bodies dropped onto static floor and angled walls.
  - Parallel “restitution lane” with spheres of increasing bounce to visually validate material behavior.
  - Validates broadphase/narrowphase contact generation, sleep/wake transitions, and stable stacking.

- **Zone C - Friction + Slope Conveyor:**
  - Three ramp tracks with low/medium/high friction material assignment.
  - Identical cubes released simultaneously to compare slide distance and settle time.
  - Validates physics material mapping (content/data -> runtime behavior) and deterministic relative ordering.

- **Zone D - Character Traversal Course:**
  - Kinematic character controller path over steps, shallow slopes, steep slopes, and ledges.
  - Includes a moving rigid platform intersection to verify interaction constraints.
  - Validates character binding hydration, slope limits, step handling, and collision filtering.

- **Zone E - Collision Filtering Matrix:**
  - Spawn groups on distinct collision layers/masks:
    - `WorldStatic`, `WorldDynamic`, `Character`, `TriggerLike`.
  - Explicit expected pairs (collide / ignore) documented and checked at runtime counters.
  - Validates that cooked filter bits are honored exactly after hydration.

- **Zone F - Stress Ring (Stability + Throughput):**
  - Circular arena continuously spawning and retiring primitive rigid bodies under cap.
  - Measures step time, active body count, and contact pair count over several minutes.
  - Validates no crashes, no runaway allocations, and stable simulation under sustained churn.

- Delivery model:
  - Each zone is a separate YAML scene/spec packaged through PakGen v7.
  - RenderScene switches between them (menu/CLI) with near-zero scene-specific C++ logic.
  - Primary validation target: end-to-end cooked path and hydration contract enforcement.

**C++ Physics Demo Scenario (single scene): `Ramp Gauntlet to Bowl`**

- Moderate-complexity procedural setup in one executable scene, no authored render assets.
- Core layout:
  - long inclined plane ramp,
  - one player sphere spawned/released at the top,
  - staggered cube obstacle rows along the ramp,
  - mixed obstacle authority: static cubes and dynamic cubes,
  - mid-ramp hinged cube flippers that swing on impact,
  - end-zone shallow bowl assembled from ring-arranged spheres.
- Flow:
  1. Sphere release from top of ramp.
  2. Sphere collides with staggered cube rows, pushing dynamic cubes while static cubes enforce fixed route constraints.
  3. Midway impacts activate hinged flippers, introducing path deflection variability.
  4. Sphere exits ramp and drops into the bowl, then settles to rest.
- Validation value:
  - Demonstrates static/dynamic/hinged interaction in one readable scenario.
  - Validates collision response, contact stability, and energy dissipation toward a deterministic rest state.
  - Keeps implementation procedural and compact while still producing meaningful gameplay-like physics behavior.

**Why this plan is strong:**

- RenderScene scenes validate the real production data path (YAML -> PakGen -> loader -> hydration -> runtime).
- C++ demo remains lightweight, procedural, and fast to iterate while still demonstrating integrated behavior.
- Combined coverage spans strict load-time contracts and runtime simulation correctness.
- Produces clear pass/fail signals observable both visually and via counters/log assertions.

**API:**

- Integration of `PhysicsSceneLoader` hook for Base Scene Hydration.
- C++ demo usage via `ScenePhysics` facade (Game Module command-authority path), with procedural primitive setup only.
- Luau bindings mirroring contract-safe surface (Scripts pushing deferred intents via `oxygen.physics`).

**Backend:**

- Requires live Jolt extension domains for meaningful feature coverage.

**Tests:**

- Demo smoke tests + scripting binding tests + cooked asset migration tests.
- Add E2E assertions tied to RenderScene YAML suite:
  - contract-gate hard-fail matrix,
  - rigid-body/character hydration counts,
  - collision-filter pair outcomes,
  - long-run stability counters.
- Add C++ demo smoke assertions:
  - stable step loop under timed event sequence,
  - expected active-body/contact ranges,
  - no runtime errors during spawn/cleanup phase.

**Integration:**

- RenderScene orchestration for scene switching across YAML validation suite.
- C++ physics demo with procedural geometry only.
- RenderScene + Lua end-to-end orchestration.
- Pak format upgrade and migration.

**Status:**

- Planning: done
- Implementation: pending
- Validation: pending

## 11. Unified Verifiable Task List

### 11.1 Aggregate Core

- [x] API/contracts
- [x] Jolt implementation
- [x] Jolt tests
- [x] PhysicsModule integration tests

### 11.2 Articulation

- [x] API/contracts
- [x] Jolt implementation
- [x] Jolt tests
- [x] PhysicsModule integration tests

### 11.3 Vehicle

- [x] API/contracts
- [x] Jolt implementation (Constraint/controller baseline)
- [x] Jolt simulation tests (Runtime-state baseline)
- [x] PhysicsModule integration tests

### 11.4 Soft-Body

- [x] API/contracts
- [x] Jolt implementation (baseline active, but `anchor_body_id` tethering needs work)
- [x] Jolt tests
- [x] PhysicsModule integration tests

### 11.5 Scene Bridge

- [x] Phase/authority/domain-separation contracts documented
- [x] New-domain bridge behavior implemented
- [x] New-domain bridge tests

### 11.6 Bounds/Collision Source-of-Truth

- [x] Contract doc linkage finalized
- [x] Pak physics payload extension
- [x] Scene hydration from cooked physics payload (section-9 scope: rigid-body/character sidecar bindings with strict contract enforcement)
- [x] Hard-fail enforcement + diagnostics
- [ ] Integration tests

### 11.7 Product Integration

- [ ] Physics C++ demo
- [ ] Luau bindings
- [ ] RenderScene + Lua physics scene
- [ ] Pak migration and validation

## 12. Conclusion

The architecture baseline is strong and now organized by feature-delivery tracks. The next execution phase is advanced backend realization (articulation solver materialization + soft-body backend + expanded vehicle tuning/telemetry surface), followed by scene-bridge hardening and end-to-end product integration tracks.
