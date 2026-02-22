# Oxygen Engine - Physics Architecture & Jolt Backend Review

**Date:** 2026-02-22
**Module Reviewed:** `Oxygen.Physics` & `Oxygen.PhysicsModule`
**Backend:** Jolt Physics Integration

## 1. Executive Summary

The physics architecture in the Oxygen Engine is highly modular, robust, and aligns well with modern AAA game engine design principles. By enforcing strict separation between the engine simulation loop (`PhysicsModule`) and the abstract physics backend (`IPhysicsSystem`), the engine achieves structural flexibility and performance. The integration of the **Jolt Physics** backend is executed cleanly, leveraging stable, data-driven handles (`BodyId`, `WorldId`) instead of raw pointers to expose physics objects. This prevents lifecycle management issues such as dangling pointers and enhances cache locality.

## 2. Architectural Design Analysis

### 2.1 Backend Abstraction (`IPhysicsSystem` & APIs)

The explicit abstraction of domains (Worlds, Bodies, Shapes, Joints, Queries, Characters) through discrete interfaces (`IBodyApi`, `IQueryApi`, etc.) is excellent.

- **Strengths:**
  - Eradicates explicit dependencies on Jolt headers inside the core scene and gameplay logic.
  - Adheres strictly to the `/GR-` (no RTTI) constraint of the Oxygen engine. Type safety is managed via enum tags and separate typed ID wrappers rather than dynamic casting.
  - Returns `PhysicsResult<T>` (via C++23 monadic types like `std::expected`) to force the caller into explicitly handling backend errors, leaving behind archaic exception handling and silent failures.

### 2.2 Phase-Based Synchronization (`PhysicsModule`)

The phase contract defined in `PhysicsModule` is one of the strongest assets of this architecture. Tying physics to the broader generic engine loop through precise phases guarantees deterministic execution and averts data-races:

1. **`kFixedSimulation`**: Steps the physics solver. Strictly computational, no scene mutability. Allowing physics solver multi-threading (via Jolt's Job System) to freely run here is very effective.
2. **`kGameplay`**: Defers scene structure modifications and pushes kinematic state to physics.
3. **`kSceneMutation`**: Pulls simulated physical poses (Dynamic, Characters) back to the Scene graph.

- **Strengths:**
  - This "pull/push" separation avoids the "frame-tearing" common in simpler physics integrations where transforms are updated piecemeal.
  - Clear "Single Authority" mapping. `BodyType::kStatic`/`kKinematic` enforces scene authority, while `kDynamic` enforces physics authority.
  - Enforced one-frame latency is standard practice for decoupling game logic from solver integration, ensuring CPU cache lines aren't heavily contended between scene and physics.

### 2.3 Binding & Memory Layout

The use of `ResourceTable` coupled with standard library `unordered_map` for cross-indexing sets up a cache-friendly structure.

- Side-tables mapping `scene::NodeHandle` to internal handles (`BodyId`, `CharacterId`) efficiently solve the object-lifecycle impedance mismatch between the Scene Graph and the Physics system.
- Reserving capacities based on scene metrics (`EstimateBindingReserve`) minimizes reallocation hitches during level loads.

### 2.4 Handles & Type Safety

By leveraging the `NamedType` struct (e.g., `WorldId`, `BodyId`, `CharacterId`, `ShapeId`), you ensure strict type and arithmetic safety.

- **Strengths:**
  - A persistent issue in rendering/physics bindings is accidentally passing a `BodyId` to a parameter expecting a `ShapeId` because both are underlying `uint32_t` IDs. `NamedType` guarantees a compile-time failure, greatly diminishing subtle bugs.
  - Handle properties follow semantic value semantics (Copying, Hashing, Comparing) without carrying lifecycle weight.

### 2.5 Collision Filtering Hierarchy

`CollisionLayers.h` combined with `CollisionFilter.h` models a two-tier broad-phase and narrow-phase filtering mechanic (mapping explicit typed layers to broader physical culling domains).

- **Strengths:**
  - Offloads the logic of identifying collision matrices out of the raw Jolt integration, making game-domain logic explicitly dictate "what" collides with "what."
  - Using `CollisionLayer` combined with `BroadPhaseLayer` culling acts as a severe pre-requisite for optimizing query and NarrowPhase testing.

### 2.6 Character Controllers & Scene Facade

The design of the Character Controller encapsulated inside `ScenePhysics::CharacterFacade` separates rigid body dynamics from command-authoritative kinematic bodies smoothly.

- **Strengths:**
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

- **Strengths:**
  - Supplying pre-allocated buffers strictly from the caller enables zero-allocation physics sweeps/raycasts per frame. This directly counters standard pitfalls where engine physics queries fragment heap memory over repeated frame bursts.

### 3.5 Joint & Constraint Domain (`IJointApi`)

The introduction of the `IJointApi` and `JoltJoints` correctly implements the next phase of the domain separation roadmap.

- **Strengths:**
  - **Safe Body Acquisition:** Utilizing `JPH::BodyLockMultiWrite` when accessing `body_a` and `body_b` inside `CreateJoint` is best-practice. It prevents subtle race conditions where bodies could be mutated or destroyed concurrently by gameplay or physics background threading.
  - **Memory Management:** Anchoring the constraint instance `JPH::Ref<JPH::TwoBodyConstraint>` safely within a thread-safe `std::unordered_map` inside `JoltJoints` directly shields the client logic from `JPH` lifecycle and memory counting caveats.
  - **Extensible Factory:** The exhaustive `MakeConstraint` switch guarantees that `JointDesc` can effortlessly scale up with drives, limits, and motors as the engine matures.

## 4. Recommendations & Potential Improvements (Status)

While the baseline V1 architecture is exceptional, as traffic and complexity grow, consider the following structural changes grounded in AAA engine optimization:

1. **Deferred Structural Mutations (JoltBodies):**
   The implementation of `FlushStructuralChanges` correctly eradicates lock contention. Instead of executing heavy Jolt compound shape rebuilds immediately under a mutex whenever `AddBodyShape` or `RemoveBodyShape` is called, it queues them via `pending_rebuilds_`.
   - **Strengths**: The `body_state_mutex_` now exclusively protects lightweight hash-map modifications, meaning gameplay scripts/threads can attach/detach shapes concurrently without stalling. The expensive `JPH::BodyInterface` updates are predictably drained during the dedicated `kGameplay` push phase where concurrency is centrally orchestrated.

2. **Further Optimization for Bulk API Queries:**
   This optimization pass is now implemented:
   - `IBodyApi` now exposes bulk APIs (`GetBodyPoses`, `MoveKinematicBatch`) with Jolt and test-backend support.
   - `JoltWorld::GetActiveBodyTransforms` now reuses persistent `BodyIDVector` scratch storage (`temp_active_body_ids`) instead of allocating per call.
   - `JoltWorld::GetActiveBodyTransforms` now uses `GetPositionAndRotation(...)` for combined pose reads.
   - `PhysicsModule::OnGameplay` now batches kinematic pushes into contiguous buffers and calls `MoveKinematicBatch(...)`.

3. **Soft Body, Articulations, and Vehicles:**
   Now that the foundational `IJointApi` is established, the roadmap naturally extends into Articulations and Vehicles. When integrating these, maintaining the rigid separation between Scene mapping and internal domains will be critical, as Articulated bodies completely break the simple 1:1 `NodeHandle` to `BodyId` relationship.

## 5. Future Enhancements

The next high-value milestone is simulation mapping generalization for aggregate entities:

1. **Aggregate Mapping Model**
   Introduce a simulation-aggregate handle layer above raw `BodyId` to support 1:N and N:1 mappings (articulation roots, links, vehicle chassis/wheels, soft-body clusters).
   - Status: baseline model defined in `src/Oxygen/Physics/AggregateMappingModel.md` with first-class `AggregateId` handle contract.

2. **Domain-Local Expansion**
   Keep API separation strict while extending domains:
   - articulations / ragdolls as dedicated domain APIs,
   - vehicles as dedicated constrained multi-body APIs,
   - soft-body support with clear ownership and update contracts.

3. **Scene Bridge Contracts**
   Maintain deterministic phase contracts:
   - structural mutations remain deferred and flushed in controlled phases,
   - pose pull/push remains authority-driven and explicit,
   - scene mapping remains handle-based and backend-agnostic.

4. **Scale Validation**
   Add long-run stress suites for aggregate entities (attachment churn, scene switches, high-contact event throughput) before enabling gameplay-facing adoption.

5. **Integration Tracks (Near-Term)**
   - **New Physics C++ Demo Module**
     Add a dedicated demo showcasing `PhysicsModule` integration through the native C++ API (`AttachRigidBody`, `AttachCharacter`, queries/events), with explicit phase-safe mutation flow.
   - **Luau Physics Bindings**
     Expose a focused scripting surface mirroring `ScenePhysics` contracts (attach/get/move/query) with clear authority rules and phase constraints documented for script authors.
   - **RenderScene + Lua Physics Scene**
     Integrate physics into `RenderScene` with Lua-enabled scene authoring, validating end-to-end orchestration: script mutations -> scene mutation stream -> physics sync -> transform propagation -> render.
   - **PakFormat Upgrade for Cooked Physics Assets**
     Extend cooked asset schema to carry physics authoring/runtime data (collision shapes, material/filter metadata, character setup, optional compound/aggregate descriptors) with versioned migration support.

### 5.1 Bounds/Collision Source-of-Truth Hardening (Near-Term Priority)

This section captures the concrete gaps discovered while scanning usage of `AABB`, `BoundingBox`, `BoundingSphere`, and `bounding_` across Oxygen.

#### Current Ownership (As Implemented)

1. **Render visibility bounds source-of-truth:** `Data::GeometryAsset` + `Scene::RenderableComponent` world-space cache.
   - Geometry bounds authored/cooked in content/data:
     - `src/Oxygen/Data/GeometryAsset.cpp`
     - `src/Oxygen/Data/PakFormat.h`
     - `src/Oxygen/Content/Import/Internal/Pipelines/MeshBuildPipeline.cpp`
   - World bounds updated from scene transforms:
     - `src/Oxygen/Scene/SceneNodeImpl.cpp`
     - `src/Oxygen/Scene/Detail/RenderableComponent.cpp`
   - Renderer consumes scene renderable world bounds:
     - `src/Oxygen/Renderer/ScenePrep/Extractors.h`

2. **Physics collision source-of-truth:** explicit physics shape/body descriptors.
   - `src/Oxygen/Physics/Body/BodyDesc.h`
   - `src/Oxygen/Physics/Shape.h`
   - `src/Oxygen/Physics/Shape/ShapeDesc.h`
   - PhysicsModule sync is transform-authority only, not render-bounds-authority:
     - `src/Oxygen/PhysicsModule/PhysicsModule.h`

3. **Conclusion:** render bounds and collision shapes are intentionally different domains today; no automatic unification path exists yet.

#### Gaps / Risks

1. **No cooked-scene first-class collision authoring path yet**
   - Scene/content payloads are rich for render AABB data but do not yet provide a first-class, versioned physics-collision schema consumed during scene hydration.

2. **No single attach policy for deriving collision from geometry when data is missing**
   - Current fallback behavior can silently accept generic defaults (for example default sphere in `BodyDesc`) and hide authoring mistakes.

3. **Hydration/demo duplication risk**
   - Multiple example paths manually propagate geometry bounds and shape/runtime setup independently, increasing drift probability between visual and collision behavior.

4. **No explicit cross-domain consistency contract document**
   - Source-of-truth ownership is implicit in code, but not yet formalized as an enforceable contract for content, scene hydration, scripting, and gameplay modules.

#### Decisions (Locked)

1. Render and physics remain separate authoritative domains:
   - Render culling bounds are owned by Geometry/Scene Renderable.
   - Collision shapes are owned by Physics descriptors/assets.

2. Physics must not implicitly reinterpret render bounds at runtime unless explicitly requested by policy/tooling.

3. Missing collision authoring should be surfaced explicitly (warning/assert/contract), not silently masked by generic defaults in production paths.

#### Resumable Execution Backlog

1. **Define Source-of-Truth Contract Doc (P0)**
   - Add a short contract section under Physics docs clarifying:
     - who owns render bounds,
     - who owns collision bounds,
     - when derivation is allowed,
     - authority across phases.
   - Deliverable: one authoritative markdown contract with references from PhysicsModule and Scene docs.

2. **PakFormat Physics Payload Extension (P0)**
   - Add versioned records for physics collision authoring in cooked assets:
     - shape geometry, local offsets/rotation, layer/filter, body type/material.
   - Deliverable: schema update + migration notes + loader integration points.

3. **Scene Hydration Physics Binding (P0)**
   - During scene loading, instantiate physics bodies/characters from cooked physics descriptors through `ScenePhysics` APIs.
   - Deliverable: deterministic attach path with clear failure semantics.

4. **Controlled Derivation Utility (P1)**
   - Add explicit opt-in utility to derive primitive collision from geometry bounds when authored collision is absent.
   - Must be tool/import-time or hydration-time policy driven, never hidden frame-time behavior.
   - Deliverable: utility + policy flag + diagnostics.

5. **Contract Enforcement & Diagnostics (P1)**
   - Add checks to flag nodes that are physics-enabled without explicit collision policy.
   - Add stats/log counters for:
     - explicit authored collision,
     - derived collision,
     - fallback/default rejection.
   - Deliverable: diagnostics in module and debug telemetry.

6. **Integration Tests (P0/P1)**
   - Add tests for:
     - cooked scene with authored collision -> stable attach behavior,
     - missing collision + derivation policy -> expected result,
     - missing collision + strict policy -> expected failure.
   - Include end-to-end path with transform sync and render visibility unchanged.

#### Ready-to-Resume Checklist (Section 5 Full Scope)

- [x] **5.0.1 Aggregate Mapping Model:** simulation-aggregate handle model defined (supports 1:N and N:1 mappings).
- [x] **5.0.2 Aggregate API Surface:** articulation/vehicle/soft-body integration points documented in backend-agnostic interfaces.
- [x] **5.0.3 Aggregate Lifecycle Tests:** creation/destruction/rebind scenarios covered by tests.

- [ ] **5.0.4 Domain-Local Expansion:** articulation domain scaffolded with clear ownership and contracts.
- [ ] **5.0.5 Domain-Local Expansion:** vehicle domain scaffolded with clear ownership and contracts.
- [ ] **5.0.6 Domain-Local Expansion:** soft-body domain scaffolded with clear ownership and contracts.
- [ ] **5.0.7 Domain Separation:** Scene/Physics mapping remains backend-agnostic and handle-based for all new domains.

- [ ] **5.0.8 Scene Bridge Contracts:** phase contract doc updated (`kGameplay`, `kSceneMutation`, `kFixedSimulation`) with new domains.
- [ ] **5.0.9 Structural Mutation Contract:** deferred structural changes and flush points explicitly documented and tested.
- [ ] **5.0.10 Authority Contract:** transform authority rules codified for rigid, character, and aggregate entities.

- [ ] **5.0.11 Scale Validation:** long-run churn tests added (attach/detach/switch scene/event drain across many frames).
- [ ] **5.0.12 Scale Validation:** high-contact/high-event-throughput stress tests added.
- [ ] **5.0.13 Scale Validation:** regression suite integrated into regular test targets.

- [ ] **5.0.14 Integration Track:** dedicated Physics C++ demo module implemented and documented.
- [ ] **5.0.15 Integration Track:** Luau physics bindings implemented with contract-safe API surface.
- [ ] **5.0.16 Integration Track:** RenderScene + Lua physics scene integration completed end-to-end.
- [ ] **5.0.17 Integration Track:** PakFormat physics authoring/runtime payload upgrade completed with migration notes.

- [ ] **5.1.1 Bounds/Collision Contract:** source-of-truth document added and linked from Physics/PhysicsModule docs.
- [ ] **5.1.2 Bounds/Collision Data Path:** scene/cooked payload schema for physics collision finalized and versioned.
- [ ] **5.1.3 Bounds/Collision Hydration:** scene loader hydrates physics from cooked scene payload.
- [ ] **5.1.4 Bounds/Collision Derivation Policy:** explicit opt-in derivation utility implemented (no hidden runtime inference).
- [ ] **5.1.5 Bounds/Collision Enforcement:** strict contract checks and diagnostics enabled for missing collision authoring.
- [ ] **5.1.6 Bounds/Collision Validation:** tests cover authored, derived, and strict-failure paths.

## 6. Conclusion

The implementation achieves exactly what a state-of-the-art C++23 engine necessitates: it is cache-conscious, strictly typed, zero-allocation compliant where it matters, safely concurrent, and decoupled. The Oxygen Physics architecture flawlessly interfaces with your phase-based scheduling schema and conforms to the overarching philosophy of stable, handle-driven interactions.
