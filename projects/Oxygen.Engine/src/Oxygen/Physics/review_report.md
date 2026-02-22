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

## 4. Recommendations & Potential Improvements

While the baseline V1 architecture is exceptional, as traffic and complexity grow, consider the following structural changes grounded in AAA engine optimization:

1. **Deferred Structural Mutations (JoltBodies):**
   The implementation of `FlushStructuralChanges` correctly eradicates lock contention. Instead of executing heavy Jolt compound shape rebuilds immediately under a mutex whenever `AddBodyShape` or `RemoveBodyShape` is called, it queues them via `pending_rebuilds_`.
   - **Strengths**: The `body_state_mutex_` now exclusively protects lightweight hash-map modifications, meaning gameplay scripts/threads can attach/detach shapes concurrently without stalling. The expensive `JPH::BodyInterface` updates are predictably drained during the dedicated `kGameplay` push phase where concurrency is centrally orchestrated.
2. **Further Optimization for Bulk API Queries:**
   The recently implemented bulk query architectures (`IBodyApi::GetBodyPoses`, `IWorldApi::GetActiveBodyTransforms`) are structurally excellent and heavily reduce virtual/interface overhead. To squeeze maximum performance out of them:
   - **Eliminate Allocation inside `JoltWorld::GetActiveBodyTransforms`:** A local `JPH::BodyIDVector` is currently created per-frame, causing heap fragmentation. Move this to a persistent context block (`JoltWorld::temp_active_body_ids_`) that is cleared and reused.
   - **Optimize Jolt Interface Reads:** Within `JoltWorld::GetActiveBodyTransforms`, combine the calls to `GetPosition()` and `GetRotation()` into the singular call `GetPositionAndRotation(...)` to halve Jolt's internal lock-state validation queries.
   - **Batch Kinematic Pushes:** The pullback from scene to physics (`OnSceneMutation`) correctly fetches the array data, however, pushing gameplay mutations `OnGameplay` still loops over `pending_transform_updates_` scalarly. Collect these into a contiguous buffer before executing a single `BodyApi::MoveKinematicBatch` query.
3. **Soft Body, Articulations, and Vehicles:**
   The `IPhysicsSystem` describes an explicit roadmap for new domain accessors (Joints, Articulation). When integrating these, maintaining the rigid separation between Scene mapping and internal domains will be critical, as Articulated bodies break the simple 1:1 `NodeHandle` to `BodyId` relationship.

## 5. Conclusion

The implementation achieves exactly what a state-of-the-art C++23 engine necessitates: it is cache-conscious, strictly typed, zero-allocation compliant where it matters, safely concurrent, and decoupled. The Oxygen Physics architecture flawlessly interfaces with your phase-based scheduling schema and conforms to the overarching philosophy of stable, handle-driven interactions.
