# Oxygen Engine - PAK Format v7 Specification (Physics & Collision)

**Date:** 2026-02-22
**Status:** Design / Specification
**Target File:** `src/Oxygen/Data/PakFormat.h`

## 1. Executive Summary

As the physics architecture of Oxygen Engine matures with full Jolt integration, the cooked content format must be upgraded from `v6` to `v7` to allow physics data to be pre-compiled (cooked) offline. This guarantees:

1. **Source of Truth Enforcement:** Collision bounds and topology are no longer inferred from render bounds at runtime.
2. **Zero-Overhead Loading:** Jolt convex hulls, meshes, and serialized settings bypass CPU-heavy runtime creation.
3. **Domain Binding:** Scenes can natively hydrate aggregates, vehicles, soft bodies, and joints using predefined component tables without script intervention.

## 2. Global Format Evolution

### 2.1 Header & Namespace Updates

- `PakHeader::version` must be bumped to `7`.
- A new `namespace oxygen::data::pak::v7` block will be introduced, importing the latest schema layer.
- The default namespace alias at the bottom of the file will target `v7`.

### 2.2 PAK Footer Extensions

To accommodate binary blobs of cooked physics data (e.g., Jolt `JPH::Shape` streams or `JPH::SoftBodySharedSettings`), we add a new region and table to `PakFooter`.

```cpp
struct PakFooter {
  // ... existing fields ...
  ResourceRegion script_region = {}; // From v5

  // -- New in v7 --
  ResourceRegion physics_region = {};

  // ... existing tables ...
  ResourceTable script_slot_table = {};

  // -- New in v7 --
  ResourceTable physics_resource_table = {};

  OffsetT browse_index_offset = 0;
  // ...
};
```

## 3. Physics Resource & Asset Descriptors

### 3.1 Physics Data Resource (`PhysicsResourceDesc`)

Describes a binary blob in the `physics_region`. Used to stream pre-cooked Jolt shapes (Triangle meshes, Convex Hulls, scaled variants) directly into Jolt's deserialization stream.

```cpp
// Sourced from engine Meta catalogs
enum class PhysicsResourceFormat : uint8_t {
  kJoltShapeBinary = 0,
  kJoltConstraintBinary = 1,
  kJoltSoftBodySharedSettingsBinary = 2,
};

#pragma pack(push, 1)
struct PhysicsResourceDesc {
  OffsetT data_offset = 0;         // Absolute offset to cooked Jolt data
  DataBlobSizeT size_bytes = 0;    // Size in bytes
  PhysicsResourceFormat format = PhysicsResourceFormat::kJoltShapeBinary;
  uint8_t reserved[7] = {};
  uint64_t content_hash = 0;       // First 8 bytes of SHA256
};
#pragma pack(pop)
static_assert(sizeof(PhysicsResourceDesc) == 24);
```

### 3.2 Physics Material Asset (`PhysicsMaterialAssetDesc`)

Stores surface interaction properties decoupled from visual materials.

```cpp
#pragma pack(push, 1)
struct PhysicsMaterialAssetDesc {
  AssetHeader header;
  float friction = 0.5f;
  float restitution = 0.0f;
  float density = 1000.0f;                    // kg/m^3
  uint8_t combine_mode_friction = 0;          // 0=Average, 1=Min, 2=Max, 3=Multiply
  uint8_t combine_mode_restitution = 0;
  uint8_t reserved[22] = {};                  // Pad to 128 bytes total
};
#pragma pack(pop)
static_assert(sizeof(PhysicsMaterialAssetDesc) == 128);
```

### 3.3 Collision Shape Asset (`CollisionShapeAssetDesc`)

Maps an asset key to a cooked physical shape resource.

```cpp
// Sourced from engine Meta catalogs
enum class CollisionShapeCategory : uint8_t {
  kConvex = 0,
  kMesh = 1,
  kCompound = 2
};

#pragma pack(push, 1)
struct CollisionShapeAssetDesc {
  AssetHeader header;
  ResourceIndexT physics_resource_index = kNoResourceIndex;
  CollisionShapeCategory shape_category = CollisionShapeCategory::kConvex;
  uint8_t reserved0[3] = {};
  float bounding_box_min[3] = {0,0,0};
  float bounding_box_max[3] = {0,0,0};
  uint8_t reserved1[100] = {};                // Pad to 256 bytes
};
#pragma pack(pop)
static_assert(sizeof(CollisionShapeAssetDesc) == 256);
```

## 4. Physics Binding Asset (`PhysicsSceneAssetDesc`)

To preserve strict domain separation, physics configuration **must not** be injected into the `SceneAssetDesc` component tables. The `Scene` graph and `SceneLoader` are structurally ignorant of the `PhysicsModule`.

Instead, a dedicated sidecar asset (`PhysicsSceneAssetDesc`) is introduced. It maps `SceneNodeIndexT` (referencing the origin `SceneAssetDesc` nodes) to the defined physics domains. A dedicated physics hydration pipeline then takes an instantiated `Scene` and this sidecar asset, binding Jolt physics bodies to the relevant `NodeHandle`s via `ScenePhysics::AttachRigidBody`.

```cpp
#pragma pack(push, 1)
struct PhysicsSceneAssetDesc {
  AssetHeader header;
  AssetKey target_scene_key;                  // 16 bytes (Ref to SceneAssetDesc)

  // Directory of physics component tables (analogous to Scene data tables)
  // Points to an array of `SceneComponentTableDesc` entries mapped to physics components
  OffsetT component_table_directory_offset = 0;
  uint32_t component_table_count = 0;

  // 95 + 16 + 8 + 4 = 123. 256 - 123 = 133
  uint8_t reserved[133] = {};                 // Pad to 256 bytes
};
#pragma pack(pop)
static_assert(sizeof(PhysicsSceneAssetDesc) == 256);
```

### 4.1 Rigid Body Binding (`RigidBodyBindingRecord`)

Describes a Jolt rigid body attached to a node.

```cpp
// Sourced from engine Meta catalogs
enum class PhysicsBodyType : uint8_t {
  kStatic = 0,
  kDynamic = 1,
  kKinematic = 2
};

enum class PhysicsMotionQuality : uint8_t {
  kDiscrete = 0,
  kLinearCast = 1
};

#pragma pack(push, 1)
struct RigidBodyBindingRecord {
  SceneNodeIndexT node_index = 0;
  PhysicsBodyType body_type = PhysicsBodyType::kStatic;
  PhysicsMotionQuality motion_quality = PhysicsMotionQuality::kDiscrete;
  uint16_t collision_layer = 0;               // Maps to CollisionLayers.h
  uint32_t collision_mask = 0xFFFFFFFF;

  float mass = 0.0f;                          // If 0, inferred from shape density
  float linear_damping = 0.05f;
  float angular_damping = 0.05f;
  float gravity_factor = 1.0f;

  uint32_t initial_activation = 1;            // Boolean
  uint32_t is_sensor = 0;                     // Boolean

  uint8_t reserved[16] = {};                  // Pad to 64 bytes
};
#pragma pack(pop)
static_assert(sizeof(RigidBodyBindingRecord) == 64);
```

### 4.2 Collider Binding (`ColliderBindingRecord`)

Attaches physical geometry and material to a RigidBody. (Supports `N` colliders per `RigidBody` via the node graph).

```cpp
#pragma pack(push, 1)
struct ColliderBindingRecord {
  SceneNodeIndexT node_index = 0;             // Must match a node with a RigidBody record
  AssetKey shape_asset_key = {};              // Ref to CollisionShapeAssetDesc
  AssetKey material_asset_key = {};           // Ref to PhysicsMaterialAssetDesc

  float local_translation[3] = {0,0,0};
  float local_rotation[4] = {0,0,0,1};        // Quat XYZW

  uint32_t is_trigger = 0;
  uint32_t override_collision_layer = 0;      // 0 = inherit from RigidBody
  uint8_t reserved[16] = {};                  // Pad to 96 bytes approx
};
#pragma pack(pop)
```

### 4.3 Advanced Domain Bindings (Characters, Soft Bodies, Vehicles, Joints, Aggregates)

Reflecting the newly implemented physics architectures. For complex configurations (Vehicles, Joints), the binding record remains tiny by referencing a serialized binary blob in the `physics_region`.

**Character Controller Binding:**

```cpp
#pragma pack(push, 1)
struct CharacterBindingRecord {
  SceneNodeIndexT node_index = 0;
  AssetKey shape_asset_key = {};              // Typically a Capsule
  float max_slope_angle_radians = 0.785f;     // ~45 degrees
  float max_strength = 100.0f;
  float character_padding = 0.02f;
  uint8_t reserved[12] = {};
};
#pragma pack(pop)
```

**Soft Body Binding:**
Matches `oxygen::physics::softbody::SoftBodyDesc`.

```cpp
#pragma pack(push, 1)
struct SoftBodyBindingRecord {
  SceneNodeIndexT node_index = 0;
  uint32_t cluster_count = 0;
  float stiffness = 0.0f;
  float damping = 0.0f;
  float edge_compliance = 0.0f;
  float shear_compliance = 0.0f;
  float bend_compliance = 1.0f;
  uint8_t tether_mode = 0;                    // kNone, kEuclidean, kGeodesic
  uint8_t reserved0[3] = {};
  float tether_max_distance_multiplier = 1.0f;
  uint8_t reserved1[12] = {};                 // Pad to 48 bytes
};
#pragma pack(pop)
static_assert(sizeof(SoftBodyBindingRecord) == 48);
```

**Joints/Constraints Binding:**
Declares an articulated link or two-body constraint between nodes. Because Jolt constraint properties (motor settings, limits, degrees of freedom) are highly variable and complex, the record points to a serialized `JPH::ConstraintSettings` binary block.

```cpp
#pragma pack(push, 1)
struct JointBindingRecord {
  SceneNodeIndexT node_index_a = 0;
  SceneNodeIndexT node_index_b = 0;           // Invalid/Sentinel = world attachment

  // Maps to a binary JPH::ConstraintSettings blob in the physics_region
  ResourceIndexT joint_settings_resource = kNoResourceIndex;

  float local_space_a_translation[3] = {0};
  float local_space_a_rotation[4] = {0,0,0,1};
  float local_space_b_translation[3] = {0};
  float local_space_b_rotation[4] = {0,0,0,1};

  uint8_t reserved[12] = {};
};
#pragma pack(pop)
static_assert(sizeof(JointBindingRecord) == 60);
```

**Vehicle Binding:**
Vehicles require extensive configuration arrays (wheels, suspension, engine, transmission). This record binds a chassis node to a serialized `JPH::VehicleConstraintSettings` or `JPH::VehicleControllerSettings` blob.

```cpp
#pragma pack(push, 1)
struct VehicleBindingRecord {
  SceneNodeIndexT node_index = 0;             // Must match a dynamic RigidBody node (chassis)

  // Maps to a binary JPH::VehicleConstraintSettings blob in the physics_region
  ResourceIndexT vehicle_settings_resource = kNoResourceIndex;

  uint8_t reserved[12] = {};
};
#pragma pack(pop)
static_assert(sizeof(VehicleBindingRecord) == 16);
```

**Aggregate Binding:**
Aggregates group physical bodies (like complex fractured destructibles or compound assemblies) to avoid broad-phase overhead by treating them as a single broad-phase bounding volume.

```cpp
// Sourced from engine Meta catalogs
enum class AggregateAuthority : uint8_t {
  kSimulation = 0,
  kCommand = 1
};

#pragma pack(push, 1)
struct AggregateBindingRecord {
  SceneNodeIndexT node_index = 0;             // Root node of the aggregate group
  uint32_t max_bodies = 0;                    // Pre-allocation size for JPH::Aggregate
  uint32_t filter_overlap = 1;                // Boolean: disable self-collision?
  AggregateAuthority authority = AggregateAuthority::kSimulation;
  uint8_t reserved[15] = {};                  // Pad to 28 bytes
};
# pragma pack(pop)
static_assert(sizeof(AggregateBindingRecord) == 28);

```

## 5. Tooling & Pipeline Impact Checklist

To realize this v7 format:

1. **Dedicated Physics Tooling/Exporters:** Generalized model formats (glTF, FBX) are poorly suited for authoring complex physics simulation setups. Tooling needs to leverage the Editor or a dedicated DCC physics exporter to author hulls, vehicles, and joints natively, passing these directly to the cooker to bake `JPH::Shape` payload streams into `PhysicsResourceDesc`.
2. **PakWriter:** Increment struct headers, handle new `ResourceRegion` offsets, and process the separate `PhysicsSceneAssetDesc` creation.
3. **Physics Hydration:** Implement a `PhysicsSceneLoader` or bridge that iterates `PhysicsSceneAssetDesc` binding records, resolving `SceneNodeIndexT` against instantiated scenes and hydrating the simulation via `ScenePhysics::AttachRigidBody()` and related facade APIs.
4. **Collision Source of Truth:** Game code fully ceases querying render geometry AABBs for physical intersections. All physics are orchestrated entirely by the cooked scene descriptor data.

## 6. Implementation-Grade ABI Contracts (Required)

### 6.0 Meta-catalog source-of-truth (Oxygen Core Meta pattern)

All persistable IDs/enum-values/flag-bit positions introduced for v7 must follow the existing `src/Oxygen/Core/Meta` catalog pattern:

1. Define canonical values in a dedicated `.inc` catalog (macro-driven), similar to:
   - `src/Oxygen/Core/Meta/Data/AssetType.inc`
   - `src/Oxygen/Core/Meta/Data/ComponentType.inc`
   - `src/Oxygen/Core/Meta/Input/PakFormat.inc`
2. Runtime enums/flags in `PakFormat.h` consume those macros; tooling and other modules consume the same catalog to avoid drift.
3. No duplicated numeric literals across runtime/tooling code for the same persisted ID.
4. New physics component/resource IDs must be declared once in Core Meta and referenced everywhere else.

### 6.1 Strict packing and size invariants

All v7 physics records must:

1. Use `#pragma pack(push, 1)` / `#pragma pack(pop)`.
2. End with `static_assert(sizeof(... ) == N)` for deterministic ABI.
3. Include explicit `reserved[]` for forward compatibility.
4. Avoid implicit padding assumptions.

### 6.2 Strongly typed enums for serialized fields

Replace raw numeric fields with typed enums where possible:

- `PhysicsResourceDesc::format` -> `enum class PhysicsResourceFormat : uint8_t`
- `CollisionShapeAssetDesc::shape_category` -> `enum class CollisionShapeCategory : uint8_t`
- `RigidBodyBindingRecord::body_type` -> `enum class PhysicsBodyType : uint8_t`
- `RigidBodyBindingRecord::motion_quality` -> `enum class PhysicsMotionQuality : uint8_t`
- `AggregateBindingRecord::authority` -> `enum class AggregateAuthority : uint8_t`

For flag bitmasks, follow Oxygen style:

1. bit positions are defined in meta catalogs (`..._FLAG(name, bit_index)`).
2. runtime flags use `OXYGEN_FLAG(bit_index)` + `OXYGEN_DEFINE_FLAGS_OPERATORS(...)`.
3. `to_string(flags)` must enumerate bits explicitly in Oxygen style.

All enums must have `to_string(...)` near type declaration and implementation in corresponding converters TU.

### 6.3 Endianness and alignment

1. Serialized format remains little-endian.
2. Loader must byte-swap on big-endian targets (future-proof requirement).
3. Deserialized runtime structs may be copied into aligned runtime representations; packed structs are wire format only.

## 7. Versioning and Migration Matrix (Required)

### 7.1 Container version behavior

1. `PakHeader.version == 6`: no physics region/table; v6 behavior unchanged.
2. `PakHeader.version == 7`: physics region/table allowed and validated.
3. Loading v7 physics payload with v6-only loader is unsupported and must fail deterministically with a clear diagnostic.

### 7.2 Scene/Physics sidecar compatibility

For `PhysicsSceneAssetDesc.target_scene_key`:

1. Scene key missing from mounted sources -> hard error.
2. Scene found but key mismatch -> hard error.
3. Scene found but node count/index constraints violated -> hard error.
4. Scene found and valid -> hydrate.

No implicit fallback to "best effort" rebinding is allowed.

## 8. End-to-End Pipeline & Hydration Policies (Required)

To ensure the physics data remains robust and authoritative, the pipeline from authoring to runtime is strictly divided. Hydration occurs through three distinct sources, each governed by strict phase-bound lifecycle contracts.

### 8.1 The Storage & Cooking Pipeline

1. **Production (Authoring):** Physics topologies, joints, and collision bounds are authored in DCC tools or Editor. Physics data is *always* natively bound to a hierarchy mapping (nodes).
2. **Cooking (PakWriter):** The cooker creates the SceneAssetDesc. Concurrently, the physics cooker isolates all physics properties and builds the sidecar PhysicsSceneAssetDesc. Baked binary shapes and constraints are packed into the physics_region and registered in the physics_resource_table. Render geometry is entirely ignored by the physics cooker.
3. **Loading (Content Module):** PakFile handles decompression and byte-swapping. The Content module reconstructs the SceneAsset object in memory and its PhysicsSceneAsset sidecar, parsing the metadata cleanly.

### 8.2 The Three Hydration Sources

Once loaded in memory, the transition to active runtime state (Hydration) is orchestrated by three actors:

#### A. Base Scene Hydrator (e.g., SceneLoaderService)
The foundational loader (like DemoShell's SceneLoaderService) reads the baseline SceneAsset to instantiate the SceneNodeImpl hierarchy.
- **Physics Policy:** SceneLoaderService is structurally unaware of physics. Following scene instantiation, a hook or dedicated ScenePhysicsHydrator intercepts the load completion, reads the PhysicsSceneAssetDesc, and resolves the SceneNodeIndexT -> NodeHandle mapping to bind baseline JPH::Body instances using ScenePhysics::AttachRigidBody() and similar facade APIs.
- **Policy Contract:** Strict mapping. Missing runtime nodes or key mismatches cause hard fail diagnostics as per Section 9.

#### B. C++ Game Modules
Game code acts as the command-authoritative layer that can inject dynamic entities (spawning projectiles, instantiating vehicles) dynamically into an active scene.
- **Physics Policy:** Game Modules instantiate nodes via the Scene API, then manually invoke methods on ScenePhysics (e.g., AttachVehicle, AttachCharacter) feeding customized or procedurally generated PhysicsResourceDesc configurations.
- **Policy Contract:** Domain separation preserved. The Game Module holds the business logic, the Scene holds the static transforms, and ScenePhysics proxies the JPH state.

#### C. Lua Scripting Layer
Scripts handle dynamic, ad-hoc, or level-specific mutations acting on the exposed Scripting APIs at runtime.
- **Physics Policy:** Scripts cannot deeply author physics topologies natively (you cannot construct a JPH::SoftBody in plain Lua). Instead, Scripts invoke predefined commands or templates (e.g., oxygen.physics.ApplyImpulse(), oxygen.physics.SetDamping()). If a script spawns a prefab, it triggers the same base hydration pipeline (A) for that prefab's sidecar, mapping new handles to the scripted nodes.
- **Policy Contract:** Scripts are strictly constrained to the API surface. They cannot mutate the offline-cooked source-of-truth metadata, and any runtime mutations are deferred and pushed down in the kGameplay or kSceneMutation phase boundaries.

### 8.3 Phase Lifecycle Guarantees

Hydration events and mutations must adhere strictly to Oxygen phase boundaries:

1. **Scene build/load phase**:
- Instantiate Scene nodes/components first.
- Resolve SceneNodeIndexT -> NodeHandle mapping once.

2. **Physics hydration phase (module-owned)**:
- Apply PhysicsSceneAssetDesc records via ScenePhysics/PhysicsModule APIs.
- No writes to SceneNodeImpl physics members (none should exist).

3. **Runtime phase ownership**:
- kGameplay: stage command intents + flush structural changes (from Scripts & C++ game code).
- kFixedSimulation: backend step only (Jolt executes).
- kSceneMutation: pull simulation state to scene (Jolt updates static/spatial hierarchies).

4. **Authority guarantees**:
- Rigid static/kinematic: scene/command authority per contract.
- Dynamic/soft-body/articulation: simulation authority.
- Vehicle: command input authority + simulation state ownership.

## 9. Validation and Diagnostics Contract (Required)

Define canonical diagnostic codes (examples):

- `physics_scene.target_scene_missing`
- `physics_scene.target_scene_mismatch`
- `physics_scene.node_index_out_of_range`
- `physics_scene.duplicate_binding`
- `physics_scene.shape_asset_missing`
- `physics_scene.material_asset_missing`
- `physics_scene.resource_index_out_of_bounds`
- `physics_scene.unsupported_record_version`
- `physics_scene.invalid_record_size`

Rules:

1. Structural corruption or identity mismatch -> hard fail.
2. Missing optional feature with declared fallback path -> warning (only when explicitly specified).
3. No silent dropping of records without a diagnostic.

## 10. Backend Payload Contracts (Jolt)

### 10.1 Physics resource payload versioning

`PhysicsResourceDesc` payloads must carry internal version tags in the binary blob header:

1. `format` identifies payload family.
2. blob header version identifies schema revision.
3. loader validates both before deserialization.

### 10.2 Integrity

1. `content_hash` validation policy must be explicit:

- strict mode: mismatch => hard fail
- permissive mode (dev only): warning + skip resource

1. Production presets should default to strict validation.

## 11. Acceptance Criteria (Done Means Done)

Section 4 is complete only when all are green:

1. **Schema/ABI**

- v7 structs compile with stable sizes and typed enums.

1. **Loader**

- v6/v7 behavior validated.
- malformed sidecar records rejected with deterministic diagnostics.

1. **Hydration integration**

- sidecar binds to scene by `target_scene_key`.
- bindings hydrate through physics module APIs only.
- no SceneNodeImpl physics storage introduced.

1. **Tests**

- unit tests for record parsing/validation.
- integration tests for successful hydration.
- negative tests for mismatch/out-of-range/missing resources/duplicate bindings.

1. **Docs/contracts**

- Domain separation and phase contracts updated and aligned.

## 12. Explicit Non-Goals for v7 (to avoid scope drift)

1. Runtime authoring/editor mutation protocol over network.
2. Cross-backend binary payload portability (Jolt blobs are backend-specific by design).
3. Automatic render-bounds-to-collision inference in shipping runtime paths.
