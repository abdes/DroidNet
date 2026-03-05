# Physics Data Architecture Implementation Plan

This plan is the migration contract for `physics.md`. It is ordered, blocking, and
zero-shim: no legacy identity, schema, cooker, loader, or runtime fallback paths
may remain once all phases are complete.

Status: `in_progress` until every phase exit gate has code + docs + validation evidence.
Phase status summary: **Phases 1-2 complete**; Phases 3-9 pending.

## Phase 1 Progress Snapshot (2026-03-04)

Implemented in this iteration (Phase 1 exit gate met):

- Added deterministic `AssetKey` derivation API in code:
  `AssetKey::FromVirtualPath(virtual_path)` using
  `xxHash3-128(UTF-8 bytes)`.
- Removed cooker free-function minting (`MakeDeterministicAssetKey`) and
  standardized call sites directly on `AssetKey::FromVirtualPath(...)`.
- Added shared canonical virtual-path validator and wired it into resolver and
  cooker/content canonical checks.
- Added canonical-path validation test matrix (valid/invalid examples from
  design) and deterministic path-key tests.
- Removed `AssetKeyPolicy` and all `asset_key_policy`/`kRandom` branches from
  import options, jobs, and pipelines.
- Removed `GenerateAssetGuid()` from `AssetKey.h` and migrated all remaining
  code call sites:
  - asset identity paths now use deterministic canonical-path key derivation.
  - non-asset UUID generation paths now use `Uuid::Generate()`.
- Updated scripting-sidecar tests to deterministic key semantics (same
  canonical path -> same key).
- Added/kept resolver precedence + tombstone tests in
  `VirtualPathResolver_test.cpp`.
- Constrained `AssetKey` creation API:
  - converted `AssetKey` from UUID-wrapper semantics to a dedicated 16-byte
    value type (no `NamedType<Uuid,...>` inheritance).
  - removed constructor-based UUID minting paths.
  - removed free-function identity minting path.
  - standardized on `AssetKey::FromVirtualPath(...)` (minting) and
    `AssetKey::FromBytes(...)` (rehydration).
  - kept `AssetKey` byte storage private; no mutable byte access API was added.

Validation evidence captured in this iteration:

- Focused build validation:
  - `cmake --build out/build-vs --config Debug --target Oxygen.Data.All.Tests`
    -> **success**
  - `cmake --build out/build-vs --config Debug --target oxygen-examples-demoshell`
    -> **success**
  - `cmake --build out/build-vs --config Debug --target oxygen-examples-async`
    -> **success**
  - `cmake --build out/build-vs --config Debug --target oxygen-examples-inputsystem`
    -> **success**
  - `cmake --build out/build-vs --config Debug --target oxygen-examples-lightbench`
    -> **success**
  - `cmake --build out/build-vs --config Debug --target oxygen-examples-multiview`
    -> **success**
  - `cmake --build out/build-vs --config Debug --target oxygen-examples-physics`
    -> **success**
  - `cmake --build out/build-vs --config Debug --target oxygen-examples-texturedcube`
    -> **success**
- Focused runtime tests:
  - `Oxygen.Data.All.Tests.exe --gtest_filter=AssetKey*` -> **2 passed**
  - `Oxygen.Content.LooseCooked.Tests.exe --gtest_filter=VirtualPathResolverTest*` -> **7 passed**
- Regression validation after Phase 1 test-fix completion:
  - `cmake --build out/build-vs --config Debug --target Oxygen.Cooker.AsyncImportScript.Tests` -> **success**
  - `Oxygen.Cooker.AsyncImportScript.Tests.exe --gtest_color=no` -> **78 passed**
  - `Oxygen.Content.AssetLoader.Tests.exe --gtest_filter=AssetLoaderSceneTest.LoadAssetSceneWithPhysicsSidecarLoadsV7` -> **1 passed**
  - `Oxygen.Scripting.Module.Tests.exe --gtest_filter=ScriptingModuleTest.SceneSlotLocalStatePersistsAcrossGameplayFrames` -> **1 passed**
- Post-change audit checks:
  - `rg -n "GenerateAssetGuid" src Examples --glob "!**/*.md"` -> no hits.
  - `rg -n "AssetKeyPolicy::|AssetKeyPolicy|asset_key_policy" src Examples --glob "!**/*.md"` -> no hits.
  - `rg -n "MakeAssetKeyFromCanonicalVirtualPath|MakeDeterministicAssetKey" src Examples --glob "!**/*.md"` -> no hits.
  - `rg -n "AssetKey[^\\n]*Uuid::Generate\\(|AssetKey\\s*\\{\\s*oxygen::Uuid|AssetKey\\s*\\{\\s*Uuid" src Examples --glob "!**/*.md"` -> no hits.
  - `rg -n "as_writable_bytes\\s*\\(\\s*[^\\n]*AssetKey" src Examples --glob "!**/*.md"` -> no hits.

Design alignment note:

- No Phase 1 design deviation was identified relative to `physics.md`; design
  document update is not required for this closure.

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
| L1 schema field completeness and L2 struct field inventory | Every schema property traceable to design; struct fields named and typed per spec; SHA-256 hash slots correctly sized; backend scalar union stubs and trailing-array navigation fields present | 2 |
| Cross-artifact references + versioning contracts V-1/V-2/V-3 | Cross-unit references use `AssetKey`; positional indices only within regeneration units | 3, 5, 7 |
| Integrity (SHA-256 only for artifacts) | All cooked descriptor/resource integrity fields upgraded to 32-byte SHA-256 | 2, 3 |
| L2 artifact taxonomy and ABI finalization | Fixed-layout descriptors, trailing-array policy, finalized backend scalar unions, strict ABI asserts, golden-file tests | 4 |
| Side-table emission registry (rows 1-14) | Sidecar + physics table/data physical emission matches documented locations | 5 |
| Loose Cooking and PAK Builder contracts | Loose index authority, incremental-safe updates, PAK relocation without recook | 5, 6 |
| L3 hydration order and runtime handle policy | Identity guard, dependency-order hydration, backend-specific restore, session-scoped handles | 7 |
| Zero-tolerance cleanup | Legacy/random-key/JSON fallback/index misuse removed | 8 |

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
- [x] Supported roots (`/Engine`, `/Game`, `/.cooked`, `/Pak/<name>`).
- [x] Source priority order and conflict logging.
- [x] Tombstone masking behavior for patch manifests.

Phase 1 exit gate:

- [x] Identity-related code paths no longer produce random GUIDs.
- [x] Canonical path validation has automated tests (valid/invalid matrix from spec examples).
- [x] Resolver priority/tombstone behavior has deterministic tests.
- [x] `AssetKey` creation API is constrained so canonical-path/resolver identity cannot be bypassed in production identity flows.

## Phase 2 - L1 Schema Completeness and L2 Struct Field Alignment

Goal: establish a verified, zero-gap correspondence between the L1 JSON schemas and
the `PakFormat_physics.h` struct field inventory relative to the full specification
in `physics.md`. No cooker or loader implementation is modified; this phase ensures
the schema and struct vocabulary is complete and correct before any downstream phase
attempts to read or write these fields.

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
  `shape_type == "compound"`.
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
  and must be documented in the L1→L2 field-mapping checklist.
- [x] `oxygen.physics-sidecar.schema.json` — `vehicle_wheel_binding`: add an optional
  `backend` discriminated sub-object (Jolt: `wheel_castor`; PhysX: none).
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
- [x] `SoftBodyBindingRecord`: replace `jolt_settings_resource_index` +
  `physx_settings_resource_index` with `core::ResourceIndexT topology_resource_index`
  + `PhysicsResourceFormat topology_format`; remove `uint32_t cluster_count` and
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
- [x] `VehicleBindingRecord`: rename `wheel_table_offset` → `wheel_slice_offset` and
  `wheel_count` → `wheel_slice_count` to match design vocabulary; update all
  read/write call sites.
- [x] `VehicleWheelBindingRecord`: add `VehicleWheelBackendScalars backend_scalars`
  union from the reserved block. Define `VehicleWheelBackendScalars` (Jolt:
  `float wheel_castor`; PhysX: reserved to matching size). Update `static_assert`.
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
- [x] SHA-256 hash slots are 32 bytes (`uint8_t[32]`) in all affected structs:
  `PhysicsResourceDesc::content_hash` and `PhysicsSceneAssetDesc::target_scene_content_hash`.
- [x] Build validation: all affected headers and translation units compile without error;
  all `static_assert` size assertions are updated to reflect new struct sizes.
- [x] Audit: `rg` confirms legacy field names are absent from production source and
  schema files (`friction` (singular) in material descriptor, `wheel_table_offset`,
  `jolt_settings_resource_index`, `cluster_count` in soft body, etc.).
- [x] **Scene migration hard gate (blocker):** Phase 2 cannot be marked complete until
  `Examples/Content/scenes/` is fully migrated to the updated physics schemas and no
  legacy blob-authored physics remains in scene authoring content. Minimum evidence:
  `rg` under `Examples/Content/scenes` shows no legacy soft-body/material keys
  (`"friction"` singular in physics-material descriptors, `jolt_settings_ref`,
  `physx_settings_ref`, `cluster_count`, `settings_scale`, `stiffness`, `damping`);
  no scene manifest entries require `physics-resource-descriptor` for scene-authored
  joint/vehicle/soft-body setup; and batch import succeeds for migrated scene manifests.
- [x] Packing audit: every `reserved` field remaining in `PakFormat_physics.h` has
  an inline comment citing its category (union tail pad, strict alignment, or
  fixed-size record); `rg -n 'reserved' src/Oxygen/Data/PakFormat_physics.h`
  returns no hit without such a comment, confirmed by manual review.
- [x] L1→L2 field-mapping checklist: a table mapping every schema property to the
  corresponding struct field is appended to this plan for each record type modified.

## Phase 2 Progress Snapshot (2026-03-05)

Implemented in this iteration (Phase 2 exit gate met for schema/ABI/migration scope):

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

Validation evidence captured in this iteration:

- Build:
  - `cmake --build out/build-vs --config Debug -- /clp:ErrorsOnly /nologo`
    -> **success**.
- Batch import:
  - `out/build-vs/bin/Debug/Oxygen.Cooker.ImportTool.exe --no-tui batch --manifest Examples/Content/scenes/physics_domains/import-manifest.json`
    -> **success**, summary `jobs=35 succeeded=35 failed=0`.
- Tests:
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

Deferred to Phase 5 (not a Phase 2 gate):

- DemoShell runtime can still hard-fail when loading `physics_domains` if required
  resource indices are unresolved for:
  `JointBindingRecord::constraint_resource_index`,
  `VehicleBindingRecord::constraint_resource_index`, and
  `SoftBodyBindingRecord::topology_resource_index`.
- Root cause: replacement resource-generation/emission for these payload indices is
  cooker/runtime integration work and is tracked in Phase 5.

### L1→L2 Field Mapping Checklist

| L1 schema property | L2 struct field(s) | Notes |
| --- | --- | --- |
| `physics-material-descriptor.static_friction` | `PhysicsMaterialAssetDesc::static_friction` | Direct numeric mapping |
| `physics-material-descriptor.dynamic_friction` | `PhysicsMaterialAssetDesc::dynamic_friction` | Direct numeric mapping |
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
| `soft_body_binding.source_mesh_ref` | `SoftBodyBindingSource::source_mesh_ref` (validation path) | Canonical geometry path validated in Phase 2 cooker; topology resource emission remains Phase 5 |
| `soft_body_binding.collision_mesh_ref` | `SoftBodyBindingSource::collision_mesh_ref` (validation path) | Canonical geometry path validated in Phase 2 cooker; topology resource emission remains Phase 5 |
| `joint_binding.node_index_a` | `JointBindingRecord::node_index_a` | Direct |
| `joint_binding.node_index_b` | `JointBindingRecord::node_index_b` | Supports numeric index or world attachment sentinel |
| `joint_binding.constraint_ref` | `JointBindingRecord::constraint_resource_index` | Optional canonical `.opres` path -> physics resource index |
| `joint_binding.backend.*` | `JointBindingRecord::backend_scalars` | Discriminated by `backend.target` |
| `vehicle_binding.node_index` | `VehicleBindingRecord::node_index` | Direct |
| `vehicle_binding.constraint_ref` | `VehicleBindingRecord::constraint_resource_index` | Optional canonical `.opres` path -> physics resource index |
| `vehicle_binding.wheels[*].node_index` | `VehicleWheelBindingRecord::wheel_node_index` | Vehicle wheel table entry |
| `vehicle_binding.wheels[*].axle_index` | `VehicleWheelBindingRecord::axle_index` | Direct |
| `vehicle_binding.wheels[*].side` | `VehicleWheelBindingRecord::side` | Enum map |
| `vehicle_binding.wheels[*].backend.*` | `VehicleWheelBindingRecord::backend_scalars` | Discriminated by `backend.target` |
| `vehicle_binding.wheels` slice topology | `VehicleBindingRecord::wheel_slice_offset` + `wheel_slice_count` | Contiguous slice into shared vehicle wheel table |

## Phase 3 - Reference and Integrity Contract Migration (Cross-Cutting)

- [ ] Enforce reference contracts across data/model/tooling code:
- [ ] Virtual path and `AssetKey` references carry no generation/version stamp (V-1).
- [ ] Positional indices remain only within regeneration units (V-2).
- [ ] Any cross-regeneration-unit reference is `AssetKey` only (V-3).
- [ ] Eliminate container-level ordinal coupling:
- [ ] Physics resource descriptor table and container asset catalog are `AssetKey` keyed.
- [ ] Incremental loose recook updates one asset without invalidating unrelated indices.
- [ ] Upgrade artifact integrity fields to SHA-256 (32 bytes) where currently narrower
  (hash slot sizes established in Phase 2; this phase ensures correct population and
  verification at cook time):
- [ ] `AssetHeader::content_hash`.
- [ ] `PhysicsResourceDesc` integrity hash.
- [ ] Loose index `AssetEntry` descriptor integrity hash.
- [ ] Any remaining asset-level hash fields in content descriptors/tables.
- [ ] Remove prohibited asset-integrity algorithms (CRC variants, MD5/SHA-1, non-crypto hash misuse).
- [ ] Keep CRC32 limited to whole-container (PAK file-level checksum) only.

Phase 3 exit gate:

- [ ] No cross-unit positional reference path remains in code or serialized layout.
- [ ] All asset-integrity fields are 32-byte SHA-256 and covered by tests.
- [ ] Algorithm usage audit proves prohibited hashes are not used for asset integrity.

## Phase 4 - L2 ABI Finalization (`PakFormat_physics.h` and Related Formats)

- [ ] Lock backend scalar union binary layouts (union definitions established in Phase 2):
- [ ] Apply packing discipline to all five union types — `RigidBodyBackendScalars`,
  `CharacterBackendScalars`, `SoftBodyBackendScalars`, `JointBackendScalars`,
  `VehicleWheelBackendScalars` — with explicit `reserved` bytes and stable member ordering.
- [ ] Finalize trailing-array element struct definitions and serialization contracts:
- [ ] Define `CompoundShapeChildDesc` fixed-size element struct (shape type, inline
  analytic params, local transform offset/rotation/scale); verify compound shape
  trailing-array self-relative offset navigation.
- [ ] Verify `SoftBodyBindingRecord` pinned and kinematic vertex trailing-array
  self-relative offset navigation (navigation fields established in Phase 2).
- [ ] Verify vehicle wheel side-table slice contract (field names aligned in Phase 2)
  with a struct layout test.
- [ ] Apply explicit packing discipline to all fixed-layout descriptors and binding
  records updated in Phase 2:
- [ ] `CollisionShapeAssetDesc`, `PhysicsMaterialAssetDesc`, `PhysicsSceneAssetDesc`, `PhysicsComponentTableDesc`, `PhysicsResourceDesc`.
- [ ] `RigidBodyBindingRecord`, `ColliderBindingRecord`, `CharacterBindingRecord`, `SoftBodyBindingRecord`, `JointBindingRecord`, `VehicleBindingRecord`, `VehicleWheelBindingRecord`, `AggregateBindingRecord`.
- [ ] Stable member ordering, explicit `reserved` bytes, 16-byte multiple sizing where required.
- [ ] Full `static_assert` coverage for size/offset invariants and trailing-array navigation fields.

Phase 4 exit gate:

- [ ] Binary layout asserts pass for every touched descriptor/record, including size,
  per-field offset, and backend scalar union size assertions.
- [ ] Golden-file serialization/deserialization tests validate offsets, counts, and
  padding for compound shapes, soft-body records, and each sidecar binding record type.
- [ ] Struct documentation reflects finalized field offsets and sizes aligned with the design.

## Phase 5 - Cooker and Loose Layout Emission (`Cooker/Import/Internal`)

- [ ] Implement/update cook pipelines for all physics artifact classes:
- [ ] Enforce schema-first validation for all L1 physics source payloads (manual checks only for non-schema constraints).
- [ ] Apply the L1→L2 field-mapping checklist (established in Phase 2) to verify cooker translation for rigid body, collider, character, soft body, joint, vehicle, vehicle wheel, and aggregate records.
- [ ] Physics materials (`.opmat`) fixed descriptor emission with SHA-256 patching.
- [ ] Collision shapes (`.ocshape`) analytic vs backend-cooked split, including compound child trailing array emission.
- [ ] Physics sidecar (`.opscene`) with full component-table directory emission.
- [ ] Backend-cooked blob classes (`shape`, `constraint`, `vehicle`, `soft-body`) into `physics.table` + `physics.data`.
- [ ] Populate sidecar binding payload indices from emitted resources:
  `JointBindingRecord::constraint_resource_index`,
  `VehicleBindingRecord::constraint_resource_index`,
  `SoftBodyBindingRecord::topology_resource_index` (with matching `topology_format`).
- [ ] Enforce side-table emission registry order/location exactly as design rows 1-14.
- [ ] Emit and patch `PhysicsSceneAssetDesc` staleness fields:
- [ ] `target_scene_key`.
- [ ] `target_scene_content_hash` (SHA-256 of paired `.oscene` artifact).
- [ ] `target_node_count`.
- [ ] Ensure loose index (`*.oxlcidx`) remains authoritative:
- [ ] Asset entries include key, descriptor-relative path, virtual path, type tag, descriptor hash.
- [ ] File records include physics table/data and other resource files.
- [ ] Incremental recook correctness:
- [ ] Re-cooking one asset updates only affected descriptor/blob/index records.
- [ ] No container-wide ordinal drift dependency.

Phase 5 exit gate:

- [ ] End-to-end cooker tests cover analytic shape, non-analytic shape, joint blob, soft-body blob, vehicle blob paths.
- [ ] Complex-scene fixture validates component directory + wheel table + trailing arrays.
- [ ] DemoShell `RenderScene` validation for `physics_domains` confirms no missing
  joint/vehicle/soft-body payload index hydration failures.
- [ ] Incremental recook test proves unaffected assets remain stable.

## Phase 6 - PAK Builder and Packaging Relocation

- [ ] Ensure PAK builder consumes cooked loose layout without recooking:
- [ ] Reads index as source of truth for assets/file records.
- [ ] Concatenates descriptor regions and physics resource regions.
- [ ] Rewrites offsets from loose-relative to PAK-relative where required.
- [ ] Produces self-contained mountable bundle with catalog/directory structures.
- [ ] Validate behavior across base + patch PAK layering for resolver priority expectations.

Phase 6 exit gate:

- [ ] Loose->PAK roundtrip tests confirm byte-for-byte payload preservation (except expected relocated offsets).
- [ ] Runtime can load both loose and PAK outputs with identical physics behavior.
- [ ] PAK build path contains no cooker logic.

## Phase 7 - Runtime Hydration and Simulation Contract (L3)

- [ ] Integrate physics module into engine frame orchestration (fixed simulation + transform propagation phases).
- [ ] Implement strict sidecar staleness guard before hydration:
- [ ] Reject on `target_scene_key`, `target_scene_content_hash`, or `target_node_count` mismatch.
- [ ] Hydrate in dependency order from design:
- [ ] Shapes -> Materials -> RigidBodies -> Colliders -> Characters -> SoftBodies -> Joints -> Vehicles -> Aggregates.
- [ ] Branch restore path strictly by backend format tag (`Jolt` vs `PhysX` cooked blobs).
- [ ] Maintain runtime caches/maps by `AssetKey` and scene-node-index mappings for live handles.
- [ ] Enforce runtime handle policy:
- [ ] Session-scoped handles only; never persisted.
- [ ] No raw backend pointer leakage across L3 boundary abstractions.
- [ ] Mid-session invalidation behavior follows backend/owner-responsibility contract.
- [ ] Implement simulation-to-scene sync for body and vehicle wheel transforms.

Phase 7 exit gate:

- [ ] Hydration tests cover every component table type and both backends (where available).
- [ ] Mismatch guard tests hard-fail invalid sidecars.
- [ ] Session teardown invalidates all handles; sync path verified under object removal scenarios.

## Phase 8 - Zero-Tolerance Cleanup and Legacy Removal

- [ ] Remove legacy/duplicate pipelines that bypass binary descriptors.
- [ ] Remove runtime fallback parsing of L1 JSON for physics instantiation.
- [ ] Remove any cross-collection ordinal reference remnants.
- [ ] Remove deprecated handle wrappers that contradict current runtime-handle policy.
- [ ] Remove compatibility code paths that keep old schema alive in production.

Phase 8 exit gate:

- [ ] Search-based audit confirms no deprecated APIs/symbols remain.
- [ ] Loader/cooker only accept finalized schema paths.
- [ ] Migration notes document intentional breaking changes and no-shim policy.

## Phase 9 - Final Validation and Release Gate

- [ ] Build validation:
- [ ] Full project build with required toolchain flags (including `/GR-` and C++23 expectations where applicable).
- [ ] Zero warnings for packing/alignment/serialization in touched physics content code.
- [ ] Determinism and parity validation:
- [ ] Same canonical virtual path -> same `AssetKey` across runs/machines.
- [ ] Repeat cooks produce stable descriptor/blob hashes when inputs are unchanged.
- [ ] Scene hydration parity across loose and PAK modes.
- [ ] Documentation validation:
- [ ] `physics.md` and this plan remain aligned after implementation deltas.
- [ ] Any scope correction discovered during implementation updates docs first (then code).

Final release gate:

- [ ] Every phase above has code, docs, and validation evidence.
- [ ] Remaining gaps list is empty.
- [ ] Status may be advanced from `in_progress` only after all evidence is recorded.
