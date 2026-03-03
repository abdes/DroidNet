# Comprehensive Physics Cooking Architecture Specification

## 0. Purpose

This document defines the complete physics cooking design for Oxygen content import.
It replaces sidecar-only scope with full physics domain coverage.

This spec is implementation-facing and must be treated as the source contract for:

1. import request/build behavior,
2. job/pipeline orchestration,
3. schema contracts,
4. output layout,
5. dependency planning,
6. diagnostics,
7. parity closure against legacy PakGen physics workflows.

## 1. Objective and Parity Target

Objective:

1. Import and cook full physics scene content through the JSON descriptor + manifest pipeline.
2. Produce deterministic loose-cooked outputs consumable by the existing C++ Pak planner/writer.
3. Remove sidecar-only limitation and eliminate demo-only behavior.

Parity target:

1. The manifest-based cooker must represent the same physics authoring surface required by existing PakGen scene+physics specs (including practical coverage exemplified by `Examples/Content/physics_domains_park_spec.yaml` patterns).

Authoring intent:

1. A content author must be able to map human domain concepts directly to JSON entities:
   - "surface behavior" -> `physics-material-descriptor`
   - "collision geometry" -> `collision-shape-descriptor`
   - "serialized Jolt payload/config" -> `physics-resource-descriptor`
   - "scene node to physics bindings" -> `physics-sidecar`
2. A tech artist must be able to read one descriptor and understand:
   - what entity it creates,
   - where it is emitted,
   - what it can reference,
   - what can reference it.

## 2. Scope

In scope:

1. First-class descriptor domains:
   - `physics-resource-descriptor`
   - `physics-material-descriptor`
   - `collision-shape-descriptor`
   - `physics-sidecar` (scene binding orchestration)
2. Schema-first ingestion for all domains.
3. Deterministic virtual-path resolution across mounts (loose + pak + inflight outputs).
4. Full binding coverage for all physics sidecar record families.
5. Manifest DAG dependency orchestration for inter-domain references.
6. Output layout normalization under a dedicated `Physics/` root for materials/shapes/resources, with scene sidecars co-located to target scenes.

Out of scope:

1. Runtime simulation code changes.
2. Automatic extraction from FBX/glTF in this phase.

## 3. Hard Constraints

1. Import path is always `RequestBuilder -> ImportJob -> ImportPipeline`.
2. No physics domain payloads are added to `ImportOptions`.
3. Schema validation is mandatory for descriptor ingress.
4. Manual validation is only for non-schema-enforceable rules.
5. All author references are virtual-path based.
6. No implicit upstream cooking in sidecar jobs.
7. Latest-format-only policy applies; no compatibility branches.

## 4. Authoritative Data Contract Mapping

All physics import contracts must map to `src/Oxygen/Data/PakFormat_physics.h`:

1. `PhysicsResourceDesc`
2. `PhysicsMaterialAssetDesc`
3. `CollisionShapeAssetDesc`
4. `PhysicsSceneAssetDesc`
5. `RigidBodyBindingRecord`
6. `ColliderBindingRecord`
7. `CharacterBindingRecord`
8. `SoftBodyBindingRecord`
9. `JointBindingRecord`
10. `VehicleBindingRecord`
11. `AggregateBindingRecord`

Enum mappings must use the Meta catalog values from:

1. `Oxygen/Core/Meta/Physics/PakPhysics.inc`
2. `Oxygen/Core/Meta/Physics/PakPhysicsShape.inc`

## 5. Canonical Output Layout

Physics outputs are under a dedicated human-readable root, except scene sidecars:

1. `Physics/Materials/*.opmat`
2. `Physics/Shapes/*.ocshape`
3. `Physics/Resources/*.opres`
4. `Physics/Resources/physics.table`
5. `Physics/Resources/physics.data`
6. `<scene_dir>/<scene_stem>.opscene` (co-located with target `.oscene`)

Rules:

1. Paths recorded in loose-cooked index are authoritative; runtime uses recorded paths, not fixed hardcoded folders.
2. Descriptor and resource naming must be deterministic per virtual path/name contract.
3. Sidecar scene pairing remains by semantic target (`target_scene_key`) and exact co-location with the target scene path.

## 6. Descriptor Domains

## 6.1 `physics-resource-descriptor`

Purpose:

1. Cook external physics binary payloads into `physics.table`/`physics.data`.
2. Emit a resolvable sidecar metadata file (`.opres`) for author-facing virtual-path references.

Schema:

1. `oxygen.physics-resource-descriptor.schema.json`

Required fields:

1. `source` (path to binary payload)
2. `format` (maps to `PhysicsResourceFormat`)

Optional fields:

1. `name`
2. `virtual_path`

Manual validation:

1. source file existence and readability,
2. format/payload compatibility checks where format-specific invariants exist,
3. dedup/collision behavior follows the universal importer policy via `ImportOptions::dedup_collision_policy`,
4. canonical `.opres` path enforcement for deduped resources (`same physics resource index` must map to exactly one canonical virtual path).

Artifacts:

1. `Physics/Resources/physics.table`
2. `Physics/Resources/physics.data`
3. `Physics/Resources/<name>.opres`

## 6.2 `physics-material-descriptor`

Purpose:

1. Emit `PhysicsMaterialAssetDesc` assets (`.opmat`).

Schema:

1. `oxygen.physics-material-descriptor.schema.json`

Fields map to:

1. `friction`
2. `restitution`
3. `density`
4. `combine_mode_friction`
5. `combine_mode_restitution`

Artifacts:

1. `Physics/Materials/<name>.opmat`

## 6.3 `collision-shape-descriptor`

Purpose:

1. Emit `CollisionShapeAssetDesc` assets (`.ocshape`).

Schema:

1. `oxygen.collision-shape-descriptor.schema.json`

Shape contract:

1. Discriminated `shape_type`.
2. Common fields:
   - local transform
   - collision layers/masks
   - `material_ref` (virtual path to `.opmat`)
3. Shape params map to `ShapeParams`.
4. Optional `payload_ref` (virtual path to `.opres`) for payload-backed shape variants.

Artifacts:

1. `Physics/Shapes/<name>.ocshape`

## 6.4 `physics-sidecar`

Purpose:

1. Emit `PhysicsSceneAssetDesc` sidecars (`.opscene`) that bind scene nodes to physics component tables.

Schema:

1. `oxygen.physics-sidecar.schema.json` (expanded and normalized)

Binding families (all supported):

1. `rigid_bodies`
2. `colliders`
3. `characters`
4. `soft_bodies`
5. `joints`
6. `vehicles`
7. `aggregates`

Reference model:

1. `shape_ref` (virtual path to `.ocshape`)
2. `material_ref` (virtual path to `.opmat`)
3. `constraint_ref` (virtual path to `.opres`)

No author-facing numeric resource index fields are allowed in schema contracts.

Artifact:

1. `<target_scene_dir>/<target_scene_stem>.opscene` (exactly beside target `.oscene`)

## 6.5 Canonical JSON Examples

Example `physics-resource-descriptor`:

```json
{
  "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.physics-resource-descriptor.schema.json",
  "name": "park.hinge_joint_a",
  "format": "jolt_constraint_binary",
  "source": "Examples/Content/physics/bin/park_hinge_joint_a.jphbin",
  "virtual_path": "/.cooked/Physics/Resources/park_hinge_joint_a.opres"
}
```

Example `physics-material-descriptor`:

```json
{
  "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.physics-material-descriptor.schema.json",
  "name": "ground",
  "friction": 0.95,
  "restitution": 0.05,
  "density": 1800.0,
  "combine_mode_friction": "max",
  "combine_mode_restitution": "average"
}
```

Example `collision-shape-descriptor` (primitive):

```json
{
  "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.collision-shape-descriptor.schema.json",
  "name": "floor_box",
  "shape_type": "box",
  "material_ref": "/.cooked/Physics/Materials/ground.opmat",
  "half_extents": [25.0, 0.5, 25.0]
}
```

Example `collision-shape-descriptor` (payload-backed):

```json
{
  "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.collision-shape-descriptor.schema.json",
  "name": "vehicle_chassis_hull",
  "shape_type": "convex_hull",
  "material_ref": "/.cooked/Physics/Materials/steel.opmat",
  "payload_ref": "/.cooked/Physics/Resources/vehicle_chassis_hull.opres"
}
```

Example `physics-sidecar`:

```json
{
  "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.physics-sidecar.schema.json",
  "target_scene_virtual_path": "/.cooked/Scenes/park.oscene",
  "bindings": {
    "rigid_bodies": [
      {
        "node_index": 1,
        "shape_ref": "/.cooked/Physics/Shapes/floor_box.ocshape",
        "material_ref": "/.cooked/Physics/Materials/ground.opmat",
        "body_type": "static"
      }
    ],
    "joints": [
      {
        "node_index_a": 4,
        "node_index_b": 5,
        "constraint_ref": "/.cooked/Physics/Resources/park_hinge_joint_a.opres"
      }
    ]
  }
}
```

Example manifest DAG (all domains):

```json
{
  "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.import-manifest.schema.json",
  "defaults": {
    "physics_resource_descriptor": {
      "output": "Examples/Content/.cooked"
    },
    "physics_material_descriptor": {
      "output": "Examples/Content/.cooked"
    },
    "collision_shape_descriptor": {
      "output": "Examples/Content/.cooked"
    },
    "physics_sidecar": {
      "output": "Examples/Content/.cooked"
    }
  },
  "jobs": [
    {
      "id": "joint_blob",
      "type": "physics-resource-descriptor",
      "source": "Examples/Content/physics/bin/park_hinge_joint_a.jphbin",
      "name": "park_hinge_joint_a",
      "format": "jolt_constraint_binary"
    },
    {
      "id": "mat_ground",
      "type": "physics-material-descriptor",
      "source": "Examples/Content/physics/ground.material.json"
    },
    {
      "id": "shape_floor",
      "type": "collision-shape-descriptor",
      "source": "Examples/Content/physics/floor.shape.json",
      "depends_on": ["mat_ground"]
    },
    {
      "id": "park_sidecar",
      "type": "physics-sidecar",
      "source": "Examples/Content/physics/park.physics.json",
      "depends_on": ["joint_blob", "shape_floor"]
    }
  ]
}
```

Repository-backed examples:

1. Full four-domain descriptor sample:
   - `Examples/Content/physics/import-manifest.physics.json`
   - `Examples/Content/physics/README.md`
2. Mixed real-content supersession sample (scene/geo/material/texture/input/script
   + physics):
   - `Examples/Content/full-import/import-manifest.json`

## 6.6 Authoring Contract Tables

## 6.6.1 Domain Concept Map

| Human Concept | Descriptor Domain | Output Artifact | Runtime/Pak Target |
| --- | --- | --- | --- |
| Surface physical behavior | `physics-material-descriptor` | `Physics/Materials/*.opmat` | `PhysicsMaterialAssetDesc` |
| Collision primitive/compound/mesh shape | `collision-shape-descriptor` | `Physics/Shapes/*.ocshape` | `CollisionShapeAssetDesc` |
| Serialized backend payload blob | `physics-resource-descriptor` | `Physics/Resources/*.opres` + `physics.table/data` entries | `PhysicsResourceDesc` |
| Scene-level binding orchestration | `physics-sidecar` | `<scene_dir>/<scene_stem>.opscene` (beside `.oscene`) | `PhysicsSceneAssetDesc` + binding tables |

## 6.6.2 `physics-resource-descriptor` Fields

| Field | Type | Required | Default | Constraints | Notes |
| --- | --- | --- | --- | --- | --- |
| `$schema` | string | no | none | valid URI/path | Author/editor schema mapping |
| `name` | string | conditional | source stem | non-empty | Required when `virtual_path` omitted |
| `virtual_path` | string | no | derived from `name` | canonical virtual path | If set, determines emitted `.opres` path |
| `source` | string | yes | none | file must exist/readable | Binary payload source |
| `format` | enum | yes | none | one of schema values | Maps to `PhysicsResourceFormat` |

`format` author values:

1. `jolt_shape_binary`
2. `jolt_constraint_binary`
3. `jolt_soft_body_shared_settings_binary`

## 6.6.3 `physics-material-descriptor` Fields

| Field | Type | Required | Default | Constraints | Maps To |
| --- | --- | --- | --- | --- | --- |
| `$schema` | string | no | none | valid URI/path | schema self-ref |
| `name` | string | conditional | source stem | non-empty | Required when `virtual_path` omitted |
| `virtual_path` | string | no | derived from `name` | canonical virtual path | Optional explicit output path |
| `friction` | number | no | `0.5` | `>= 0` | `PhysicsMaterialAssetDesc::friction` |
| `restitution` | number | no | `0.0` | `>= 0` | `PhysicsMaterialAssetDesc::restitution` |
| `density` | number | no | `1000.0` | `> 0` | `PhysicsMaterialAssetDesc::density` |
| `combine_mode_friction` | enum | no | `average` | enum value | `combine_mode_friction` |
| `combine_mode_restitution` | enum | no | `average` | enum value | `combine_mode_restitution` |

Combine mode values:

1. `average`
2. `min`
3. `max`
4. `multiply`

## 6.6.4 `collision-shape-descriptor` Common Fields

| Field | Type | Required | Default | Constraints | Maps To |
| --- | --- | --- | --- | --- | --- |
| `$schema` | string | no | none | valid URI/path | schema self-ref |
| `name` | string | conditional | source stem | non-empty | Required when `virtual_path` omitted |
| `virtual_path` | string | no | derived from `name` | canonical virtual path | Optional explicit output path |
| `shape_type` | enum | yes | none | enum value | `CollisionShapeAssetDesc::shape_type` |
| `material_ref` | string | yes | none | resolves to `.opmat` | `material_ref` index |
| `payload_ref` | string | conditional | none | resolves to `.opres` | Required for payload-backed types |
| `local_position` | vec3 | no | `[0,0,0]` | numeric array length 3 | `local_position` |
| `local_rotation` | quat | no | `[0,0,0,1]` | numeric array length 4 | `local_rotation` |
| `local_scale` | vec3 | no | `[1,1,1]` | numeric array length 3 | `local_scale` |
| `is_sensor` | bool | no | `false` | boolean | `is_sensor` |
| `collision_own_layer` | uint64 | no | `1` | `0..2^64-1` | `collision_own_layer` |
| `collision_target_layers` | uint64 | no | `18446744073709551615` | `0..2^64-1` | `collision_target_layers` |

## 6.6.5 `collision-shape-descriptor` Shape-Type Params

| `shape_type` | Required Params | Payload Requirement |
| --- | --- | --- |
| `sphere` | `radius` | no |
| `capsule` | `radius`, `half_height` | no |
| `box` | `half_extents` (vec3) | no |
| `cylinder` | `radius`, `half_height` | no |
| `cone` | `radius`, `half_height` | no |
| `plane` | `normal` (vec3), `distance` | no |
| `world_boundary` | `boundary_mode`, `limits_min` (vec3), `limits_max` (vec3) | no |
| `compound` | compound children payload structure | optional/depends on schema branch |
| `convex_hull` | none | yes (`payload_ref`) |
| `triangle_mesh` | none | yes (`payload_ref`) |
| `height_field` | none | yes (`payload_ref`) |

## 6.6.6 `physics-sidecar` Top-Level Fields

| Field | Type | Required | Default | Constraints |
| --- | --- | --- | --- | --- |
| `$schema` | string | no | none | valid URI/path |
| `target_scene_virtual_path` | string | yes | none | canonical virtual path to `.oscene` |
| `bindings` | object | yes | none | contains binding arrays below |

Emission rule:

1. For `target_scene_virtual_path = /.../name.oscene`, emitted sidecar path is exactly `/.../name.opscene`.

## 6.6.7 Binding Record Fields

Rigid body:

| Field | Type | Required | Default | Notes |
| --- | --- | --- | --- | --- |
| `node_index` | uint32 | yes | none | target scene node |
| `shape_ref` | string | yes | none | `.ocshape` ref |
| `material_ref` | string | yes | none | `.opmat` ref |
| `body_type` | enum | no | `static` | static/dynamic/kinematic |
| `motion_quality` | enum | no | `discrete` | discrete/linear_cast |
| `collision_layer` | uint16 | no | `0` | layer |
| `collision_mask` | uint32 | no | `4294967295` | mask |
| `mass` | number | no | `0.0` | 0 means infer |
| `linear_damping` | number | no | `0.05` |  |
| `angular_damping` | number | no | `0.05` |  |
| `gravity_factor` | number | no | `1.0` |  |
| `initial_activation` | bool | no | `true` |  |
| `is_sensor` | bool | no | `false` |  |

Collider:

| Field | Type | Required | Default |
| --- | --- | --- | --- |
| `node_index` | uint32 | yes | none |
| `shape_ref` | string | yes | none |
| `material_ref` | string | yes | none |
| `collision_layer` | uint16 | no | `0` |
| `collision_mask` | uint32 | no | `4294967295` |

Character:

| Field | Type | Required | Default |
| --- | --- | --- | --- |
| `node_index` | uint32 | yes | none |
| `shape_ref` | string | yes | none |
| `mass` | number | no | `80.0` |
| `max_slope_angle` | number | no | `0.7854` |
| `step_height` | number | no | `0.3` |
| `max_strength` | number | no | `100.0` |
| `collision_layer` | uint16 | no | `0` |
| `collision_mask` | uint32 | no | `4294967295` |

Soft body:

| Field | Type | Required | Default |
| --- | --- | --- | --- |
| `node_index` | uint32 | yes | none |
| `settings_ref` | string | yes | none |
| `cluster_count` | uint32 | no | `0` |
| `stiffness` | number | no | `0.0` |
| `damping` | number | no | `0.0` |
| `edge_compliance` | number | no | `0.0` |
| `shear_compliance` | number | no | `0.0` |
| `bend_compliance` | number | no | `1.0` |
| `tether_mode` | enum | no | `none` |
| `tether_max_distance_multiplier` | number | no | `1.0` |

Soft-body binding validation:

1. `cluster_count` must be greater than zero.
2. `settings_ref` must resolve to a physics resource with format
   `jolt_soft_body_shared_settings_binary`.

Joint:

| Field | Type | Required | Default | Notes |
| --- | --- | --- | --- | --- |
| `node_index_a` | uint32 | yes | none | body A |
| `node_index_b` | uint32/null | yes | none | body B or world sentinel |
| `constraint_ref` | string | yes | none | `.opres` ref with constraint format |

Vehicle:

| Field | Type | Required | Default |
| --- | --- | --- | --- |
| `node_index` | uint32 | yes | none |
| `constraint_ref` | string | yes | none |

Vehicle hydration contract:

1. `node_index` is the chassis node and must resolve to a dynamic rigid-body binding.
2. Wheel topology is resolved from dynamic rigid-body descendants of the chassis node.
3. At least two distinct wheel rigid bodies must resolve.
4. `constraint_ref` must resolve to `.opres` with `jolt_constraint_binary` format and is mandatory for vehicle binding validity.

Aggregate:

| Field | Type | Required | Default |
| --- | --- | --- | --- |
| `node_index` | uint32 | yes | none |
| `max_bodies` | uint32 | no | `0` |
| `filter_overlap` | bool | no | `true` |
| `authority` | enum | no | `simulation` |

## 6.6.8 Cardinality and Invariants

1. At most one `rigid_body` per `node_index`.
2. At most one `character` per `node_index`.
3. At most one `soft_body` per `node_index`.
4. At most one `vehicle` per `node_index`.
5. At most one `aggregate` per `node_index`.
6. `colliders` can be many per node.
7. `joints` can be many.
8. All `node_index` values must be within target scene node bounds.
9. All refs must resolve by type.

## 7. Reference Resolution and Identity Rules

All reference resolution is deterministic:

1. normalize canonical virtual path,
2. resolve against inflight outputs then mounted roots by precedence,
3. reject ambiguity,
4. verify expected type.

Type checks:

1. `shape_ref` -> `AssetType::kCollisionShape`
2. `material_ref` -> `AssetType::kPhysicsMaterial`
3. `constraint_ref`/`payload_ref` -> `.opres` metadata pointing to `PhysicsResourceDesc` entry with expected format

Target scene checks:

1. `target_scene_virtual_path` must resolve to a scene descriptor,
2. `target_scene_key` must be read and embedded,
3. node indices in sidecar bindings must be in-range for target node count.

Source-domain consistency:

1. resolved referenced assets/resources must belong to resolvable source context for current import session.
2. mismatch errors are emitted explicitly.
3. this source-domain lock is intentional design (`physics.sidecar.reference_source_mismatch`) to prevent accidental cross-source binding mismatch and non-deterministic packaging.

## 8. Manifest and DAG Contract

Manifest supports four physics job types:

1. `physics-resource-descriptor`
2. `physics-material-descriptor`
3. `collision-shape-descriptor`
4. `physics-sidecar`

Rules:

1. `id` required for descriptor jobs.
2. `depends_on` optional explicit dependencies.
3. key whitelist enforced per type.
4. defaults + job overrides precedence:
   - manifest defaults < job object settings < CLI explicit overrides
   - when CLI override exists, it wins for all domains consistently
5. dependency collector infers additional edges from virtual-path refs when producer jobs are in same manifest.
6. cycles and unresolved in-manifest producer mappings are hard failures.

## 9. Job/Pipeline Architecture

Each domain has dedicated surfaces:

1. `*ImportSettings`
2. `*ImportRequestBuilder`
3. `*ImportJob`
4. `*ImportPipeline`

Shared internals:

1. schema diagnostic bridge helper,
2. physics reference resolver helper,
3. sidecar scene resolver helper,
4. physics resource descriptor emitter/parser (`.opres`),
5. existing session emit/finalize/index machinery.

Ownership rule:

1. containers/orchestration jobs do not emit `.opres` directly;
2. resource-domain job owns physics resource emission and `.opres` side effect.

## 10. Validation Policy

Schema-first validation (mandatory):

1. object shape and required fields,
2. enum/range constraints,
3. discriminator structure,
4. `additionalProperties: false` throughout descriptor schemas.

Manual validation (only where needed):

1. cross-file reference existence/type checks,
2. target scene and node bounds checks,
3. duplicate singleton binding constraints per node,
4. ambiguity and source mismatch checks,
5. payload-format compatibility checks.

## 11. Diagnostics Contract

Diagnostic namespaces:

1. `physics.resource.*`
2. `physics.material.*`
3. `physics.shape.*`
4. `physics.sidecar.*`
5. `physics.manifest.*`

Examples of required diagnostics:

1. `physics.resource.source_missing`
2. `physics.resource.virtual_path_collision`
3. `physics.material.schema_validation_failed`
4. `physics.shape.material_ref_unresolved`
5. `physics.shape.payload_ref_format_mismatch`
6. `physics.sidecar.target_scene_missing`
7. `physics.sidecar.node_ref_out_of_bounds`
8. `physics.sidecar.shape_ref_not_collision_shape`
9. `physics.sidecar.constraint_ref_unresolved`
10. `physics.manifest.key_not_allowed`

Diagnostics must remain concise and author-helpful (no ocean boiling).

## 12. Testing Requirements

Required test layers:

1. schema tests for each physics schema,
2. request-builder tests per domain,
3. job/pipeline tests per domain (success + canonical failures),
4. manifest tests (defaults, overrides, DAG, key whitelist),
5. integration tests for full scene+physics flow,
6. pak planner/writer inclusion tests for emitted physics outputs.

Representative integration scenario:

1. resource blobs -> shapes/materials -> sidecar -> scene pack inclusion.

## 13. PakGen Supersession Alignment

This spec defines the physics track for PakGen supersession:

1. all physics content authoring must be representable through descriptor domains + manifest DAG,
2. no PakGen-only physics behavior remains required for production workflows,
3. parity closure is evidence-based via tests and representative content scenarios.

Representative parity evidence set:

1. Legacy park-spec concept coverage (`physics_domains_park_spec.yaml`) is
   represented by descriptor workflows and tests:
   - physics resources (`.opres`): `PhysicsResourceDescriptor*` tests
   - physics materials (`.opmat`): `PhysicsMaterialDescriptor*` tests
   - collision shapes (`.ocshape`): `CollisionShapeDescriptor*` tests
   - scene bindings (`.opscene`): `Physics*` sidecar tests
2. Concrete repository examples:
   - `Examples/Content/physics/import-manifest.physics.json` (all four physics
     domains, including all seven sidecar binding families).
   - `Examples/Content/full-import/import-manifest.json` (mixed-domain
     supersession scenario used in runtime validation).

## 14. Completion Criteria

Physics cook design is complete when:

1. all four physics domains are implemented end-to-end,
2. output layout is normalized under `Physics/...` for materials/shapes/resources, and scene sidecars are emitted beside their target scenes,
3. schema-first validation is systematic,
4. DAG orchestration is deterministic and tested,
5. emitted outputs are consumed by C++ pak flow without special-case hacks,
6. implementation plan (`design/cook_physics_impl.md`) reaches 100% with evidence.

## 15. Pak Format and Footer Contracts (Parity with `pak_physics`)

1. Pak format version for physics domain is v7.
2. Footer carries:
   - `physics_region`
   - `physics_resource_table`
3. Physics cook must never produce orphan table/data pair states.
4. All emitted physics resources are representable through `PhysicsResourceDesc`.

## 16. ABI and Serialization Contracts

1. Serialized physics structs are packed and size-asserted by `PakFormat_physics.h`.
2. Persisted enum values are sourced from Core Meta catalogs; no duplicated literals.
3. New or changed persisted fields require explicit reserved-space policy and ABI update rationale.
4. Wire format is little-endian; non-little-endian requires byte-swapping in loader path.

## 17. Versioning and Migration Matrix

| Container Version | Physics Resources | Physics Sidecar | Expected Behavior |
| --- | --- | --- | --- |
| v6 | not present | not present | legacy behavior without physics sidecar resources |
| v7 | present | present | full physics import/hydration path enabled |

Rules:

1. v6 content must not pretend physics v7 features exist.
2. v7 load with missing required physics references is hard error.
3. scene/sidecar key mismatch is hard error.

## 18. End-to-End Hydration and Authority Contracts

Storage/cook/load pipeline:

1. offline authoring -> cooker emits scene + physics descriptors/resources.
2. content loading reconstructs scene and physics descriptors independently.
3. hydration binds physics to scene nodes by indices/keys.

Hydration actors:

1. base scene hydrator triggers physics-sidecar hydration after scene node graph exists.
2. C++ game modules can attach additional runtime physics entities via physics APIs.
3. Lua scripting uses constrained gameplay APIs; scripts do not rewrite cooked source-of-truth descriptors.

Phase boundaries:

1. scene build/load,
2. physics hydration,
3. runtime ownership phases (`kGameplay`, `kFixedSimulation`, `kSceneMutation`) with strict authority splits.

## 19. Backend Payload Versioning and Integrity

1. Physics payload blobs include internal payload-family/version headers.
2. Cooker validates payload family against declared descriptor `format`.
3. Integrity policy:
   - strict mode: hash/version mismatch is hard error,
   - permissive dev mode: warning and configurable skip/fail behavior.
4. Production defaults to strict integrity checks.

## 20. Diagnostics Parity Catalog

Import-time diagnostics (examples):

1. `physics.resource.source_missing`
2. `physics.resource.format_invalid`
3. `physics.resource.virtual_path_collision`
4. `physics.material.schema_validation_failed`
5. `physics.shape.payload_ref_format_mismatch`
6. `physics.sidecar.target_scene_missing`
7. `physics.sidecar.target_scene_mismatch`
8. `physics.sidecar.node_ref_out_of_bounds`
9. `physics.sidecar.duplicate_binding`
10. `physics.sidecar.shape_ref_not_collision_shape`
11. `physics.sidecar.material_ref_not_physics_material`
12. `physics.sidecar.constraint_ref_unresolved`
13. `physics.manifest.key_not_allowed`
14. `physics.manifest.dependency_cycle`
15. `import.dedup_collision.physics`

Runtime/load-time diagnostics parity (from `pak_physics` contract):

1. `physics_scene.target_scene_missing`
2. `physics_scene.target_scene_mismatch`
3. `physics_scene.node_index_out_of_range`
4. `physics_scene.duplicate_binding`
5. `physics_scene.shape_asset_missing`
6. `physics_scene.material_asset_missing`
7. `physics_scene.resource_index_out_of_bounds`
8. `physics_scene.unsupported_record_version`
9. `physics_scene.invalid_record_size`

## 21. Conformance Matrix to `design/pak_physics.md`

| `pak_physics` Section | Covered in this spec | Notes |
| --- | --- | --- |
| Global format evolution (`v7`) | yes | sections 15, 17 |
| Physics resource/material/shape descriptors | yes | sections 6.1-6.3, 6.6 |
| Physics sidecar + all binding families | yes | sections 6.4, 6.6.7, 6.6.8 |
| Tooling and pipeline impact | yes | sections 8, 9, 12 |
| ABI contracts (packing/enums/endianness) | yes | section 16 |
| Versioning and migration behavior | yes | section 17 |
| End-to-end hydration/authority phases | yes | section 18 |
| Payload versioning/integrity | yes | section 19 |
| Diagnostics contract | yes | section 20 |
| Acceptance criteria/non-goals | yes | sections 14, 22 |

## 22. Explicit Non-Goals

1. Networked runtime authoring protocol design.
2. Cross-backend portable binary payload ABI.
3. Runtime fallback to render-bounds-derived physics in shipping paths.
