# Physics and Scene Integration Design

This document outlines the design and implementation plan for integrating the backend-agnostic Physics API with Oxygen's Scene Graph. Specifically, it covers the implementation of Scene Facades (Item 1) and Scene-Level Query Wrappers (Item 5).

## 1. Scene Facades (`RigidBodyFacade` and `CharacterFacade`)

### Motivation
The core physics API (`IWorldApi`, `IBodyApi`) operates on raw `BodyId`s for performance and backend-agnosticism. However, game logic interacts primarily with the Scene Graph via `NodeHandle`s. To provide an ergonomic, object-oriented API without coupling the `Scene` module to the `Physics` module, we need facades that bridge `SceneNode` and the physics system.

### Design
We will introduce facade classes, such as `RigidBodyFacade` and `CharacterFacade`, which wrap a `BodyId` and provide direct access to physics operations. These facades will be provided by the `PhysicsModule` (or a dedicated `ScenePhysics` namespace) to maintain strict module boundaries.

#### API Ergonomics
```cpp
// Example usage:
SceneNode node = scene->CreateNode("Player");

// Attach a rigid body to the node
physics::BodyDesc desc;
desc.shape = physics::BoxShape{ {1.0f, 2.0f, 1.0f} };
desc.motion_type = physics::MotionType::kDynamic;

// The PhysicsModule (or a ScenePhysics facade) handles the attachment
// to keep the Scene module ignorant of Physics types.
ScenePhysics::AttachRigidBody(node, desc);

// Access the facade
if (auto rb = ScenePhysics::GetRigidBody(node)) {
    rb->AddForce({0.0f, 10.0f, 0.0f});
    rb->SetLinearVelocity({5.0f, 0.0f, 0.0f});
}
```

#### Implementation Details

To implement this without breaking module boundaries, we must follow these exact steps:

1. **Component Storage (in `Oxygen.PhysicsModule`):**
   - We cannot store `physics::BodyId` directly in `SceneNodeImpl` because `Oxygen.Scene` cannot depend on `Oxygen.Physics`.
   - **Solution:** `Oxygen.PhysicsModule` will maintain a private `std::unordered_map<scene::NodeHandle::IdType, physics::BodyId>` to track which node owns which body. This side-table approach requires zero changes to `Oxygen.Scene` and provides fast O(1) forward lookups (`NodeHandle` -> `BodyId`).

2. **The Facade Class (in `Oxygen.PhysicsModule`):**
   - Create `RigidBodyFacade` in the `Oxygen.PhysicsModule` target.
   - It will hold:
     - `scene::NodeHandle` (to access the scene node)
     - `physics::BodyId` (to pass to the physics API)
     - `observer_ptr<physics::IWorldApi>` (to execute commands)
   - It will expose methods like `AddForce(const glm::vec3& force)` which internally call `world_api->GetBodyApi().AddForce(body_id, force)`.

3. **The Bridge API (in `Oxygen.PhysicsModule`):**
   - Create a `ScenePhysics` class/namespace.
   - Implement `ScenePhysics::AttachRigidBody(scene::SceneNode& node, const physics::BodyDesc& desc)`.
   - **Inside this function:**
     1. Read the transform: `auto pos = node.GetTransform().GetWorldPosition().value_or(glm::vec3(0));`
     2. Inject the Node ID for reverse lookups: `BodyDesc modified_desc = desc; modified_desc.user_data = node.GetHandle().Id();`
     3. Create the body: `BodyId id = world_api->CreateBody(modified_desc, pos, rot);`
     4. Store the mapping: `physics_module->RegisterNodeBodyMapping(node.GetHandle(), id);`

4. **Lifecycle Management (in `Oxygen.PhysicsModule`):**
   - The `PhysicsModule` already inherits from `scene::ISceneObserver`.
   - We must add a new virtual method to `ISceneObserver` in `Oxygen.Scene` called `OnNodeDestroyed(const NodeHandle& node_handle)`.
   - `PhysicsModule` will override `OnNodeDestroyed`. When called, it looks up the `BodyId` for that `NodeHandle` in its side-table and calls `IWorldApi::DestroyBody(body_id)`.

*Note on Dependencies:* The `ScenePhysics` facade and the `PhysicsModule` live in the `Oxygen.PhysicsModule` target, which *does* depend on `Oxygen.Scene`. The core `Oxygen.Physics` target remains completely independent of the Scene Graph. It does not know what a `SceneNode` is, nor does it know how to get its transform. The `PhysicsModule` acts as the bridge, reading the transform from the `SceneNode` and passing it as raw math types (`glm::vec3`, `glm::quat`) to the `Oxygen.Physics` API.

## 2. Scene-Level Query Wrappers

### Motivation
Physics queries (`RayCast`, `Sweep`, `Overlap`) natively return `BodyId`s. Game developers need to know *which SceneNode* was hit, not just the internal physics ID. Furthermore, collision callbacks report events using `BodyId`s, which must be resolved back to `SceneNode`s to trigger gameplay logic.

### Design
We will provide a set of wrapper functions (e.g., in a `ScenePhysics` namespace or a `PhysicsWorldFacade` class) that execute physics queries and translate the results back into `NodeHandle`s using a highly efficient reverse lookup mechanism.

#### API Ergonomics
```cpp
// Example usage:
RayCast ray{ .origin = {0,0,0}, .direction = {0,0,-1}, .distance = 100.0f };

if (auto hit = ScenePhysics::CastRay(scene, ray)) {
    NodeHandle hit_node = hit->node;
    std::cout << "Hit node: " << hit_node.GetName() << "\n";
}
```

#### Implementation Details

To implement this without breaking module boundaries, we must follow these exact steps:

1. **Reverse Lookup (`BodyId` -> `NodeHandle`):**
   - When creating a body in `ScenePhysics::AttachRigidBody`, we store the `NodeHandle`'s internal ID (`node.GetHandle().Id()`) in the `user_data` field of the `physics::BodyDesc`. This is a type-erased `uint64_t`.
   - The physics backend (e.g., Jolt) must preserve this `user_data` and return it in query results and collision callbacks. This provides fast O(1) reverse lookups (`BodyId` -> `NodeHandle`).

2. **Wrapper Logic (in `Oxygen.PhysicsModule`):**
   - The wrapper calls the underlying `IWorldApi::RayCast()`.
   - It iterates through the returned hits.
   - For each hit, it extracts the `user_data` (which is the `NodeHandle::IdType`).
   - It reconstructs the `NodeHandle` using the `Scene`'s node registry (e.g., `scene->GetNode(user_data)`).

3. **Result Structures (in `Oxygen.PhysicsModule`):**
   - Create scene-specific result structures (e.g., `SceneRayCastHit`) that mirror the physics results but replace `BodyId` with `NodeHandle`.

```cpp
struct SceneRayCastHit {
    scene::NodeHandle node;
    glm::vec3 position;
    glm::vec3 normal;
    float distance;
};
```

## 3. Phased Implementation Plan

To execute this design reliably and incrementally, we will follow a phased approach. Each phase is isolated, testable, and strictly respects the module boundaries.

### Phase 1: Scene Graph Hooks (Target: `Oxygen.Scene`)
*Establish the lifecycle hooks without introducing any physics concepts.*
1. **Update `ISceneObserver`**: Add `virtual void OnNodeDestroyed(const NodeHandle& node) = 0;` to the interface.
2. **Trigger the Hook**: Update `Scene::DestroyNode` (or equivalent) to invoke `OnNodeDestroyed` on all registered observers right before the node is actually invalidated.

### Phase 2: State Management (Target: `Oxygen.PhysicsModule`)
*Set up the internal tracking before exposing any public APIs.*
1. **Add the Side-Table**: Add `std::unordered_map<scene::NodeHandle::IdType, physics::BodyId> node_to_body_;` to the `PhysicsModule` class.
2. **Handle Destruction**: Override `OnNodeDestroyed` in `PhysicsModule`. When called, look up the node ID in `node_to_body_`. If found, call `world_api_->DestroyBody(body_id)` and erase the entry.

### Phase 3: Facades & Bridge API (Target: `Oxygen.PhysicsModule`)
*Build the developer-facing API for attaching and manipulating bodies.*
1. **Create `RigidBodyFacade`**: Implement the lightweight wrapper holding `NodeHandle`, `BodyId`, and `IWorldApi*`. Add a few basic forwarding methods (e.g., `AddForce`, `SetLinearVelocity`).
2. **Create `ScenePhysics`**: Implement `ScenePhysics::AttachRigidBody(NodeHandle, BodyDesc)`.
   - Extract the world position/rotation from the node.
   - Set `desc.user_data = node.Id()`.
   - Call `CreateBody` on the physics backend.
   - Register the result in the `PhysicsModule`'s side-table.
3. **Implement Retrieval**: Add `ScenePhysics::GetRigidBody(NodeHandle)` which queries the side-table and returns a `std::optional<RigidBodyFacade>`.

### Phase 4: The Sync Loop (Target: `Oxygen.PhysicsModule`)
*Connect the physics simulation to the scene graph transforms.*
1. **Push (Kinematics)**: In `PhysicsModule::OnGameplay()`, iterate over kinematic bodies in the side-table, read their `SceneNode` transforms, and push them to the physics backend.
2. **Pull (Dynamics)**: In `PhysicsModule::OnSceneMutation()`, query the physics backend for all active/awake dynamic bodies, read their new transforms, and apply them back to the corresponding `SceneNode`s.

### Phase 5: Query Wrappers (Target: `Oxygen.PhysicsModule`)
*Translate physics queries back to scene nodes.*
1. **Define Results**: Create `SceneRayCastHit` containing a `NodeHandle` instead of a `BodyId`.
2. **Implement Wrappers**: Implement `ScenePhysics::CastRay`. Call the underlying physics `RayCast`, extract the `user_data` (Node ID) from the hit, and use `scene->GetNode(id)` to populate the `SceneRayCastHit`.
