# Oxygen Engine - Physics Architecture & Jolt Backend Review

**Date:** 2026-02-22
**Module Reviewed:** `Oxygen.Physics` & `Oxygen.PhysicsModule`
**Backend:** Jolt Physics Integration

## 1. Executive Summary

The physics architecture in the Oxygen Engine is highly modular, robust, and aligns well with modern AAA game engine design principles. By enforcing strict separation between the engine simulation loop (`PhysicsModule`) and the abstract physics backend (`IPhysicsSystem`), the engine achieves structural flexibility and performance. The integration of the **Jolt Physics** backend is executed cleanly, leveraging stable, data-driven handles (`BodyId`, `WorldId`) instead of raw pointers to expose physics objects. This prevents lifecycle management issues such as dangling pointers and enhances cache locality.

## 2. Architectural Design Analysis

### 2.1 Backend Abstraction (`IPhysicsSystem` & APIs)

The explicit abstraction of domains (Worlds, Bodies, Shapes, Joints, etc.) through discrete interfaces (`IBodyApi`, `IQueryApi`, etc.) is excellent.

- **Strengths:**
  - Eradicates explicit dependencies on Jolt headers inside the core scene and gameplay logic.
  - Adheres strictly to the `/GR-` (no RTTI) constraint of the Oxygen engine. Type safety is managed via enum tags and separate typed ID wrappers rather than dynamic casting.
  - `PhysicsResult<T>` returns (likely via `std::expected` or similar monadic types in C++23) force the caller to explicitly handle backend errors, which is a significant improvement over exceptions or silent failures.

### 2.2 Phase-Based Synchronization (`PhysicsModule`)

The phase contract defined in `PhysicsModule` is one of the strongest assets of this architecture. Tying physics to the broader generic engine loop through precise phases guarantees deterministic execution and averts data-races:

1. **`kFixedSimulation`**: Steps the physics solver. Strictly computational, no scene mutability. Allowing physics solver multi-threading (via Jolt's Job System) to freely run here is very effective.
2. **`kGameplay`**: Defers scene structure modifications and pushes kinematic state to physics.
3. **`kSceneMutation`**: Pulls simulated physical poses (Dynamic, Characters) back to the Scene graph.

- **Strengths:**
  - This "pull/push" separation avoids the "frame-tearing" common in simpler physics integrations where transforms are updated piecemeal.
  - Clear "Single Authority" mapping. `BodyType::kStatic`/`kKinematic` enforces scene authority, while `kDynamic` enforces physics authority.
  - Enforced one-frame latency is an accepted industry standard for decoupling logic from simulation, ensuring cache lines aren't fought over by rendering/scene updates concurrently with physics.

### 2.3 Binding & Memory Layout

The use of `ResourceTable` coupled with standard library `unordered_map` for cross-indexing sets up a cache-friendly structure.

- Side-tables mapping `scene::NodeHandle` to internal handles (`BodyId`, `CharacterId`) efficiently solve the object-lifecycle impedance mismatch between the Scene Graph and the Physics system.
- Reserving capacities based on scene metrics (`EstimateBindingReserve`) minimizes reallocation hitches during level loads.

## 3. Jolt Integration Review

The Jolt Physics backend (`JoltBodies`, `JoltWorld`, `JoltCharacters`, etc.) has been mapped seamlessly behind the `IPhysicsSystem` APIs.

### 3.1 Handles & Wrappers

Jolt uses strict pointer-based lifetime management (`JPH::BodyID`, `JPH::BodyInterface`), which you've effectively wrapped behind engine-local IDs like `oxygen::physics::BodyId`. This prevents JPH types from leaking into front-end logic.

### 3.2 Coordinate System & Convention

Oxygen operates on a `+Z UP, -Y forward` convention. The usage of a dedicated `Converters.h` (to marshal `oxygen::Vec3`/`Quat` into `JPH::Vec3`/`JPH::Quat`) provides a clean translation boundary.

### 3.3 Shape State Caching

Within `JoltBodies.h`:

```cpp
struct BodyState final {
  JPH::RefConst<JPH::Shape> base_shape {};
  std::unordered_map<ShapeInstanceId, ShapeInstanceState> shape_instances {};
};
```

Storing a local `unordered_map` of `shape_instances` allows O(1) mutations for compound collision shapes without needing to rip out and rebuild the entire JPH hierarchy blindly.

## 4. Recommendations & Potential Improvements

While the baseline V1 architecture is exceptional, as traffic and complexity grow, consider the following structural changes grounded in AAA engine optimization:

1. **Lock Contention in JoltShapes/JoltBodies:**
   The `std::mutex shape_instance_mutex_` inside `JoltBodies` indicates dynamic structural changes are synchronized. If structural changes (adding/removing shapes) become frequent at runtime, this mutex might bottleneck the gameplay phase. Consider deferring shape mutations via a lock-free multi-producer queue to be consumed exclusively during the `kGameplay` push phase.
2. **Vectorized / Bulk API Queries:**
   Currently, state queries/updates operate scalarly (`GetBodyPosition(WorldId, BodyId)` -> `Vec3`). Best practice for large entity counts is to query and update via Bulk arrays (AoA/SoA data models). Consider introducing a `PullDynamicTransforms(WorldId, span<BodyId>, span<Vec3>, span<Quat>)` API to `IBodyApi` to minimize virtual interface overhead when `PhysicsModule` synchronizes 10,000+ objects in `kSceneMutation`.
3. **Soft Body, Articulations, and Vehicles:**
   The `IPhysicsSystem` describes an explicit roadmap for new domain accessors (Joints, Articulation). When integrating these, maintaining the rigid separation between Scene mapping and internal domains will be critical, as Articulated bodies break the simple 1:1 `NodeHandle` to `BodyId` relationship.

## 5. Conclusion

The implementation achieves exactly what a state-of-the-art C++23 engine necessitates: it is cache-conscious, strictly typed, safely concurrent, and decoupled. The Oxygen Physics architecture natively fits the OxCo concurrency models and bindless philosophies by favoring deterministic handle-based data access over chaotic polymorphic object webs.
