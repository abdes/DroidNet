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

- Implement soft-body lifecycle/material/state.
- Implement `GetAuthority` and `FlushStructuralChanges`.
- Expose non-null `IPhysicsSystem::SoftBodies()`.

**Tests:**

- API contract tests in `Physics_test.cpp`.
- Jolt soft-body tests for lifecycle/material/state/authority/flush/error handling.

**Integration:**

- PhysicsModule simulation-authoritative bridge policy for soft-body-owned nodes.

**Status:**

- API/contracts: done
- Jolt backend: pending
- Jolt domain tests: pending
- Module integration tests: pending

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
- New domain-specific bridge tests pending once Jolt extension domains are live.

**Integration:**

- Enforce:
  - `kGameplay`: stage/push command intents
  - `kFixedSimulation`: solver only
  - `kSceneMutation`: pull/apply simulation state

**Status:**

- Contract codification: done
- New-domain bridge implementation: pending
- New-domain bridge tests: pending

## 9. Feature: Bounds/Collision Source-of-Truth

**Goal:** Prevent drift between render bounds and collision authoring.

**API/Data:**

- Render bounds remain owned by Geometry/Renderable.
- Collision remains owned by Physics descriptors.
- Future cooked payload extension for physics collision authoring is required.

**Backend:**

- Jolt consumes collision descriptors; must not infer from render bounds implicitly.

**Tests:**

- Pending integration tests for authored vs derived vs strict-failure collision policy paths.

**Integration:**

- Scene hydration must attach physics from cooked physics payload, not ad hoc defaults.

**Status:**

- Ownership analysis: done
- Contract doc linkage: pending
- Pak/hydration implementation: pending
- Validation tests: pending

## 10. Feature: Product Integration (Demo + Scripting + Pak)

**Goal:** Deliver end-to-end developer-facing usage of the physics stack.

**API:**

- C++ demo usage through `ScenePhysics`.
- Luau bindings mirroring contract-safe surface.

**Backend:**

- Requires live Jolt extension domains for meaningful feature coverage.

**Tests:**

- Demo smoke tests + scripting binding tests + cooked asset migration tests.

**Integration:**

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
- [ ] Jolt implementation
- [ ] Jolt tests
- [ ] PhysicsModule integration tests

### 11.5 Scene Bridge

- [x] Phase/authority/domain-separation contracts documented
- [ ] New-domain bridge behavior implemented
- [ ] New-domain bridge tests

### 11.6 Bounds/Collision Source-of-Truth

- [ ] Contract doc linkage finalized
- [ ] Pak physics payload extension
- [ ] Scene hydration from cooked physics payload
- [ ] Derivation policy utility + diagnostics
- [ ] Integration tests

### 11.7 Product Integration

- [ ] Physics C++ demo
- [ ] Luau bindings
- [ ] RenderScene + Lua physics scene
- [ ] Pak migration and validation

## 12. Conclusion

The architecture baseline is strong and now organized by feature-delivery tracks. The next execution phase is advanced backend realization (articulation solver materialization + soft-body backend + expanded vehicle tuning/telemetry surface), followed by scene-bridge hardening and end-to-end product integration tracks.
