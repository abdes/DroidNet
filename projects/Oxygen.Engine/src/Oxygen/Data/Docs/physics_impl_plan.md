# Physics Data Architecture Implementation Plan

This plan is the migration contract for `physics.md`. It is ordered, blocking, and
zero-shim: no legacy identity, schema, cooker, loader, or runtime fallback paths
may remain once all phases are complete.

Status: `in_progress` until every phase exit gate has code + docs + validation evidence.
Phase status summary: **Phases 1-4 complete; Phase 5 pending**.
Phase scope note (2026-03-06): former Phases 6-9 were folded into Phases 3-5 so
the implementation reaches fully working functional parity by the end of Phase 5.

## Phase 1 Progress Snapshot (Tracking Only)

Status: `complete` (2026-03-06).

Scope delivered:

- Deterministic identity is enforced for asset keys (`AssetKey::FromVirtualPath`);
  random GUID identity generation and `AssetKeyPolicy` branches were removed.
- Canonical virtual-path validation is enforced at cooker/content ingestion points,
  including mount-root identifier rules (`/.cooked` plus any valid identifier).
- Resolver precedence and tombstone masking behavior are covered and retained.

Validation evidence summary:

- Build/test evidence for the Phase 1 change set is recorded in this plan and
  includes focused content/cooker/runtime suites passing in `out/build-vs/bin/Debug`.
- Search audits recorded in this plan show no remaining production references to
  removed identity APIs/symbols (`GenerateAssetGuid`, `AssetKeyPolicy`,
  deterministic-key free-function aliases).

Remaining delta to Phase 1 exit gate:

- None.

## Non-Negotiable Completion Protocol

- [ ] A phase is only marked complete when implementation exists in code.
- [ ] Required docs/plans are updated in the same change set.
- [ ] Validation evidence is attached (tests run, or explicit unvalidated delta).
- [ ] If validation is not executed, phase status remains `in_progress`.
- [ ] No backward-compatibility shims, dual-write, or parallel legacy paths unless explicitly approved.
- [ ] If implementation scope is found insufficient, update design + plan docs before continuing code changes.

## Coverage Matrix (Design -> Migration Track)

| `physics.md` design area | Mandatory migration outcome | Phase |
| --- | --- | --- |
| Asset identity (canonical virtual path + deterministic `AssetKey`) | Canonical path contract enforced, resolver-driven keys, UUID path removed | 1 |
| L1 schema field completeness and L2 struct field inventory | Every schema property traceable to design; struct fields named/typed per spec; SHA-256 hash slots 32 bytes; backend scalar unions and trailing-array navigation fields present | 2 |
| L2 artifact taxonomy and ABI finalization | Fixed-layout descriptors, trailing-array policy, finalized backend scalar unions, strict ABI asserts, packing discipline audit, golden-file tests | 2 |
| Cross-artifact references + versioning contracts V-1/V-2/V-3 | Cross-unit references use `AssetKey`; positional indices only within regeneration units; container-level ordinal coupling removed | 3, 4 |
| Integrity (SHA-256 only for artifacts) | Hash slot upgrades plus correct cook-time population/verification and algorithm-use audit | 2, 3, 4 |
| Side-table emission registry (rows 1-14) | Sidecar + physics table/data physical emission matches documented locations | 3 |
| Loose Cooking and PAK Builder contracts | Loose index authority, incremental-safe recook updates, PAK relocation without recook | 3, 4 |
| L3 hydration order and runtime handle policy | Identity guard, dependency-order hydration, backend-specific restore, session-scoped handles, sync write-back | 4 |
| Zero-tolerance cleanup + final release validation | Legacy/JSON fallback removal, determinism, loose-vs-PAK parity, full evidence-backed release gate | 5 |

## Phase 1 - Identity Foundation and Canonical Path Enforcement

- [x] Replace non-deterministic identity generation with deterministic keying:
- [x] Implement `AssetKey = xxHash3-128(UTF-8 canonical virtual path)` (16-byte storage).
- [x] Migrate all `GenerateAssetGuid()` call sites (~36 noted in design) to deterministic path-key derivation (or explicit non-asset `Uuid::Generate()` where asset identity is not involved).
- [x] Delete `GenerateAssetGuid()` and block reintroduction (compile-time guard/lint rule).
- [x] Implement canonical virtual path validation rules from `physics.md`:
- [x] Absolute path (`/`), no empty segments/trailing slash, no `.` or `..`.
- [x] Allowed character set enforcement and dot-in-leaf-only rule.
- [x] Exact case preservation and <=512-byte length enforcement.
- [x] Implement/validate mount-root behavior in resolver:
- [x] Mount root is a valid identifier (`[A-Za-z_][A-Za-z0-9_-]*`) with `/.cooked` retained as core root.
- [x] Source priority order and conflict logging.
- [x] Tombstone masking behavior for patch manifests.

Phase 1 exit gate:

- [x] Identity-related code paths no longer produce random GUIDs.
- [x] Canonical path validation has automated tests (valid/invalid matrix from spec examples).
- [x] Resolver priority/tombstone behavior has deterministic tests.
- [x] `AssetKey` creation API is constrained so canonical-path/resolver identity cannot be bypassed in production identity flows.

## Phase 2 - L1 Schema + `PakFormat_physics.h` 100% Closure (Build Green Required)

Status: `complete` (2026-03-06).

Goal: complete **all** planned `PakFormat_physics.h`/related physics-format ABI
changes in this phase (no deferral), and establish a verified, zero-gap
correspondence between L1 schemas and L2 field vocabulary from `physics.md`.
This phase is not complete unless the updated ABI compiles successfully in the
project build.

**L1 JSON schema gaps to close:**

- [x] `oxygen.physics-material-descriptor.schema.json`: split the single `friction`
  property into `static_friction` and `dynamic_friction` — the design specifies each
  independently at L1.
- [x] `oxygen.collision-shape-descriptor.schema.json`: add a `children` array property
  for compound shapes; each child entry must carry `shape_type`, the full set of
  inline analytic params (`radius`, `half_height`, `half_extents`, `normal`,
  `distance`, `boundary_mode`, `limits_min`, `limits_max`, `payload_ref`), and a
  local transform (`local_position`, `local_rotation`, `local_scale`). Add an
  `allOf` conditional requiring `children` (minItems: 1) when
  `shape_type == "compound"`. Add/verify explicit `is_sensor` (`boolean`) for
  per-shape trigger semantics.
- [x] `oxygen.physics-sidecar.schema.json` — `rigid_body_binding`: add optional
  properties `center_of_mass_override` (vec3), `inertia_tensor_override` (vec3
  diagonal), `max_linear_velocity` (number ≥ 0), `max_angular_velocity` (number ≥ 0),
  `allowed_dof` (object with per-axis boolean flags `translate_x/y/z`,
  `rotate_x/y/z`); add a `backend` discriminated sub-object (discriminant `target`:
  `"jolt"` or `"physx"`) carrying per-backend scalars from the design table (Jolt:
  `num_velocity_steps_override`, `num_position_steps_override`; PhysX:
  `min_velocity_iters`, `min_position_iters`, `max_contact_impulse`,
  `contact_report_threshold`).
- [x] `oxygen.physics-sidecar.schema.json` — `character_binding`: add optional
  properties `step_down_distance`, `skin_width`, `predictive_contact_distance`
  (numbers), `inner_shape_ref` (canonical_ocshape_path); add a `backend`
  discriminated sub-object (Jolt: `penetration_recovery_speed`, `max_num_hits`,
  `hit_reduction_cos_max_angle`; PhysX: `contact_offset`).
- [x] `oxygen.physics-sidecar.schema.json` — `soft_body_binding`: remove `stiffness`
  (no design backing); rename `damping` → `global_damping`; replace
  `jolt_settings_ref` / `physx_settings_ref` with `source_mesh_ref` (canonical
  path to the L1 geometry source); remove `cluster_count` and `settings_scale`
  unless design backing is confirmed; add missing properties `volume_compliance`,
  `pressure_coefficient`, `solver_iteration_count`, `self_collision` (boolean),
  `collision_mesh_ref` (optional canonical path), `pinned_vertices` (array of
  non-negative integers), `kinematic_vertices` (array of non-negative integers);
  add a `backend` discriminated sub-object (Jolt: `velocity_iteration_count`,
  `lra_stiffness_fraction`, `skinned_constraint_enable`; PhysX FEM:
  `youngs_modulus`, `poisson_ratio`, `dynamic_friction`). Note: L1 uses
  `poisson_ratio` for readability; the L2 struct field is `poissons` to mirror
  `PxFEMSoftBodyMaterial::setPoissons()` exactly — this rename is intentional
  and must be documented in the L1→L2 field-mapping checklist. `source_mesh_ref`
  and optional `collision_mesh_ref` are cooker-input provenance/dependency fields;
  they are validated/resolved during import and are not persisted in
  `SoftBodyBindingRecord` unless the design is explicitly revised.
- [x] `oxygen.physics-sidecar.schema.json` — `vehicle_wheel_binding`: add an optional
  `backend` discriminated sub-object (Jolt: `wheel_castor`; PhysX: none).
- [x] `oxygen.physics-sidecar.schema.json` — complete **joint_binding** parity with
  design fields (constraint type, node references/world anchor sentinel, constraint
  space, local frames A/B, limits, spring settings, motor settings, break
  thresholds, collide-connected flag, priority, backend scalars). Remove or deprecate
  any schema key that is not present in `physics.md`.
- [x] `oxygen.physics-sidecar.schema.json` — complete **vehicle_binding** parity with
  design fields: chassis reference, controller type, engine block, transmission block,
  differential list, anti-roll list, and wheel definitions split into topology
  (`vehicle_wheel_binding`) vs simulation settings destined for backend-cooked vehicle
  blob. Legacy direct blob-reference authoring fields are not part of final schema.
- [x] `oxygen.physics-sidecar.schema.json` — `vehicle_binding.controller_type`:
  add/verify explicit required enum (`"wheeled"` | `"tracked"`) and keep it
  engine-neutral (no backend-specific discriminator leakage).
- [x] `oxygen.physics-sidecar.schema.json` — verify **collider_binding** parity with
  design fields, explicitly including `is_sensor` (`boolean`) plus
  `shape_ref/material_ref/collision_layer/collision_mask` (no missing required
  fields; no non-design extras).
- [x] `oxygen.physics-sidecar.schema.json` — verify **aggregate_binding** property set
  matches design exactly (no missing required fields; no non-design extras).
- [x] `oxygen.physics-resource-descriptor.schema.json`: add missing PhysX format tags
  to the `resource_format` enum: `physx_convex_mesh_binary`,
  `physx_triangle_mesh_binary`, `physx_height_field_binary`,
  `physx_constraint_binary`, `physx_vehicle_settings_binary`.

**L2 binary struct field gaps to close (`PakFormat_physics.h`):**

- [x] `PhysicsResourceDesc`: upgrade `uint64_t content_hash` (8 bytes) to
  `uint8_t content_hash[32]` (32-byte SHA-256 slot). Update `static_assert`.
- [x] `PhysicsMaterialAssetDesc`: rename `float friction` to `float static_friction`
  and add `float dynamic_friction`; adjust reserved padding to maintain or explicitly
  declare the new struct size. Update `static_assert`.
- [x] `CollisionShapeAssetDesc`: add/verify `uint32_t is_sensor` for shape-authored
  trigger semantics; adjust reserved/padding and `static_assert` accordingly.
- [x] `PhysicsSceneAssetDesc`: carve `uint8_t target_scene_content_hash[32]` out of
  the existing reserved block (32-byte SHA-256 slot for the paired `.oscene`
  staleness check). Update `static_assert`.
- [x] `ShapeParams::CompoundParams`: replace `uint32_t reserved_u32` with
  `uint32_t child_count` and add `uint32_t child_byte_offset` (self-relative byte
  offset to the trailing child descriptor array per the trailing-array policy in
  `physics.md`).
- [x] `RigidBodyBindingRecord`: add `float com_override[3]`, `uint32_t has_com_override`,
  `float inertia_override[3]`, `uint32_t has_inertia_override`,
  `float max_linear_velocity`, `float max_angular_velocity`,
  `uint32_t allowed_dof_flags`; add `RigidBodyBackendScalars backend_scalars` union.
  Define `RigidBodyBackendScalars` (Jolt: `uint8_t num_velocity_steps_override`,
  `uint8_t num_position_steps_override`; PhysX: `uint8_t min_velocity_iters`,
  `uint8_t min_position_iters`, `float max_contact_impulse`,
  `float contact_report_threshold`). Update `static_assert`.
- [x] `CharacterBindingRecord`: add `float step_down_distance`, `float skin_width`,
  `float predictive_contact_distance`, `AssetKey inner_shape_asset_key`; add
  `CharacterBackendScalars backend_scalars` union. Define `CharacterBackendScalars`
  (Jolt: `float penetration_recovery_speed`, `uint32_t max_num_hits`,
  `float hit_reduction_cos_max_angle`; PhysX: `float contact_offset`). Update
  `static_assert`.
- [x] `ColliderBindingRecord`: add/verify `uint32_t is_sensor`; update serializer/
  loader call sites and `static_assert`.
- [x] `SoftBodyBindingRecord`: replace `jolt_settings_resource_index` +
  `physx_settings_resource_index` with `core::ResourceIndexT topology_resource_index`
  and `PhysicsResourceFormat topology_format`; remove `uint32_t cluster_count` and
  `float stiffness` (no design backing); rename `float damping` →
  `float global_damping`; add trailing array navigation:
  `uint32_t pinned_vertex_count`, `uint32_t pinned_vertex_byte_offset`,
  `uint32_t kinematic_vertex_count`, `uint32_t kinematic_vertex_byte_offset`; add
  `SoftBodyBackendScalars backend_scalars` union. Define `SoftBodyBackendScalars`
  (Jolt: `uint32_t num_velocity_steps`, `uint32_t num_position_steps`,
  `float gravity_factor`; PhysX FEM: `float youngs_modulus`, `float poissons`,
  `float dynamic_friction`). Update `static_assert`.
- [x] `JointBindingRecord`: add `JointBackendScalars backend_scalars` union drawn from
  the reserved block. Define `JointBackendScalars` (Jolt:
  `uint8_t num_velocity_steps_override`, `uint8_t num_position_steps_override`;
  PhysX: `float inv_mass_scale0`, `float inv_mass_scale1`, `float inv_inertia_scale0`,
  `float inv_inertia_scale1`). Update `static_assert`.
- [x] `VehicleBindingRecord`: add/verify engine-neutral `uint32_t controller_type`
  (`Wheeled` vs `Tracked` enum) and rename `wheel_table_offset` →
  `wheel_slice_offset`, `wheel_count` → `wheel_slice_count`; update all read/write
  call sites and `static_assert`.
- [x] `VehicleWheelBindingRecord`: add `VehicleWheelBackendScalars backend_scalars`
  union from the reserved block. Define `VehicleWheelBackendScalars` (Jolt:
  `float wheel_castor`; PhysX: reserved to matching size). Update `static_assert`.
- [x] Perform a full L2 record inventory pass for `ColliderBindingRecord`,
  `JointBindingRecord`, `VehicleBindingRecord`, `VehicleWheelBindingRecord`, and
  `AggregateBindingRecord` to ensure every engine-neutral field required by
  `physics.md` is represented with final naming/units, and any non-design legacy field
  is removed or explicitly captured as a documented deviation.
- [x] Ensure `PhysicsResourceDesc` and related catalog structures have the fields
  required to support the Phase 3 `AssetKey`-keyed lookup contract (no hidden ordinal
  dependency baked into finalized ABI).
- [x] `PakFormat_physics.h` ABI lock tasks (must finish in Phase 2, not Phase 4):
  - [x] Finalize and lock binary layout for all five backend scalar unions:
    `RigidBodyBackendScalars`, `CharacterBackendScalars`,
    `SoftBodyBackendScalars`, `JointBackendScalars`,
    `VehicleWheelBackendScalars` (member order + explicit tail padding policy).
  - [x] Define/finalize `CompoundShapeChildDesc` fixed-size trailing-array element
    struct and verify self-relative navigation from `ShapeParams::CompoundParams`.
    Required child fields:
    - `shape_type`.
    - Inline analytic params block (`radius`, `half_height`, `half_extents`,
      `normal`, `distance`, `boundary_mode`, `limits_min`, `limits_max`).
    - Local transform block (`local_position`, `local_rotation`, `local_scale`).
    - Non-analytic payload locator (`AssetKey` or cooked-shape payload ref) used
      by convex/mesh/height-field children; null/invalid for analytic children.
  - [x] Verify/finalize soft-body trailing-array navigation contract
    (`pinned_vertex_*`, `kinematic_vertex_*`) and vehicle wheel slice contract
    (`wheel_slice_offset`, `wheel_slice_count`) with layout tests.
  - [x] Apply explicit packing discipline and `static_assert` coverage to all physics
    descriptors/records touched by the design:
    `CollisionShapeAssetDesc`, `PhysicsMaterialAssetDesc`, `PhysicsSceneAssetDesc`,
    `PhysicsComponentTableDesc`, `PhysicsResourceDesc`,
    `RigidBodyBindingRecord`, `ColliderBindingRecord`,
    `CharacterBindingRecord`, `SoftBodyBindingRecord`,
    `JointBindingRecord`, `VehicleBindingRecord`,
    `VehicleWheelBindingRecord`, `AggregateBindingRecord`.
  - [x] Add/update golden-file serialization/deserialization fixtures validating
    size/offset/padding/trailing-array invariants for compound shapes, soft-body
    records, and sidecar component tables.
- [x] **Packing discipline audit across all physics structs**: review every
  `reserved` byte array in every struct and union in `PakFormat_physics.h` against
  the following rules, removing or replacing any that do not satisfy them:
  - A `reserved` field is permitted **only** if it falls into one of these three
    categories:
    1. **Explicit union tail padding** — fills the remaining bytes of a backend
       scalar union arm to bring it to the exact size of the largest arm, ensuring
       all alternatives within the `#pragma pack(push,1)` union are the same size.
    2. **Strict alignment requirement** — a subsequent named field mandates an
       alignment that cannot be satisfied without padding, and there is no
       reordering of members that would eliminate it.
    3. **Fixed-size array record** — the struct is a fixed-size record in a flat
       binary array and the padding bytes bring the total to the required power-of-two
       or explicitly documented ABI size.
  - Any `reserved` field that exists only because a prior version left space
    "for future use" without a concrete alignment or sizing justification must be
    removed. The freed space must be consumed by the named fields added in this
    phase; if no new fields are needed, the struct must shrink and its `static_assert`
    must be updated accordingly.
  - Within `#pragma pack(push,1)` structs, implicit compiler padding does not
    exist; every byte is explicit. Explicit `reserved` arrays that duplicate what
    would be implicit padding in a non-packed struct are therefore unnecessary and
    must be removed.
  - All retained padding/reserved fields must carry an inline comment stating which of
    the three permitted categories justifies their presence.
  - **Naming convention** for retained fields:
    - `_pad0`, `_pad1`, … (sequential suffix) for interior alignment padding between
      named members.
    - `_reserved` (no numeric suffix) for the single trailing field that fills the
      tail of a fixed-size struct or the tail of a union arm to match the largest arm.
      Only one `_reserved` field is permitted per struct or union arm; if multiple
      tail-padding fields would be required, consolidate them into one array.
  - Existing `reserved` fields that do not follow this convention must be renamed;
    any that survive the category audit must be renamed in the same change set.

Phase 2 exit gate:

- [x] **100% `PakFormat` completion gate:** no pending `PakFormat_physics.h` ABI
  change remains for any later phase; all format-shape changes required by
  `physics.md` are implemented in this phase.
- [x] Every JSON schema property is traceable to a named field in `physics.md` for its
  asset class; any property absent from the design is either removed or documented in
  a deviation note appended to this plan.
- [x] Every `PakFormat_physics.h` struct field name, type, and unit matches the design
  vocabulary exactly per the L1 Authoring and L2 records sections of `physics.md`.
- [x] All five backend scalar union types (`RigidBodyBackendScalars`,
  `CharacterBackendScalars`, `SoftBodyBackendScalars`, `JointBackendScalars`,
  `VehicleWheelBackendScalars`) are defined with the correct field names from the
  design table and are embedded in their respective binding records.
- [x] All trailing array navigation field pairs (`count` + `byte_offset`) mandated by
  `physics.md` are present: compound shape child array in `ShapeParams::CompoundParams`
  and soft-body pinned/kinematic vertex arrays in `SoftBodyBindingRecord`.
- [x] Vehicle control-mode gate: `VehicleBindingRecord::controller_type` exists and is
  validated as engine-neutral `Wheeled|Tracked` authored intent.
- [x] Sensor coverage gate: per-shape and per-collider sensor fields required by the
  design are explicitly present and mapped (`CollisionShapeAssetDesc::is_sensor`,
  `ColliderBindingRecord::is_sensor`).
- [x] Soft-body source/collision mesh reference contract is explicit: these remain
  L1 cooker inputs (resolved/validated during import) and are intentionally not
  serialized in `SoftBodyBindingRecord` under current design.
- [x] SHA-256 hash slots are 32 bytes (`uint8_t[32]`) in all affected structs:
  `PhysicsResourceDesc::content_hash` and `PhysicsSceneAssetDesc::target_scene_content_hash`.
- [x] ABI verification coverage includes per-record `static_assert` size/offset checks
  and golden-file tests for compound child arrays, soft-body trailing arrays, and
  wheel-slice navigation.
- [x] Build validation (required): all affected headers/translation units compile,
  all format assertions pass, and
  `cmake --build out/build-vs --config Debug -- /clp:ErrorsOnly /nologo` succeeds.
- [x] Audit: `rg` confirms legacy field names are absent from production source and
  schema files (`friction` (singular) in material descriptor, `wheel_table_offset`,
  `jolt_settings_resource_index`, `cluster_count` in soft body, etc.).
- [x] Packing audit: every `reserved` field remaining in `PakFormat_physics.h` has
  an inline comment citing its category (union tail pad, strict alignment, or
  fixed-size record); `rg -n 'reserved' src/Oxygen/Data/PakFormat_physics.h`
  returns no hit without such a comment, confirmed by manual review.
- [x] L1→L2 field-mapping checklist: a table mapping every schema property to the
  corresponding struct field is appended to this plan for each record type modified.

## Phase 2 Historical Snapshot (Pre-Restore Reference Only)

Historical notes captured before restore (non-authoritative; hints only):

- Added/loaded all Phase 2 backend scalar unions and updated all affected binding
  records in `PakFormat_physics.h` + `PakFormatSerioLoaders.h`.
- Updated sidecar schema and importer parsing for rigid/character/soft/joint/wheel
  backend scalar fields and Phase 2 soft-body record vocabulary.
- Updated runtime hydration call sites to compile against renamed records
  (`topology_resource_index`, `topology_format`, `wheel_slice_offset`,
  `wheel_slice_count`).
- Migrated `Examples/Content/scenes/physics_domains` off legacy
  `physics-resource-descriptor` scene coupling; removed obsolete
  `*.physics-resource.json` + `*.jphbin` files and updated manifest/sidecar.

Phase 2 closure validation (rerun 2026-03-06):

- Build:
  - `cmake --build out/build-vs --config Debug -- /clp:ErrorsOnly /nologo`
    -> **success**.
  - `cmake --build out/build-vs --config Debug -- /m:6 /clp:ErrorsOnly /nologo`
    -> **success**.
- Batch import:
  - `out/build-vs/bin/Debug/Oxygen.Cooker.ImportTool.exe --no-tui batch --manifest Examples/Content/scenes/physics_domains/import-manifest.json`
    -> **success**, summary `jobs=35 succeeded=35 failed=0`.
- Tests:
  - `python -m json.tool src/Oxygen/Cooker/Import/Schemas/oxygen.physics-sidecar.schema.json`
    -> **success**.
  - `python -m pytest src/Oxygen/Cooker/Tools/PakGen/tests/test_collision_shape_pack_contract.py src/Oxygen/Cooker/Tools/PakGen/tests/test_packers_sizes.py src/Oxygen/Cooker/Tools/PakGen/tests/test_physics_scene_default_resource_indices.py src/Oxygen/Cooker/Tools/PakGen/tests/test_physics_scene_bindings_roundtrip.py src/Oxygen/Cooker/Tools/PakGen/tests/test_golden_physics_pack_lock.py -q`
    -> **43 passed**.
  - `Oxygen.Cooker.AsyncImportPhysics.Tests.exe --gtest_filter=PhysicsJsonSchemaTest.*`
    -> **5 passed**.
  - `Oxygen.Cooker.ImportToolBatchDag.Tests.exe --gtest_filter=BatchCommandPhysicsDagTest.*`
    -> **9 passed**.
  - `Oxygen.Content.LoadersPhysics.Tests.exe` -> **13 passed**.
  - `Oxygen.Content.AssetLoader.Tests.exe --gtest_filter=AssetLoaderSceneTest.LoadAssetSceneWithPhysicsSidecarLoadsV7`
    -> **1 passed**.
- Audit checks:
  - `rg -n -F "physics-resource-descriptor" Examples/Content/scenes` -> no hits.
  - `rg --files Examples/Content/scenes -g "*.physics-resource.json"` -> no hits.
  - `rg -n -F "jolt_settings_ref" Examples/Content/scenes` -> no hits.
  - `rg -n -F "physx_settings_ref" Examples/Content/scenes` -> no hits.
  - `rg -n -F "cluster_count" Examples/Content/scenes` -> no hits.
  - `rg -n -F "settings_scale" Examples/Content/scenes` -> no hits.
  - `rg -n -F '"friction"' Examples/Content/scenes --glob "*.physics-material.json"`
    -> no hits.

Deferred to historical Phase 5 (current plan: Phase 3-4 scope; not a Phase 2 gate):

- DemoShell runtime can still hard-fail when loading `physics_domains` if required
  resource indices are unresolved for:
  `JointBindingRecord::constraint_resource_index`,
  `VehicleBindingRecord::constraint_resource_index`, and
  `SoftBodyBindingRecord::topology_resource_index`.
- Root cause: replacement resource-generation/emission for these payload indices is
  cooker/runtime integration work and was tracked in historical Phase 5
  (current plan: Phase 3-4).

### L1→L2 Field Mapping Checklist

| L1 schema property | L2 struct field(s) | Notes |
| --- | --- | --- |
| `physics-material-descriptor.static_friction` | `PhysicsMaterialAssetDesc::static_friction` | Direct numeric mapping |
| `physics-material-descriptor.dynamic_friction` | `PhysicsMaterialAssetDesc::dynamic_friction` | Direct numeric mapping |
| `collision-shape-descriptor.is_sensor` | `CollisionShapeAssetDesc::is_sensor` | Shape-authored trigger semantic flag |
| `collision-shape-descriptor.children[*].shape_type` | `CompoundShapeChildDesc::shape_type` | Compound child type tag |
| `collision-shape-descriptor.children[*].(analytic params)` | `CompoundShapeChildDesc::analytic_params` | Inline analytic parameter block |
| `collision-shape-descriptor.children[*].local_position/local_rotation/local_scale` | `CompoundShapeChildDesc::local_*` | Child local transform |
| `collision-shape-descriptor.children[*].payload_ref` | `CompoundShapeChildDesc` non-analytic payload locator (`AssetKey` or cooked payload ref) | Required for non-analytic child types |
| `collider_binding.is_sensor` | `ColliderBindingRecord::is_sensor` | Collider-level trigger override/authoring flag |
| `rigid_body_binding.node_index` | `RigidBodyBindingRecord::node_index` | Direct |
| `rigid_body_binding.shape_ref` | `RigidBodyBindingRecord::shape_asset_key` | Resolved canonical path -> `AssetKey` |
| `rigid_body_binding.material_ref` | `RigidBodyBindingRecord::material_asset_key` | Resolved canonical path -> `AssetKey` |
| `rigid_body_binding.body_type` | `RigidBodyBindingRecord::body_type` | Enum map |
| `rigid_body_binding.motion_quality` | `RigidBodyBindingRecord::motion_quality` | Enum map |
| `rigid_body_binding.collision_layer` | `RigidBodyBindingRecord::collision_layer` | Direct |
| `rigid_body_binding.collision_mask` | `RigidBodyBindingRecord::collision_mask` | Direct |
| `rigid_body_binding.mass` | `RigidBodyBindingRecord::mass` | Direct |
| `rigid_body_binding.linear_damping` | `RigidBodyBindingRecord::linear_damping` | Direct |
| `rigid_body_binding.angular_damping` | `RigidBodyBindingRecord::angular_damping` | Direct |
| `rigid_body_binding.gravity_factor` | `RigidBodyBindingRecord::gravity_factor` | Direct |
| `rigid_body_binding.initial_activation` | `RigidBodyBindingRecord::initial_activation` | bool -> `uint32_t` |
| `rigid_body_binding.is_sensor` | `RigidBodyBindingRecord::is_sensor` | bool -> `uint32_t` |
| `rigid_body_binding.center_of_mass_override` | `RigidBodyBindingRecord::com_override` + `has_com_override` | Optional vec3 + presence flag |
| `rigid_body_binding.inertia_tensor_override` | `RigidBodyBindingRecord::inertia_override` + `has_inertia_override` | Optional vec3 + presence flag |
| `rigid_body_binding.max_linear_velocity` | `RigidBodyBindingRecord::max_linear_velocity` | Direct |
| `rigid_body_binding.max_angular_velocity` | `RigidBodyBindingRecord::max_angular_velocity` | Direct |
| `rigid_body_binding.allowed_dof.*` | `RigidBodyBindingRecord::allowed_dof_flags` | Packed bitfield (`tx/ty/tz/rx/ry/rz`) |
| `rigid_body_binding.backend.*` | `RigidBodyBindingRecord::backend_scalars` | Discriminated by `backend.target` |
| `character_binding.node_index` | `CharacterBindingRecord::node_index` | Direct |
| `character_binding.shape_ref` | `CharacterBindingRecord::shape_asset_key` | Resolved canonical path -> `AssetKey` |
| `character_binding.mass` | `CharacterBindingRecord::mass` | Direct |
| `character_binding.max_slope_angle` | `CharacterBindingRecord::max_slope_angle` | Direct |
| `character_binding.step_height` | `CharacterBindingRecord::step_height` | Direct |
| `character_binding.step_down_distance` | `CharacterBindingRecord::step_down_distance` | Direct |
| `character_binding.max_strength` | `CharacterBindingRecord::max_strength` | Direct |
| `character_binding.skin_width` | `CharacterBindingRecord::skin_width` | Direct |
| `character_binding.predictive_contact_distance` | `CharacterBindingRecord::predictive_contact_distance` | Direct |
| `character_binding.collision_layer` | `CharacterBindingRecord::collision_layer` | Direct |
| `character_binding.collision_mask` | `CharacterBindingRecord::collision_mask` | Direct |
| `character_binding.inner_shape_ref` | `CharacterBindingRecord::inner_shape_asset_key` | Optional canonical path -> `AssetKey` |
| `character_binding.backend.*` | `CharacterBindingRecord::backend_scalars` | Discriminated by `backend.target` |
| `soft_body_binding.node_index` | `SoftBodyBindingRecord::node_index` | Direct |
| `soft_body_binding.edge_compliance` | `SoftBodyBindingRecord::edge_compliance` | Direct |
| `soft_body_binding.shear_compliance` | `SoftBodyBindingRecord::shear_compliance` | Direct |
| `soft_body_binding.bend_compliance` | `SoftBodyBindingRecord::bend_compliance` | Direct |
| `soft_body_binding.volume_compliance` | `SoftBodyBindingRecord::volume_compliance` | Direct |
| `soft_body_binding.pressure_coefficient` | `SoftBodyBindingRecord::pressure_coefficient` | Direct |
| `soft_body_binding.global_damping` | `SoftBodyBindingRecord::global_damping` | Direct |
| `soft_body_binding.restitution` | `SoftBodyBindingRecord::restitution` | Direct |
| `soft_body_binding.friction` | `SoftBodyBindingRecord::friction` | Direct |
| `soft_body_binding.vertex_radius` | `SoftBodyBindingRecord::vertex_radius` | Direct |
| `soft_body_binding.tether_mode` | `SoftBodyBindingRecord::tether_mode` | Enum map |
| `soft_body_binding.tether_max_distance_multiplier` | `SoftBodyBindingRecord::tether_max_distance_multiplier` | Direct |
| `soft_body_binding.solver_iteration_count` | `SoftBodyBindingRecord::solver_iteration_count` | Direct |
| `soft_body_binding.self_collision` | `SoftBodyBindingRecord::self_collision` | bool -> `uint8_t` |
| `soft_body_binding.pinned_vertices` | `SoftBodyBindingRecord::pinned_vertex_count` + `pinned_vertex_byte_offset` | Trailing array count + self-relative byte offset |
| `soft_body_binding.kinematic_vertices` | `SoftBodyBindingRecord::kinematic_vertex_count` + `kinematic_vertex_byte_offset` | Trailing array count + self-relative byte offset |
| `soft_body_binding.backend.target=jolt` | `SoftBodyBindingRecord::topology_format` + `backend_scalars.jolt.*` | Sets `kJoltSoftBodySharedSettingsBinary`; maps velocity iterations |
| `soft_body_binding.backend.target=physx` | `SoftBodyBindingRecord::topology_format` + `backend_scalars.physx.*` | Sets `kPhysXSoftBodySettingsBinary`; maps FEM scalars (`poisson_ratio` -> `poissons`) |
| `soft_body_binding.source_mesh_ref` | No persisted L2 field (transient import input only) | Parsed into importer-local `SoftBodyBindingSource::source_mesh_ref`, resolved/validated as geometry dependency, then consumed to produce soft-body topology blob |
| `soft_body_binding.collision_mesh_ref` | No persisted L2 field (transient import input only) | Parsed into importer-local `SoftBodyBindingSource::collision_mesh_ref`, resolved/validated as optional geometry dependency, then consumed by soft-body cook path |
| `joint_binding.node_index_a` | `JointBindingRecord::node_index_a` | Direct |
| `joint_binding.node_index_b` | `JointBindingRecord::node_index_b` | Supports numeric index or world attachment sentinel |
| `joint_binding.constraint_type`, `constraint_space`, `local_frame_a_*`, `local_frame_b_*`, `limits_lower/limits_upper`, `spring_stiffnesses`, `spring_damping_ratios`, `motor_modes`, `motor_target_velocities`, `motor_target_positions`, `motor_max_forces`, `motor_max_torques`, `motor_drive_frequencies`, `motor_damping_ratios`, `break_force`, `break_torque`, `collide_connected`, `priority` | No persisted L2 field in Phase 2 | Schema-validated authored constraint settings; consumed by Phase 3 backend constraint-blob cook, then bound via `JointBindingRecord::constraint_resource_index` |
| `joint_binding.backend.*` | `JointBindingRecord::backend_scalars` | Discriminated by `backend.target` |
| `vehicle_binding.node_index` | `VehicleBindingRecord::node_index` | Direct |
| `vehicle_binding.controller_type` | `VehicleBindingRecord::controller_type` | Engine-neutral enum (`Wheeled`/`Tracked`) |
| `vehicle_binding.engine`, `vehicle_binding.transmission`, `vehicle_binding.differentials`, `vehicle_binding.anti_roll_bars` | No persisted L2 field in Phase 2 | Schema-validated authored driveline settings; consumed by Phase 3 vehicle-settings blob cook, then bound via `VehicleBindingRecord::constraint_resource_index` |
| `vehicle_binding.wheels[*].node_index` | `VehicleWheelBindingRecord::wheel_node_index` | Vehicle wheel table entry |
| `vehicle_binding.wheels[*].axle_index` | `VehicleWheelBindingRecord::axle_index` | Direct |
| `vehicle_binding.wheels[*].side` | `VehicleWheelBindingRecord::side` | Enum map |
| `vehicle_binding.wheels[*].backend.*` | `VehicleWheelBindingRecord::backend_scalars` | Discriminated by `backend.target` |
| `vehicle_binding.wheels[*]` simulation fields (suspension/travel/curves/radius/etc.) | No persisted L2 field in Phase 2 | Schema-validated authored wheel simulation settings; consumed by Phase 3 vehicle-settings blob cook |
| `vehicle_binding.wheels` slice topology | `VehicleBindingRecord::wheel_slice_offset` + `wheel_slice_count` | Contiguous slice into shared vehicle wheel table |

Deviation notes:

- `joint_binding.constraint_ref` and `vehicle_binding.constraint_ref` were removed
  from L1 schema in Phase 2 to enforce authored-parameter ownership from
  `physics.md`; `constraint_resource_index` remains a cooker-owned output populated
  by Phase 3 backend blob emission.

## Phase 3 Progress Snapshot (Tracking Only)

Status: `in_progress` (2026-03-06).

Scope delivered:

- Sidecar cooker path emits backend payload indices for soft bodies, joints, and
  vehicles into `physics.table`/`physics.data`, and writes those indices to
  `SoftBodyBindingRecord::topology_resource_index`,
  `JointBindingRecord::constraint_resource_index`, and
  `VehicleBindingRecord::constraint_resource_index`.
- Sidecar emission now patches `PhysicsSceneAssetDesc::target_scene_content_hash`
  from the paired `.oscene` descriptor bytes (SHA-256) and preserves deterministic
  wheel-slice ordering (`wheel_slice_offset`, `wheel_slice_count`) in the shared
  vehicle-wheel table.
- Collision-shape import now serializes compound trailing child descriptors
  (`ShapeParams::CompoundParams.{child_count,child_byte_offset}` +
  `CompoundShapeChildDesc[]`) and supports payload-backed non-analytic shapes when
  `payload_ref` is omitted by emitting an authored cooked payload to physics
  table/data.
- Collision-shape payload validation now accepts PhysX shape resource formats where
  compatible with `ShapePayloadType` (`kPhysXConvexMeshBinary`,
  `kPhysXTriangleMeshBinary`, `kPhysXHeightFieldBinary`) in addition to
  `kJoltShapeBinary`.
- Physics resource integrity path now uses SHA-256 bytes end-to-end in
  `CookedPhysicsResourcePayload` and `PhysicsResourceDesc::content_hash`.

Validation evidence summary:

- `Oxygen.Cooker.AsyncImportCollisionShapeDescriptor.Tests.exe`
  `--gtest_filter=CollisionShapeDescriptorJsonSchemaTest.*:CollisionShapeDescriptorImportJobTest.*`
  -> **12 passed**.
- `Oxygen.Cooker.AsyncImportPhysicsResourceDescriptor.Tests.exe`
  `--gtest_filter=PhysicsResourceDescriptorJsonSchemaTest.*:PhysicsResourceDescriptorImportJobTest.*`
  -> **9 passed**.
- `Oxygen.Cooker.AsyncImportPhysics.Tests.exe`
  `--gtest_filter=PhysicsPhase3ClosureTest.*` -> **2 passed**.
- `Oxygen.Cooker.AsyncImportPhysics.Tests.exe` -> **24 passed**.
- `Oxygen.Cooker.ImportToolBatchDag.Tests.exe`
  `--gtest_filter=BatchCommandPhysicsDagTest.*` -> **9 passed**.
- `Oxygen.Content.LoadersPhysics.Tests.exe` -> **13 passed**.
- `cmake --build out/build-vs --config Debug -- /m:6` -> **exit code 0**.
- Scene migration hard-gate audits:
  - `rg -n -F "jolt_settings_ref" Examples/Content/scenes` -> no hits.
  - `rg -n -F "physx_settings_ref" Examples/Content/scenes` -> no hits.
  - `rg -n -F "cluster_count" Examples/Content/scenes` -> no hits.
  - `rg -n -F "settings_scale" Examples/Content/scenes` -> no hits.
  - `rg -n -F '"friction"' Examples/Content/scenes --glob "*.physics-material.json"`
    -> no hits.
  - `rg -n -F "physics-resource-descriptor" Examples/Content/scenes` -> no hits.
  - `rg --files Examples/Content/scenes -g "*.physics-resource.json"` -> no hits.

Remaining delta to Phase 3 exit gate:

- None.

## Phase 3 - Cooker Translation, Reference Contracts, and Loose Emission

- [x] Implement/update cook pipelines for all physics artifact classes in loose mode:
- [x] Enforce schema-first validation for all L1 physics source payloads (manual checks
  only for non-schema-enforceable constraints).
- [x] Apply the L1->L2 mapping checklist from Phase 2 for rigid body, collider,
  character, soft body, joint, vehicle, vehicle wheel, and aggregate records.
- [x] Emit physics materials (`.opmat`) as fixed descriptors with SHA-256 patching.
- [x] Emit collision shapes (`.ocshape`) with analytic-vs-backend-cooked split,
  including compound child trailing-array serialization.
- [x] Preserve sensor semantics in emission: shape-level `is_sensor` writes into
  `CollisionShapeAssetDesc`; collider-level `is_sensor` writes into
  `ColliderBindingRecord`.
- [x] Emit physics sidecars (`.opscene`) with full component-table directory and
  side-table placement matching design rows 1-14 exactly.
- [x] Emit backend-cooked blob classes (`shape`, `constraint`, `vehicle`, `soft-body`)
  into `physics.table` + `physics.data` and populate all corresponding binding payload
  indices (`constraint_resource_index`, `topology_resource_index`, etc.).
- [x] Vehicle emission uses `VehicleBindingRecord::controller_type` to select
  wheeled-vs-tracked authored settings translation and backend cook path.
- [x] Enforce reference/versioning contracts across tooling and serialized outputs:
- [x] V-1: virtual path and `AssetKey` references carry no generation/version stamp.
- [x] V-2: positional indices are confined to regeneration units only.
- [x] V-3: any cross-regeneration-unit reference uses `AssetKey` only.
- [x] Remove scene-authored legacy blob coupling from content where design requires
  authored parameters (no `*.physics-resource.json` dependency for scene-authored
  joint/vehicle/soft-body setup).
- [x] Eliminate container-level ordinal coupling:
- [x] Physics resource descriptor table and container asset catalog are `AssetKey` keyed.
- [x] Incremental loose recook updates one asset without invalidating unrelated entries.
- [x] Emit and patch sidecar staleness fields:
- [x] `target_scene_key`.
- [x] `target_scene_content_hash` (SHA-256 of paired `.oscene` artifact).
- [x] `target_node_count`.
- [x] Ensure loose index (`*.oxlcidx`) remains authoritative:
- [x] Asset entries include key, descriptor-relative path, virtual path, type tag,
  descriptor hash.
- [x] File records include physics table/data and all other referenced resource files.
- [x] Complete integrity migration behavior (hash slot sizes already landed in Phase 2):
- [x] Populate and verify SHA-256 for `AssetHeader::content_hash`,
  `PhysicsResourceDesc::content_hash`, loose index descriptor hashes, and any remaining
  asset-level integrity fields.
- [x] Remove prohibited integrity algorithms (CRC variants, MD5/SHA-1, non-crypto hash
  misuse) for asset-level integrity; keep CRC32 restricted to whole-PAK checksum.
- [x] **Scene migration hard gate (moved from Phase 2):** migrate
  `Examples/Content/scenes/` to the finalized physics schemas with no legacy keys.
  Minimum evidence: `rg` shows no `jolt_settings_ref`, `physx_settings_ref`,
  `cluster_count`, `settings_scale`, legacy singular `"friction"` in
  physics-material descriptors, and no scene manifests requiring legacy
  `physics-resource-descriptor` coupling.

Phase 3 exit gate:

- [x] End-to-end cooker tests cover analytic shape, non-analytic shape, joint blob,
  soft-body blob, and vehicle blob paths.
- [x] Complex-scene fixture validates component directory, wheel table, compound trailing
  child arrays, and soft-body trailing arrays.
- [x] Batch import succeeds for migrated scene manifests with zero legacy coupling.
- [x] No cross-unit positional reference path remains in cooker output.
- [x] All asset-integrity fields are populated/verified as SHA-256 and covered by tests.
- [x] Incremental recook test proves unaffected assets remain stable.

## Phase 4 - PAK Relocation and Runtime Hydration Contract (L3)

- [x] Ensure PAK builder consumes fully cooked loose layout without recooking:
- [x] Read index as source of truth for assets/file records.
- [x] Concatenate descriptor regions and physics resource regions.
- [x] Rewrite offsets from loose-relative to PAK-relative where required.
- [x] Produce self-contained mountable bundle with catalog/directory structures.
- [x] Validate base+patch layering behavior for resolver priority/tombstone semantics.
- [x] Integrate physics module into frame orchestration (fixed simulation and transform
  propagation phases).
- [x] Enforce strict sidecar staleness guard before hydration:
- [x] Hard-fail on `target_scene_key`, `target_scene_content_hash`, or
  `target_node_count` mismatch.
- [x] Hydrate in design dependency order:
  `Shapes -> Materials -> RigidBodies -> Colliders -> Characters -> SoftBodies -> Joints -> Vehicles -> Aggregates`.
- [x] Branch restore path strictly by backend format tag (`Jolt` vs `PhysX` blobs).
- [x] Maintain runtime caches/maps by `AssetKey` and scene-node-index mappings.
- [x] Enforce per-instance override source-of-truth: runtime reads instance values
  exclusively from cooked binding records; no physics instantiation path reads L1 JSON.
- [x] Sensor behavior contract: hydration/runtime paths must honor shape/collider
  `is_sensor` as trigger semantics (overlap reporting without contact impulses).
- [x] Enforce runtime handle policy from design:
- [x] Session-scoped handles only; never persisted.
- [x] No raw backend pointer leakage across L3 abstractions.
- [x] Mid-session invalidation behavior follows backend/owner-responsibility contract.
- [x] Implement simulation-to-scene sync for body and vehicle wheel transforms (and
  soft-body-driven updates where present).

Phase 4 exit gate:

- [x] Loose->PAK roundtrip tests confirm payload preservation (except expected offset
  relocation) and no cooker logic in PAK builder path.
- [x] Runtime loads both loose and PAK outputs with identical physics behavior.
- [x] Hydration tests cover every component table type and backend restore branch
  available in the build.
- [x] Vehicle controller-mode tests cover both `Wheeled` and `Tracked` authored
  records end-to-end (cook -> hydrate -> runtime behavior).
- [x] Mismatch guard tests hard-fail invalid sidecars.
- [x] Audit/tests confirm no runtime L1-JSON fallback path is used during physics
  hydration/instantiation.
- [x] Sensor behavior tests confirm shape/collider `is_sensor` records instantiate
  trigger behavior (overlap events, no contact response).
- [x] Session teardown invalidates handles; object-removal scenarios are covered.
- [x] DemoShell `RenderScene` for `physics_domains` runs without missing payload index
  hydration failures.

## Phase 4 Progress Snapshot (Tracking Only)

Status: `complete` (2026-03-06).

Scope delivered:

- `SceneLoaderService` now enforces strict sidecar identity guard on all three
  invariants (`target_scene_key`, `target_scene_content_hash`, `target_node_count`)
  before hydration and executes hydration in design dependency order.
- Runtime hydration now routes shape/joint/vehicle resource formats by active physics
  backend (`Jolt`/`PhysX`) and rejects backend/format mismatches deterministically.
- Sensor contract is enforced from cooked records: shape-level and collider-level
  `is_sensor` values map to trigger flags during body creation.
- `ImportOptions` now carries a single shared physics backend selector
  (`options.physics.backend`, default `jolt`) wired through request-builder and
  import-manifest ingestion; no duplicate backend enum was introduced.
- Cooker sidecar emission for soft/joint/vehicle resources is backend-cook only
  (`Cook*Blob` flow) with strict backend contract checks and explicit
  `physics.sidecar.backend_mismatch` diagnostics; legacy authored-blob emission
  path is removed from runtime-producing code paths.
- Joint hydration is cooked-only: `SceneLoaderService` forwards cooked constraint
  blobs into `JointDesc`, and `JoltJoints` restores `TwoBodyConstraintSettings`
  from binary blobs (invalid payloads hard-fail with `InvalidArgument`).
- Vehicle hydration now uses guarded, deterministic cooked-binary restore in
  `JoltVehicles` with explicit rejection of legacy `OPHB` payloads and malformed
  blobs before backend object creation; no synthesized fallback settings remain.
- Soft-body trigger interaction contract is stabilized in runtime contact
  validation (`soft-body vs sensor` contacts are ignored), preventing trigger
  activation from injecting soft-body instability.
- Dedicated blob-contract tests were added for joint, soft body, and vehicle
  payload validation as separate test files.

Validation evidence summary:

- Build:
  - `cmake --build out/build-vs --config Debug -- /m:6` -> **exit code 0**.
- Runtime/hydration/physics:
  - `Oxygen.Examples.DemoShell.SceneLoaderServicePhase4.Tests.exe`
    -> **4 passed**.
  - `Oxygen.Physics.Jolt.Tests.exe` -> **95 passed**.
- Cooker/sidecar:
  - `Oxygen.Cooker.AsyncImportPhysics.Tests.exe` -> **25 passed**.
- DemoShell runtime scene smoke:
  - `Oxygen.Examples.RenderScene.exe -v 0 -f 240 -r 30` -> **exit code 0**.
  - Post-run log audit command:
    `rg -n "Deferred physics hydration failed|failed to attach|falling back|invalid/unsupported blob|physics hydration failed|OPHB" out/build-vs/renderscene_phase4.log`
    -> **no matches**.
- Runtime JSON fallback audit:
  - `rg -n "nlohmann|json::" Examples/DemoShell/Services/SceneLoaderService.cpp src/Oxygen/PhysicsModule src/Oxygen/Physics/Jolt src/Oxygen/Physics/System -g"*.cpp" -g"*.h"`
    -> **no matches**.

Remaining delta to Phase 4 exit gate:

- None.

## Phase 5 - Full Working Closure, Cleanup, and Final Release Gate

- [x] Remove legacy/duplicate pipelines bypassing binary descriptors.
- [x] Remove runtime fallback parsing of L1 JSON for physics instantiation.
- [ ] Remove cross-collection ordinal reference remnants.
- [ ] Remove deprecated handle wrappers that violate session-handle policy.
- [ ] Remove compatibility code paths that keep old physics schema alive in production.
- [ ] Same canonical virtual path -> same `AssetKey` across runs/machines.
- [ ] Repeat cooks with unchanged input produce stable descriptor/blob hashes.
- [ ] Scene hydration parity holds across loose and PAK modes.
- [ ] Scenario I (analytic sphere path) passes end-to-end.
- [ ] Scenario II (compound + joint + soft body + vehicle) passes end-to-end.
- [ ] Zero warnings for packing/alignment/serialization in touched physics-content code.
- [ ] `physics.md` and this plan remain aligned after implementation deltas.
- [ ] Any discovered scope correction updates docs first, then implementation.

Task 1 closure evidence (2026-03-07):

- Removed and deleted legacy `physics-resource-descriptor` ingestion:
  schema file, request/settings/builders, async import job, and dedicated tests.
- Removed manifest/import-tool routing and schema acceptance for
  `physics-resource-descriptor` job type.
- Updated collision-shape payload-ref tests to seed `.opres` sidecars directly
  (no legacy import pipeline dependency).
- Validation:
  - `cmake --build out/build-vs --config Debug --target oxygen-cooker Oxygen.Cooker.AsyncImportCollisionShapeDescriptor.Tests Oxygen.Cooker.AsyncImportPhysicsMaterialDescriptor.Tests Oxygen.Cooker.ImportToolBatchDag.Tests -- /m:6` -> **success**.
  - `Oxygen.Cooker.AsyncImportCollisionShapeDescriptor.Tests.exe` -> **20 passed**.
  - `Oxygen.Cooker.AsyncImportPhysicsMaterialDescriptor.Tests.exe` -> **14 passed** (includes legacy-job-type rejection test).
  - `Oxygen.Cooker.ImportToolBatchDag.Tests.exe` -> **9 passed**.

Task 2 closure evidence (2026-03-07):

- Runtime hydration/physics instantiation contains no L1-JSON parse path:
  `rg -n "nlohmann::json|json::parse" src/Oxygen/Physics src/Oxygen/PhysicsModule Examples/DemoShell/Services/SceneLoaderService.cpp -g"*.cpp" -g"*.h"` -> **no matches**.
- Removed vehicle legacy-envelope special-case in runtime restore path:
  `JoltVehicles` no longer carries `OPHB` blob magic branching; restore is
  strict binary-state decode only.
- Validation:
  - `Oxygen.Physics.Jolt.Tests.exe --gtest_filter="*BlobContract*"` -> **4 passed** (`joint/soft-body/vehicle` non-cooked payload rejection contract intact).

Phase 5 final release gate:

- [ ] **Everything fully working:** no known functional gap remains against
  `physics.md` (L1 authoring contract, L2 cooking/layout, PAK relocation, L3 hydration,
  runtime handle policy, and sync behavior).
- [ ] Every Phase 2-5 checklist item has code, docs, and validation evidence.
- [ ] Remaining gaps list is empty.
- [ ] Status may advance from `in_progress` only after all evidence is recorded.

## Historical Phase Numbering (Folded)

Former Phases 6-9 were merged into Phases 3-5 on 2026-03-06 so the plan's
functional completion point is Phase 5. Do not treat 6-9 as active gates unless
a future scope expansion explicitly reintroduces them.
