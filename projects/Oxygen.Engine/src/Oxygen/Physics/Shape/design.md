# Oxygen Physics Shape API Spec

This document is normative: all listed shape capabilities and contracts are
mandatory implementation requirements for Oxygen Physics. Demo-only, V1-only, or
partial-scope substitutions are non-compliant, and will be rejected!

## Goal

- Provide an engine-grade, complete, stable Physics shape API that can represent
  real production scenes without demo-specific hacks.
- Keep one durable shape contract so content iteration does not require
  recurring Pak format churn.
- Match mainstream engine capability coverage, with high fidelity, while
  remaining implementable on current backends (Jolt now, PhysX-compatible shape
  model).
- Authored as a collision shape asset in content/Pak.
- Expose collision shapes to Lua as first-class, explicit physics resources so
  scripts can create, attach, query, and inspect shape-based interactions
  without bypassing backend simulation authority or engine lifecycle contracts.

## Design Constraints

1. No demo-only behavior in core Physics API or backend shape semantics.
2. Shape behavior and data contracts must obey engine invariants from
   `src/Oxygen/Core/Constants.h`:
   - Right-handed world space.
   - `Z-up`.
   - Movement forward axis is `-Y` (`oxygen::space::move::Forward`).
   - Right axis is `+X` (`oxygen::space::move::Right`).
   - Physics gravity is along `-Z` (`oxygen::physics::Gravity`).
   - These are engine law and are not configurable.
3. Shape ownership is in physics sidecar records; render/scene assets do not
   define physics shape data.
4. The shape vocabulary and capability set in this document are the contract
   baseline and must stay explicit and non-ambiguous.
5. No hidden semantics in unrelated fields; shape behavior must be represented
   by explicit shape/capability data.
6. API and content contracts should be stable across demos; demos author data,
   they do not redefine core physics shape semantics.
7. Asset-key sentinel contract: an all-zero `AssetKey` is the invalid/null
   value for shape payload/material references. This is a hard cross-module
   invariant.

## Shape Vocabulary

Storage semantics used below:

- `PAK Data` means cooked `.pak` storage (`physics_resource_table` + `physics_region` when blob-backed).
- `Loose Cooked Data` means loose-cooked file pair storage (`*.physics.table` + `*.physics.data` when blob-backed).
- Descriptor-only shapes never require physics table/data blobs in either mode.

| Shape | Notes | Descriptor Only | PAK Data | Loose Cooked Data |
| --- | --- | --- | --- | --- |
| `Sphere` | Core primitive shape. | ✓ | params: `radius` | |
| `Capsule` | Primary character-friendly shape; stable for movement/contacts. | ✓ | params: `radius`, `half_height` | |
| `Box` | Core primitive shape. | ✓ | params: `half_extents` | |
| `Cylinder` | Convex-hull backed where no native cylinder exists. | ✓ | params: `radius`, `half_height` | |
| `Cone` | Convex-hull backed where no native cone exists. | | `physics_resource_asset_key` -> cooked convex blob | required: convex entry + blob |
| `ConvexHull` | Cooked/validated convex collision shape. | | `physics_resource_asset_key` -> cooked convex blob | required: convex entry + blob |
| `TriangleMesh` | Cooked mesh collision shape; typically static/kinematic usage. | | `physics_resource_asset_key` -> cooked mesh blob | required: mesh entry + blob |
| `HeightField` | Terrain collision representation. | | `physics_resource_asset_key` -> cooked heightfield blob | required: heightfield entry + blob |
| `Plane` | Infinite mathematical plane collider. | ✓ | params: `normal`, `distance` | |
| `WorldBoundary` | Engine boundary concept, distinct from `Plane`. | ✓ | params: `boundary_mode`, `limits` | |
| `Compound` | Collection of child shapes with per-child local transforms authored at cook input level; runtime consumes a cooked compound shape blob. | | `physics_resource_asset_key` -> cooked compound blob | required: compound entry + blob |

## Required Shape Capabilities

This section defines:

- common capabilities that the shape system exposes for the full shape API.
- shape-specific capability constraints and cook/runtime requirements.

### Oxygen Basic Capabilities

These capabilities are mandatory in Oxygen even when some other engines do not
expose them as explicit public shape contracts.

| Capability | Why It Exists | Runtime Representation | Cooked Representation |
| --- | --- | --- | --- |
| Stable shape identity/handles | Deterministic rehydration and script-safe references. | `ShapeId` (invalid sentinel: `kInvalidShapeId`) for shape resource lifetime and `ShapeInstanceId` (invalid sentinel: `kInvalidShapeInstanceId`) for attachment lifetime, both mapped to backend lifetime. | Stable shape record key + stable `CookedShapePayloadRef` (if payload-backed) |
| Deterministic serialization | Reproducible builds/cooks and stable diffs. | Deterministic hydration ordering and mapping. | Deterministic `ShapeDescriptor` encoding and deterministic `CookedShapePayloadRef` assignment |

### Common Capabilities

Representation terms used below:

- `ShapeDescriptor`: descriptor-authored shape capability fields.
- `CookedShapePayloadRef`: descriptor field that references a cooked physics payload.
- Payload container is format-dependent only: PAK uses `physics_resource_table` + `physics_region`; loose-cooked uses `*.physics.table` + `*.physics.data`.

| Capability | Why It Exists | Runtime Representation | Cooked Representation |
| --- | --- | --- | --- |
| Trigger/sensor | Overlap/event detection without physical contact response (no impulses). | `is_sensor: bool` on runtime shape/collider (`false` = response-enabled, `true` = sensor-only). | `ShapeDescriptor.is_sensor: uint32_t` (`0` = false, non-zero = true; writer emits normalized `0` or `1`) |
| Collision filtering (`layer`, `mask`) | Restricts interactions to intended gameplay domains and reduces unnecessary broadphase/narrowphase work. | `collision_own_layer: uint64_t` (exactly one bit set) and `collision_target_layers: uint64_t`. | `ShapeDescriptor.collision_own_layer: uint64_t` (exactly one bit set) and `ShapeDescriptor.collision_target_layers: uint64_t` (bit mask). |
| Physics material assignment | Friction/restitution behavior and combine policy. | Material handle bound at shape/collider creation. | `ShapeDescriptor.material_asset_key` |

### Required `ShapeDescriptor` fields

| Field | Type | Requirement |
| --- | --- | --- |
| `shape_type` | enum | Required for all shapes. |
| `local_position` | `Vec3` | Required for all shapes. |
| `local_rotation` | `Quat` | Required for all shapes. |
| `local_scale` | `Vec3` | Required for all shapes (`uniform`/`non-uniform` rules are shape-specific). |
| `is_sensor` | `uint32_t` bool | Required for all shapes (`0`/`1` normalized by writer). |
| `collision_own_layer` | `uint64_t` | Required for all shapes; exactly one bit set. |
| `collision_target_layers` | `uint64_t` | Required for all shapes; bit mask. |
| `material_asset_key` | `AssetKey` | Required for all shapes (fallback/default material allowed by policy). |
| `shape_params` | shape-specific struct | Required for descriptor-only shapes. |
| `cooked_shape_ref` | `CookedShapePayloadRef` | Single-descriptor field. Used only for payload-backed shapes; for descriptor-only shapes it MUST be set to invalid/null sentinel and is ignored by runtime. |

### Shape Payload Type Contract

`CookedShapePayloadRef` MUST carry an explicit payload type discriminator.

Runtime payload type contract:

```cpp
enum class ShapePayloadType : uint8_t {
  kConvex = 1,
  kMesh = 2,
  kHeightField = 3,
  kCompound = 4,
};
```

| Payload Type | Used By Shapes | Contract |
| --- | --- | --- |
| `kConvex` | `Cone`, `ConvexHull`, backend-fallback `Cylinder` | Refers to a cooked convex payload entry. |
| `kMesh` | `TriangleMesh` | Refers to a cooked triangle-mesh payload entry. |
| `kHeightField` | `HeightField` | Refers to a cooked heightfield payload entry. |
| `kCompound` | `Compound` | Refers to a cooked compound payload entry (child shapes + child local transforms). |

Rules:

- Descriptor-only shapes (`Sphere`, `Capsule`, `Box`, `Cylinder` when native,
  `Plane`, `WorldBoundary`) MUST NOT carry `cooked_shape_ref`.
- Payload-backed shapes MUST provide `cooked_shape_ref` with a payload type
  matching the shape category.
- Shape-to-payload type mismatch is invalid and MUST fail hydration.
- Missing payload entry for a required `cooked_shape_ref` is invalid and MUST
  fail hydration.

### Shape Params Contract

All numeric parameters are in engine world units and must obey engine axis
invariants (`Z-up`, right-handed).

| Shape | `shape_params` Fields | Validation Rules |
| --- | --- | --- |
| `Sphere` | `radius: float` | `radius > 0` |
| `Capsule` | `radius: float`, `half_height: float` | `radius > 0`, `half_height > 0` |
| `Box` | `half_extents: Vec3` | `half_extents.x > 0 && half_extents.y > 0 && half_extents.z > 0` |
| `Cylinder` | `radius: float`, `half_height: float` | `radius > 0`, `half_height > 0` |
| `Cone` | `radius: float`, `half_height: float` (authoring descriptor); cooked convex required | `radius > 0`, `half_height > 0`, valid `cooked_shape_ref` |
| `ConvexHull` | authoring descriptor + cooked convex payload | valid `cooked_shape_ref` type `kConvex` |
| `TriangleMesh` | authoring descriptor + cooked mesh payload | valid `cooked_shape_ref` type `kMesh`; dynamic body disallowed |
| `HeightField` | authoring descriptor + cooked heightfield payload | valid `cooked_shape_ref` type `kHeightField`; dynamic body disallowed |
| `Plane` | `normal: Vec3`, `distance: float` | `length(normal) > 0`; runtime uses normalized normal |
| `WorldBoundary` | `boundary_mode: enum`, `limits: AABB/planes` | boundary mode must be valid enum; limits must be finite and non-degenerate |
| `Compound` | authoring descriptor + cooked compound payload | valid `cooked_shape_ref` type `kCompound` |

Validation failure policy:

- Any invalid `shape_params` field is a hard load/hydration error.
- No implicit clamping of authored values.

### Transform And Scale Semantics

- `local_position` and `local_rotation` are always interpreted in parent-body
  local space.
- `local_scale` is shape-local scale applied before world transform.
- Uniform scale is supported for all shapes.
- Non-uniform scale support:
  - Required for `Box`, `TriangleMesh`, `HeightField`, `Compound`.
  - Allowed for `Sphere`/`Capsule`/`Cylinder`/`Cone` only when representable in
    backend without semantic change; otherwise creation/hydration fails.
- `Plane` ignores `local_scale`.
- `WorldBoundary` scale semantics are defined by `boundary_mode` and `limits`,
  not by geometric primitive scaling.

### Compound Payload Contract

Cooked compound payload MUST encode an ordered child table:

- `child_shape_ref` (shape/payload reference)
- `child_local_position: Vec3`
- `child_local_rotation: Quat`
- `child_local_scale: Vec3` (always present; fixed record layout)

Determinism and limits:

- Child order in payload is authoritative and deterministic.
- Duplicate child references are allowed.
- Empty compound payload is invalid.
- For child shapes that do not support non-uniform scaling, invalid scale
  values MUST fail hydration.

Runtime API contract in C++:

```cpp
struct CompoundChildDesc {
  ShapeId shape_id;
  Vec3 local_position;
  Quat local_rotation;
  Vec3 local_scale { 1.0F, 1.0F, 1.0F };
};
```

### Shape Lifecycle And Mutation Contract

- `CreateShape(const ShapeDescriptor&) -> ShapeId` validates full descriptor and
  payload references before publishing handle.
- `DestroyShape(ShapeId)` fails if still attached (`ShapeInstanceId` alive).
- Authored/cooked `ShapeDescriptor` data is immutable as content.
- Runtime shape objects identified by `ShapeId` are mutable by API (including
  scripting) under physics phase/lifecycle rules.
- Runtime mutation semantics (including potential backend rebuilds) are
  implementation-defined but MUST preserve `ShapeId` stability and contract
  validity.

## Shape Compatibility Matrix

| Shape | Static | Kinematic | Dynamic | Requires Cooked Payload | Backend Mapping Notes |
| --- | --- | --- | --- | --- | --- |
| `Sphere` | ✓ | ✓ | ✓ | | Native primitive in Jolt/PhysX. |
| `Capsule` | ✓ | ✓ | ✓ | | Native primitive in Jolt/PhysX. |
| `Box` | ✓ | ✓ | ✓ | | Native primitive in Jolt/PhysX. |
| `Cylinder` | ✓ | ✓ | ✓ | backend-dependent | Jolt native; PhysX convex-backed. |
| `Cone` | ✓ | ✓ | ✓ | ✓ | Convex-backed where no native cone exists. |
| `ConvexHull` | ✓ | ✓ | ✓ | ✓ | Cooked convex payload. |
| `TriangleMesh` | ✓ | ✓ | | ✓ | Cooked mesh payload; dynamic not supported in this contract. |
| `HeightField` | ✓ | ✓ | | ✓ | Cooked heightfield payload; dynamic not supported in this contract. |
| `Plane` | ✓ | ✓ | | | Infinite analytic plane; not a world-boundary policy object. |
| `WorldBoundary` | ✓ | ✓ | | | Engine policy shape mapped to backend primitives/rules. |
| `Compound` | ✓ | ✓ | ✓ | ✓ | Cooked compound payload with child-local transforms. |

## Implementation Notes

Collision filtering validation/logic:

- `collision_own_layer` must satisfy: `value != 0 && (value & (value - 1)) == 0` (exactly one bit set).
- Layer capacity is an Oxygen contract of 64 layers (one bit per layer in `uint64_t`).
- Pairwise bitmask gate:
  - `(A.collision_own_layer & B.collision_target_layers) != 0`
  - `(B.collision_own_layer & A.collision_target_layers) != 0`
- Final allow/deny is the conjunction of the bitmask gate and `ICollisionFilter::ShouldCollide(...)`.

Query support invariant:

- Query support is an engine invariant, not a descriptor capability.
- Every supported shape type MUST support query families `raycast`, `sweep`, and
  `overlap`.
- For each family, hit modes `any`, `closest`, and `all` MUST be provided.
- Query filtering semantics (`collision_own_layer`, `collision_target_layers`,
  trigger policy, include/exclude filters) MUST be identical across families and
  backend adapters.
- Query result semantics (distance ordering, normal orientation, shape/instance
  identity mapping) MUST be engine-normalized and backend-independent.
- Descriptor/cooked payload data are only normal shape instantiation data; they
  are not query-specific schema.

## Pak Format Impact

This section is normative and defines the required Pak schema alignment for this
shape spec.

### Scope Lock

- No backward-compatibility path is required.
- No Pak format version increment is required.
- The active Pak schema must be updated in place to match this shape contract.

### Required Serialized Types

Pak data must define explicit serialized types for:

- `ShapeType` (full shape vocabulary from this spec).
- `ShapePayloadType` (`kConvex`, `kMesh`, `kHeightField`, `kCompound`).
- `CookedShapePayloadRef` (`payload_type` + payload resource reference, with
  invalid sentinel support).
- `AssetKey` (serialized asset-key reference type for shape material assignment).
- `ShapeDescriptor` (single canonical serialized descriptor for all shapes).

Enum and constant source-of-truth contract:

- All serialized shape enums/constants must be declared in Core metadata `.inc`
  catalogs under `src/Oxygen/Core/Meta/Physics/`.
- The Pak format types and runtime physics/script-facing enum surfaces must all
  consume those same `.inc` definitions; parallel/manual enum definitions are
  non-compliant.
- Required Core metadata catalog files:
  - `src/Oxygen/Core/Meta/Physics/PakPhysicsShape.inc`
- Required shared catalogs include:
  - `ShapeType`
  - `ShapePayloadType`
  - `WorldBoundaryMode`
  - Any serialized sentinel/flag constants required by `ShapeDescriptor`
    validation.
- `PakPhysicsShape.inc` owns the shape payload/material sentinel constants
  consumed by serialized shape descriptors.

Required serialized sentinel/flag constants:

- `kInvalidPhysicsResourceAssetKey` (`AssetKey`) = all-zero bytes
- `kInvalidShapePayloadType` (`ShapePayloadType`) = `0`
- `kInvalidPhysicsMaterialAssetKey` (`AssetKey`) = all-zero bytes

Constant declaration rule:

- In `PakPhysicsShape.inc`, invalid shape/material sentinels must be declared
  as all-zero `AssetKey` constants (not numeric literals).
- `kShapeIsSensorFalse` (`uint32_t`) = `0`
- `kShapeIsSensorTrue` (`uint32_t`) = `1`

`material_asset_key` serialized contract:

- Type: `AssetKey` (16 bytes).
- Value must reference `PhysicsMaterialAssetDesc`.
- `kInvalidPhysicsMaterialAssetKey` means "no material assigned" and is the
  all-zero key.
- No mandatory fallback material slot is required by this shape spec.

### Enum Numeric Value Contract

Serialized enum numeric values are fixed and mandatory.
Authoritative source rule: these numeric values are defined by the required
Core `.inc` catalogs and consumed by Pak/runtime/bindings directly. Duplicated
hand-authored enum values are non-compliant.

`ShapeType`:

| Name | Value |
| --- | --- |
| `kInvalid` | `0` |
| `kSphere` | `1` |
| `kCapsule` | `2` |
| `kBox` | `3` |
| `kCylinder` | `4` |
| `kCone` | `5` |
| `kConvexHull` | `6` |
| `kTriangleMesh` | `7` |
| `kHeightField` | `8` |
| `kPlane` | `9` |
| `kWorldBoundary` | `10` |
| `kCompound` | `11` |

`ShapePayloadType`:

| Name | Value |
| --- | --- |
| `kInvalid` | `0` |
| `kConvex` | `1` |
| `kMesh` | `2` |
| `kHeightField` | `3` |
| `kCompound` | `4` |

`WorldBoundaryMode`:

| Name | Value |
| --- | --- |
| `kInvalid` | `0` |
| `kAabbClamp` | `1` |
| `kPlaneSet` | `2` |

### Packed Binary Layout Contract

The following serialized structs are packed (`#pragma pack(push, 1)`) and their
byte sizes are fixed.
`PakFormat.h` must enforce these layouts with compile-time checks:
`static_assert(sizeof(...))` and `static_assert(offsetof(...))` for
`CookedShapePayloadRef`, `ShapeParams`, and `ShapeDescriptor`.

| Type | Size (bytes) |
| --- | --- |
| `CookedShapePayloadRef` | `17` |
| `ShapeParams` | `80` |
| `ShapeDescriptor` | `277` |

`CookedShapePayloadRef` layout:

| Field | Type | Bytes |
| --- | --- | --- |
| `payload_asset_key` | `AssetKey` | `16` |
| `payload_type` | `ShapePayloadType` | `1` |

`ShapeDescriptor` field order:

| Field | Type | Bytes |
| --- | --- | --- |
| `header` | `AssetHeader` | `95` |
| `shape_type` | `ShapeType` | `1` |
| `local_position` | `float[3]` | `12` |
| `local_rotation` | `float[4]` | `16` |
| `local_scale` | `float[3]` | `12` |
| `is_sensor` | `uint32_t` | `4` |
| `collision_own_layer` | `uint64_t` | `8` |
| `collision_target_layers` | `uint64_t` | `8` |
| `material_asset_key` | `AssetKey` | `16` |
| `shape_params` | `ShapeParams` | `80` |
| `cooked_shape_ref` | `CookedShapePayloadRef` | `17` |

### `ShapeParams` Tagged Union Contract

`ShapeParams` is a fixed-size tagged union selected by `shape_type`. Unused
bytes in the active member are zeroed by writer.

Per-shape member set:

| `shape_type` | Union Member |
| --- | --- |
| `kSphere` | `SphereParams { float radius; float reserved[19]; }` |
| `kCapsule` | `CapsuleParams { float radius; float half_height; float reserved[18]; }` |
| `kBox` | `BoxParams { float half_extents[3]; float reserved[17]; }` |
| `kCylinder` | `CylinderParams { float radius; float half_height; float reserved[18]; }` |
| `kCone` | `ConeParams { float radius; float half_height; float reserved[18]; }` |
| `kConvexHull` | `ConvexHullParams { float reserved[20]; }` |
| `kTriangleMesh` | `TriangleMeshParams { float reserved[20]; }` |
| `kHeightField` | `HeightFieldParams { float reserved[20]; }` |
| `kPlane` | `PlaneParams { float normal[3]; float distance; float reserved[16]; }` |
| `kWorldBoundary` | `WorldBoundaryParams { uint32_t boundary_mode; float limits_min[3]; float limits_max[3]; float reserved[13]; }` |
| `kCompound` | `CompoundParams { uint32_t reserved_u32; float reserved[19]; }` |

Validation rules from `Shape Params Contract` remain mandatory.

### Hydration Error Contract

Hydration failures for shape schema validation are hard failures and must emit
the exact stable error IDs below with an explanatory message.

| Error ID | Condition |
| --- | --- |
| `OXY-SHAPE-001` | `shape_type` is invalid/unknown. |
| `OXY-SHAPE-002` | `collision_own_layer` is zero or not single-bit. |
| `OXY-SHAPE-003` | `is_sensor` is not normalized (`0` or `1`). |
| `OXY-SHAPE-004` | `cooked_shape_ref` is invalid for payload-backed shape. |
| `OXY-SHAPE-005` | `cooked_shape_ref` is set for descriptor-only shape. |
| `OXY-SHAPE-006` | `ShapePayloadType` does not match `shape_type` contract. |
| `OXY-SHAPE-007` | Referenced payload entry missing in resource table/data. |
| `OXY-SHAPE-008` | `shape_params` validation failure for active `shape_type`. |
| `OXY-SHAPE-009` | Compound payload has zero children. |
| `OXY-SHAPE-010` | Compound child scale violates child shape scale contract. |

### `ShapeDescriptor` Serialized Contract

Pak serialization must store `ShapeDescriptor` with these required fields:

- `shape_type`
- `local_position`
- `local_rotation`
- `local_scale`
- `is_sensor`
- `collision_own_layer`
- `collision_target_layers`
- `material_asset_key`
- `shape_params`
- `cooked_shape_ref`

Rules:

- `collision_own_layer` and `collision_target_layers` are serialized as
  `uint64_t`.
- `collision_own_layer` must carry exactly one set bit.
- `cooked_shape_ref` is always present in `ShapeDescriptor`; for
  descriptor-only shapes it must be set to invalid sentinel and ignored at
  runtime.
- `shape_type` to `ShapePayloadType` mapping must follow this document exactly;
  mismatches fail hydration.

### Shape Params Binary Contract

Pak serialization must provide a fixed, deterministic binary contract for
`shape_params` keyed by `shape_type`:

- The descriptor size/layout is fixed and predictable.
- `shape_params` is a fixed-size binary payload field in `ShapeDescriptor`
  represented as a tagged union keyed by `shape_type`.
- No pointer/reference/offset-indirection is permitted inside `shape_params`.
- No optional variable-length payload is permitted inside `ShapeDescriptor`.
- Per-shape parameter validation rules from `Shape Params Contract` are
  mandatory load/hydration gates.

### Cooked Payload Contract

Payload-backed shapes use `CookedShapePayloadRef` to reference cooked physics
payload entries stored in:

- Pak mode: `physics_resource_table` + `physics_region`.
- Loose-cooked mode: `*.physics.table` + `*.physics.data`.

Rules:

- Descriptor-only shapes do not use payload entries.
- Payload-backed shapes require a valid payload reference with matching
  `ShapePayloadType`.
- Missing or type-mismatched payload references are hard hydration errors.

### Compound Payload Contract In Pak

Compound payload data must serialize an ordered, deterministic child table with
fixed child record layout:

- `child_shape_ref`
- `child_local_position`
- `child_local_rotation`
- `child_local_scale` (always present)

Rules:

- Child order is authoritative.
- Empty compound payload is invalid.
- Invalid child scale for a child shape contract is a hard hydration error.

### Binding Records Alignment

Body/controller/joint binding records are topology and behavior bindings, not
shape capability schema. Shape capability fields are serialized in
`ShapeDescriptor`, then referenced by bindings.
