//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Compilers.h>
#include <Oxygen/Data/PakFormat_world.h>

// packed structs intentionally embed unaligned NamedType ResourceIndexT fields
OXYGEN_DIAGNOSTIC_PUSH
OXYGEN_DIAGNOSTIC_DISABLE_MSVC(4315)
// NOLINTBEGIN(*-avoid-c-arrays,*-magic-numbers)

//! Oxygen PAK format physics domain schema.
/*!
 Owns physics assets/resources and physics sidecar scene-binding records.
*/
namespace oxygen::data::pak::physics {

// NOLINTBEGIN(*-macro-usage,*-enum-size)
#define OXPHYS_RESOURCE_FORMAT(name, value) name = (value),
enum class PhysicsResourceFormat : uint8_t {
#include <Oxygen/Core/Meta/Physics/PakPhysics.inc>
};
#undef OXPHYS_RESOURCE_FORMAT

#define OXPHYS_COMBINE_MODE(name, value) name = (value),
enum class PhysicsCombineMode : uint8_t {
#include <Oxygen/Core/Meta/Physics/PakPhysics.inc>
};
#undef OXPHYS_COMBINE_MODE

#define OXPHYS_SHAPE_TYPE(name, value) name = (value),
enum class ShapeType : uint8_t {
#include <Oxygen/Core/Meta/Physics/PakPhysicsShape.inc>
};
#undef OXPHYS_SHAPE_TYPE

#define OXPHYS_SHAPE_PAYLOAD_TYPE(name, value) name = (value),
enum class ShapePayloadType : uint8_t {
#include <Oxygen/Core/Meta/Physics/PakPhysicsShape.inc>
};
#undef OXPHYS_SHAPE_PAYLOAD_TYPE

#define OXPHYS_WORLD_BOUNDARY_MODE(name, value) name = (value),
enum class WorldBoundaryMode : uint32_t {
#include <Oxygen/Core/Meta/Physics/PakPhysicsShape.inc>
};
#undef OXPHYS_WORLD_BOUNDARY_MODE

#define OXPHYS_SHAPE_CONSTANT(name, value)
#define OXPHYS_SHAPE_RESOURCE_INDEX_ASSERT(expr)                               \
  static_assert(core::kNoResourceIndex == 0);
#include <Oxygen/Core/Meta/Physics/PakPhysicsShape.inc>
#undef OXPHYS_SHAPE_RESOURCE_INDEX_ASSERT
#undef OXPHYS_SHAPE_CONSTANT

#define OXPHYS_BODY_TYPE(name, value) name = (value),
enum class PhysicsBodyType : uint8_t {
#include <Oxygen/Core/Meta/Physics/PakPhysics.inc>
};
#undef OXPHYS_BODY_TYPE

#define OXPHYS_MOTION_QUALITY(name, value) name = (value),
enum class PhysicsMotionQuality : uint8_t {
#include <Oxygen/Core/Meta/Physics/PakPhysics.inc>
};
#undef OXPHYS_MOTION_QUALITY

#define OXPHYS_SOFT_BODY_TETHER_MODE(name, value) name = (value),
enum class SoftBodyTetherMode : uint8_t {
#include <Oxygen/Core/Meta/Physics/PakPhysics.inc>
};
#undef OXPHYS_SOFT_BODY_TETHER_MODE

#define OXPHYS_VEHICLE_CONTROLLER_TYPE(name, value) name = (value),
enum class VehicleControllerType : uint32_t {
#include <Oxygen/Core/Meta/Physics/PakPhysics.inc>
};
#undef OXPHYS_VEHICLE_CONTROLLER_TYPE

#define OXPHYS_VEHICLE_WHEEL_SIDE(name, value) name = (value),
enum class VehicleWheelSide : uint8_t {
#include <Oxygen/Core/Meta/Physics/PakPhysics.inc>
};
#undef OXPHYS_VEHICLE_WHEEL_SIDE

#define OXPHYS_AGGREGATE_AUTHORITY(name, value) name = (value),
enum class AggregateAuthority : uint8_t {
#include <Oxygen/Core/Meta/Physics/PakPhysics.inc>
};
#undef OXPHYS_AGGREGATE_AUTHORITY

#define OXPHYS_BINDING_TYPE(name, value) name = (value),
enum class PhysicsBindingType : uint32_t {
#include <Oxygen/Core/Meta/Physics/PakPhysics.inc>
};
#undef OXPHYS_BINDING_TYPE
// NOLINTEND(*-macro-usage,*-enum-size)

[[maybe_unused]] constexpr uint8_t kPhysicsMaterialAssetVersion = 1;
[[maybe_unused]] constexpr uint8_t kCollisionShapeAssetVersion = 1;
[[maybe_unused]] constexpr uint8_t kPhysicsSceneAssetVersion = 1;
[[maybe_unused]] constexpr core::ResourceIndexT kInvalidShapePayloadRefIndex
  = core::kNoResourceIndex;
[[maybe_unused]] constexpr ShapePayloadType kInvalidShapePayloadType
  = ShapePayloadType::kInvalid;
[[maybe_unused]] constexpr AssetKey kInvalidPhysicsMaterialAssetKey = {};
[[maybe_unused]] constexpr AssetKey kInvalidCollisionShapeAssetKey = {};
[[maybe_unused]] constexpr uint32_t kShapeIsSensorFalse = 0U;
[[maybe_unused]] constexpr uint32_t kShapeIsSensorTrue = 1U;
[[maybe_unused]] constexpr world::SceneNodeIndexT kWorldAttachmentNodeIndex
  = 0xFFFFFFFFU;

static_assert(core::kCurrentPakFormatVersion == 7);

//! Describes a cooked backend physics binary blob stored in the physics_region.
#pragma pack(push, 1)
struct PhysicsResourceDesc {
  core::OffsetT data_offset = 0; //!< Absolute offset to cooked physics data
  core::DataBlobSizeT size_bytes = 0; //!< Size in bytes
  PhysicsResourceFormat format = PhysicsResourceFormat::kJoltShapeBinary;
  uint8_t content_hash[32] = {}; //!< Full SHA-256 of payload bytes
};
#pragma pack(pop)
static_assert(sizeof(PhysicsResourceDesc) == 45);
static_assert(offsetof(PhysicsResourceDesc, data_offset) == 0);
static_assert(offsetof(PhysicsResourceDesc, size_bytes) == 8);
static_assert(offsetof(PhysicsResourceDesc, format) == 12);
static_assert(offsetof(PhysicsResourceDesc, content_hash) == 13);

//! Physics surface properties asset.
#pragma pack(push, 1)
struct PhysicsMaterialAssetDesc {
  core::AssetHeader header;
  float static_friction = 0.5F;
  float dynamic_friction = 0.5F;
  float restitution = 0.0F;
  float density = 1000.0F; //!< kg/m^3
  PhysicsCombineMode combine_mode_friction = PhysicsCombineMode::kAverage;
  PhysicsCombineMode combine_mode_restitution = PhysicsCombineMode::kAverage;
};
#pragma pack(pop)
static_assert(sizeof(PhysicsMaterialAssetDesc) == 97);
static_assert(offsetof(PhysicsMaterialAssetDesc, static_friction)
  == sizeof(core::AssetHeader));
static_assert(offsetof(PhysicsMaterialAssetDesc, dynamic_friction)
  == offsetof(PhysicsMaterialAssetDesc, static_friction) + sizeof(float));
static_assert(offsetof(PhysicsMaterialAssetDesc, combine_mode_friction)
  == offsetof(PhysicsMaterialAssetDesc, density) + sizeof(float));
static_assert(offsetof(PhysicsMaterialAssetDesc, combine_mode_restitution)
  == offsetof(PhysicsMaterialAssetDesc, combine_mode_friction)
    + sizeof(PhysicsCombineMode));

// Cooked payload ref.
#pragma pack(push, 1)
struct CookedShapePayloadRef {
  core::ResourceIndexT resource_index = kInvalidShapePayloadRefIndex;
  ShapePayloadType payload_type = kInvalidShapePayloadType;
};
#pragma pack(pop)
static_assert(sizeof(CookedShapePayloadRef) == 5);
static_assert(offsetof(CookedShapePayloadRef, resource_index) == 0);
static_assert(offsetof(CookedShapePayloadRef, payload_type) == 4);

// Fixed tagged-union payload for per-shape params.
#pragma pack(push, 1)
union ShapeParams {
  struct SphereParams {
    float radius = 0.0F;
    float _reserved[19] = {}; //!< Union tail: 80-byte arm size
  } sphere;
  struct CapsuleParams {
    float radius = 0.0F;
    float half_height = 0.0F;
    float _reserved[18] = {}; //!< Union tail: 80-byte arm size
  } capsule;
  struct BoxParams {
    float half_extents[3] = { 0.0F, 0.0F, 0.0F };
    float _reserved[17] = {}; //!< Union tail: 80-byte arm size
  } box;
  struct CylinderParams {
    float radius = 0.0F;
    float half_height = 0.0F;
    float _reserved[18] = {}; //!< Union tail: 80-byte arm size
  } cylinder;
  struct ConeParams {
    float radius = 0.0F;
    float half_height = 0.0F;
    float _reserved[18] = {}; //!< Union tail: 80-byte arm size
  } cone;
  struct ConvexHullParams {
    float _reserved[20] = {}; //!< Union tail: 80-byte arm size
  } convex_hull;
  struct TriangleMeshParams {
    float _reserved[20] = {}; //!< Union tail: 80-byte arm size
  } triangle_mesh;
  struct HeightFieldParams {
    float _reserved[20] = {}; //!< Union tail: 80-byte arm size
  } height_field;
  struct PlaneParams {
    float normal[3] = { 0.0F, 0.0F, 0.0F };
    float distance = 0.0F;
    float _reserved[16] = {}; //!< Union tail: 80-byte arm size
  } plane;
  struct WorldBoundaryParams {
    WorldBoundaryMode boundary_mode = WorldBoundaryMode::kInvalid;
    float limits_min[3] = { 0.0F, 0.0F, 0.0F };
    float limits_max[3] = { 0.0F, 0.0F, 0.0F };
    float _reserved[13] = {}; //!< Union tail: 80-byte arm size
  } world_boundary;
  struct CompoundParams {
    uint32_t child_count = 0;
    uint32_t child_byte_offset = 0; //!< Self-relative offset to child records
    float _reserved[18] = {}; //!< Union tail: 80-byte arm size
  } compound;
  float raw[20] = {};
};
#pragma pack(pop)
static_assert(sizeof(ShapeParams) == 80);
static_assert(offsetof(ShapeParams, compound.child_count) == 0);
static_assert(offsetof(ShapeParams, compound.child_byte_offset) == 4);

//! Fixed-size trailing-array element for compound shape children.
#pragma pack(push, 1)
struct CompoundShapeChildDesc {
  uint32_t shape_type = static_cast<uint32_t>(ShapeType::kInvalid);
  float radius = 0.0F;
  float half_height = 0.0F;
  float half_extents[3] = { 0.0F, 0.0F, 0.0F };
  float normal[3] = { 0.0F, 0.0F, 0.0F };
  float distance = 0.0F;
  WorldBoundaryMode boundary_mode = WorldBoundaryMode::kInvalid;
  float limits_min[3] = { 0.0F, 0.0F, 0.0F };
  float limits_max[3] = { 0.0F, 0.0F, 0.0F };
  float local_position[3] = { 0.0F, 0.0F, 0.0F };
  float local_rotation[4] = { 0.0F, 0.0F, 0.0F, 1.0F };
  float local_scale[3] = { 1.0F, 1.0F, 1.0F };
  //!< Invalid for analytic child types
  AssetKey payload_asset_key = {}; // NOLINT
  uint8_t _reserved[4] = {}; //!< Fixed-size record: 128-byte child element size
};
#pragma pack(pop)
static_assert(sizeof(CompoundShapeChildDesc) == 128);
static_assert(offsetof(CompoundShapeChildDesc, shape_type) == 0);
static_assert(offsetof(CompoundShapeChildDesc, radius) == 4);
static_assert(offsetof(CompoundShapeChildDesc, boundary_mode) == 40);
static_assert(offsetof(CompoundShapeChildDesc, local_rotation) == 80);
static_assert(offsetof(CompoundShapeChildDesc, payload_asset_key) == 108);

//! Canonical serialized collision shape descriptor.
#pragma pack(push, 1)
struct CollisionShapeAssetDesc {
  core::AssetHeader header;
  ShapeType shape_type = ShapeType::kInvalid;
  float local_position[3] = { 0.0F, 0.0F, 0.0F };
  float local_rotation[4] = { 0.0F, 0.0F, 0.0F, 1.0F };
  float local_scale[3] = { 1.0F, 1.0F, 1.0F };
  uint32_t is_sensor = kShapeIsSensorFalse;
  uint64_t collision_own_layer = 1ULL;
  uint64_t collision_target_layers = 0xFFFFFFFFFFFFFFFFULL;
  AssetKey material_asset_key = kInvalidPhysicsMaterialAssetKey;
  ShapeParams shape_params {};
  CookedShapePayloadRef cooked_shape_ref {};
};
#pragma pack(pop)
static_assert(sizeof(CollisionShapeAssetDesc) == 241);
static_assert(
  offsetof(CollisionShapeAssetDesc, shape_type) == sizeof(core::AssetHeader));
static_assert(offsetof(CollisionShapeAssetDesc, is_sensor)
  == offsetof(CollisionShapeAssetDesc, local_scale) + sizeof(float[3]));
static_assert(offsetof(CollisionShapeAssetDesc, shape_params)
  == offsetof(CollisionShapeAssetDesc, material_asset_key) + sizeof(AssetKey));
static_assert(offsetof(CollisionShapeAssetDesc, cooked_shape_ref)
  == offsetof(CollisionShapeAssetDesc, shape_params) + sizeof(ShapeParams));

//! Per-component binding table directory entry.
//! Mirrors SceneComponentTableDesc layout for consistency.
#pragma pack(push, 1)
struct PhysicsComponentTableDesc {
  //!< Physics sidecar binding type (NOT a SceneNode component)
  PhysicsBindingType binding_type = PhysicsBindingType::kUnknown;
  core::ResourceTable table = {}; //!< Offset/count/entry_size
};
#pragma pack(pop)
static_assert(sizeof(PhysicsComponentTableDesc) == 20);
static_assert(offsetof(PhysicsComponentTableDesc, binding_type) == 0);
static_assert(
  offsetof(PhysicsComponentTableDesc, table) == sizeof(PhysicsBindingType));

//! Physics scene sidecar asset descriptor.
/*! Contains the binding tables mapping world::SceneNodeIndexT to physics domain
 *  components. Loading is strictly separated from the base SceneAssetDesc.
 *
 *  The sidecar is identified by the same base name as its paired Scene asset
 *
 * with a ".opscene" suffix in the asset directory.
 *
 *  @par Hydration contract
 *  `target_scene_key` must identify a Scene asset already loaded in the same
 *  mount. Loader hard-fails on identity mismatch, missing scene key, or
 *  node-count violations (see PakFormatVersion7_Physics.md §7.2). */
#pragma pack(push, 1)
struct PhysicsSceneAssetDesc {
  core::AssetHeader header;
  AssetKey target_scene_key; //!< Must match the paired SceneAsset key
  uint32_t target_node_count = 0; //!< Expected node count for identity check
  uint32_t component_table_count = 0;
  //!< Offset to PhysicsComponentTableDesc[]
  core::OffsetT component_table_directory_offset = 0;
  //!< SHA-256 of paired .oscene payload
  uint8_t target_scene_content_hash[32] = {};
};
#pragma pack(pop)
static_assert(sizeof(PhysicsSceneAssetDesc) == 143);
static_assert(offsetof(PhysicsSceneAssetDesc, target_scene_key)
  == sizeof(core::AssetHeader));
static_assert(offsetof(PhysicsSceneAssetDesc, target_scene_content_hash)
  == offsetof(PhysicsSceneAssetDesc, component_table_directory_offset)
    + sizeof(core::OffsetT));

//! Backend-specific scalar fields for rigid-body bindings.
#pragma pack(push, 1)
union RigidBodyBackendScalars {
  struct JoltScalars {
    uint8_t num_velocity_steps_override = 0;
    uint8_t num_position_steps_override = 0;
    uint8_t _reserved[8] = {}; //!< Union tail: 10-byte arm size
  } jolt;
  struct PhysXScalars {
    uint8_t min_velocity_iters = 0;
    uint8_t min_position_iters = 0;
    float max_contact_impulse = 0.0F;
    float contact_report_threshold = 0.0F;
  } physx;
  uint8_t raw[10] = {};
};
#pragma pack(pop)
static_assert(sizeof(RigidBodyBackendScalars) == 10);
static_assert(
  offsetof(RigidBodyBackendScalars, jolt.num_velocity_steps_override)
  == offsetof(RigidBodyBackendScalars, raw));
static_assert(
  offsetof(RigidBodyBackendScalars, physx.max_contact_impulse) == 2);

//! Backend-specific scalar fields for character bindings.
#pragma pack(push, 1)
union CharacterBackendScalars {
  struct JoltScalars {
    float penetration_recovery_speed = 0.0F;
    uint32_t max_num_hits = 0;
    float hit_reduction_cos_max_angle = 0.0F;
  } jolt;
  struct PhysXScalars {
    float contact_offset = 0.0F;
    uint8_t _reserved[8] = {}; //!< Union tail: 12-byte arm size
  } physx;
  uint8_t raw[12] = {};
};
#pragma pack(pop)
static_assert(sizeof(CharacterBackendScalars) == 12);
static_assert(offsetof(CharacterBackendScalars, jolt.penetration_recovery_speed)
  == offsetof(CharacterBackendScalars, raw));
static_assert(offsetof(CharacterBackendScalars, physx.contact_offset)
  == offsetof(CharacterBackendScalars, raw));

//! Backend-specific scalar fields for soft-body bindings.
#pragma pack(push, 1)
union SoftBodyBackendScalars {
  struct JoltScalars {
    uint32_t num_velocity_steps = 0;
    uint32_t num_position_steps = 0;
    float gravity_factor = 1.0F;
  } jolt;
  struct PhysXFemScalars {
    float youngs_modulus = 0.0F;
    float poissons = 0.0F;
    float dynamic_friction = 0.0F;
  } physx;
  uint8_t raw[12] = {};
};
#pragma pack(pop)
static_assert(sizeof(SoftBodyBackendScalars) == 12);
static_assert(offsetof(SoftBodyBackendScalars, jolt.num_velocity_steps)
  == offsetof(SoftBodyBackendScalars, raw));
static_assert(offsetof(SoftBodyBackendScalars, physx.youngs_modulus)
  == offsetof(SoftBodyBackendScalars, raw));

//! Backend-specific scalar fields for joint bindings.
#pragma pack(push, 1)
union JointBackendScalars {
  struct JoltScalars {
    uint8_t num_velocity_steps_override = 0;
    uint8_t num_position_steps_override = 0;
    uint8_t _reserved[14] = {}; //!< Union tail: 16-byte arm size
  } jolt;
  struct PhysXScalars {
    float inv_mass_scale0 = 0.0F;
    float inv_mass_scale1 = 0.0F;
    float inv_inertia_scale0 = 0.0F;
    float inv_inertia_scale1 = 0.0F;
  } physx;
  uint8_t raw[16] = {};
};
#pragma pack(pop)
static_assert(sizeof(JointBackendScalars) == 16);
static_assert(offsetof(JointBackendScalars, jolt.num_velocity_steps_override)
  == offsetof(JointBackendScalars, raw));
static_assert(offsetof(JointBackendScalars, physx.inv_mass_scale0)
  == offsetof(JointBackendScalars, raw));

//! Backend-specific scalar fields for vehicle-wheel bindings.
#pragma pack(push, 1)
union VehicleWheelBackendScalars {
  struct JoltScalars {
    float wheel_castor = 0.0F;
  } jolt;
  struct PhysXScalars {
    uint8_t _reserved[4] = {}; //!< Union tail: 4-byte arm size
  } physx;
  uint8_t raw[4] = {};
};
#pragma pack(pop)
static_assert(sizeof(VehicleWheelBackendScalars) == 4);
static_assert(offsetof(VehicleWheelBackendScalars, jolt.wheel_castor)
  == offsetof(VehicleWheelBackendScalars, raw));

//! Rigid body binding record.
#pragma pack(push, 1)
struct RigidBodyBindingRecord {
  world::SceneNodeIndexT node_index = 0;
  PhysicsBodyType body_type = PhysicsBodyType::kStatic;
  PhysicsMotionQuality motion_quality = PhysicsMotionQuality::kDiscrete;
  uint16_t collision_layer = 0; //!< Maps to CollisionLayers.h
  uint32_t collision_mask = 0xFFFFFFFF;

  float mass = 0.0F; //!< 0 = inferred from shape density
  float linear_damping = 0.05F;
  float angular_damping = 0.05F;
  float gravity_factor = 1.0F;

  uint32_t initial_activation = 1; //!< Boolean
  uint32_t is_sensor = 0; //!< Boolean

  //!< CollisionShapeAssetDesc
  AssetKey shape_asset_key = kInvalidCollisionShapeAssetKey;
  //!< PhysicsMaterialAssetDesc
  AssetKey material_asset_key = kInvalidPhysicsMaterialAssetKey;

  float com_override[3] = { 0.0F, 0.0F, 0.0F };
  uint32_t has_com_override = 0; //!< Boolean
  float inertia_override[3] = { 0.0F, 0.0F, 0.0F };
  uint32_t has_inertia_override = 0; //!< Boolean
  float max_linear_velocity = 0.0F;
  float max_angular_velocity = 0.0F;
  uint32_t allowed_dof_flags = 0;
  RigidBodyBackendScalars backend_scalars {};
};
#pragma pack(pop)
static_assert(sizeof(RigidBodyBindingRecord) == 122);
static_assert(offsetof(RigidBodyBindingRecord, node_index) == 0);
static_assert(offsetof(RigidBodyBindingRecord, shape_asset_key)
  == offsetof(RigidBodyBindingRecord, is_sensor) + sizeof(uint32_t));
static_assert(offsetof(RigidBodyBindingRecord, backend_scalars)
  == offsetof(RigidBodyBindingRecord, allowed_dof_flags) + sizeof(uint32_t));

//! Collider-only binding record — static trigger/sensor shape.
#pragma pack(push, 1)
struct ColliderBindingRecord {
  world::SceneNodeIndexT node_index = 0;
  AssetKey shape_asset_key = kInvalidCollisionShapeAssetKey;
  AssetKey material_asset_key = kInvalidPhysicsMaterialAssetKey;
  uint16_t collision_layer = 0;
  uint32_t collision_mask = 0xFFFFFFFF;
  uint32_t is_sensor = 0; //!< Boolean
};
#pragma pack(pop)
static_assert(sizeof(ColliderBindingRecord) == 46);
static_assert(offsetof(ColliderBindingRecord, collision_layer)
  == offsetof(ColliderBindingRecord, material_asset_key) + sizeof(AssetKey));
static_assert(offsetof(ColliderBindingRecord, is_sensor)
  == offsetof(ColliderBindingRecord, collision_mask) + sizeof(uint32_t));

//! Character controller binding record.
#pragma pack(push, 1)
struct CharacterBindingRecord {
  world::SceneNodeIndexT node_index = 0;
  AssetKey shape_asset_key = kInvalidCollisionShapeAssetKey;
  float mass = 80.0F;
  float max_slope_angle = 0.7854F; //!< ~45 deg in radians
  float step_height = 0.3F;
  float step_down_distance = 0.0F;
  float max_strength = 100.0F;
  float skin_width = 0.0F;
  float predictive_contact_distance = 0.0F;
  uint16_t collision_layer = 0;
  uint32_t collision_mask = 0xFFFFFFFF;
  AssetKey inner_shape_asset_key = kInvalidCollisionShapeAssetKey;
  CharacterBackendScalars backend_scalars {};
};
#pragma pack(pop)
static_assert(sizeof(CharacterBindingRecord) == 82);
static_assert(offsetof(CharacterBindingRecord, inner_shape_asset_key)
  == offsetof(CharacterBindingRecord, collision_mask) + sizeof(uint32_t));
static_assert(offsetof(CharacterBindingRecord, backend_scalars)
  == offsetof(CharacterBindingRecord, inner_shape_asset_key)
    + sizeof(AssetKey));

//! Soft body binding record.
#pragma pack(push, 1)
struct SoftBodyBindingRecord {
  world::SceneNodeIndexT node_index = 0;
  float edge_compliance = 0.0F;
  float shear_compliance = 0.0F;
  float bend_compliance = 1.0F;
  float volume_compliance = 0.0F;
  float pressure_coefficient = 0.0F;
  float global_damping = 0.0F;
  float restitution = 0.0F;
  float friction = 0.2F;
  float vertex_radius = 0.0F;
  float tether_max_distance_multiplier = 1.0F;
  uint32_t solver_iteration_count = 0;
  core::ResourceIndexT topology_resource_index = core::kNoResourceIndex;
  uint32_t pinned_vertex_count = 0;
  uint32_t pinned_vertex_byte_offset = 0; //!< Self-relative offset
  uint32_t kinematic_vertex_count = 0;
  uint32_t kinematic_vertex_byte_offset = 0; //!< Self-relative offset
  SoftBodyBackendScalars backend_scalars {};
  SoftBodyTetherMode tether_mode = SoftBodyTetherMode::kNone;
  PhysicsResourceFormat topology_format
    = PhysicsResourceFormat::kJoltSoftBodySharedSettingsBinary;
  uint8_t self_collision = 0; //!< Boolean
};
#pragma pack(pop)
static_assert(sizeof(SoftBodyBindingRecord) == 83);
static_assert(offsetof(SoftBodyBindingRecord, pinned_vertex_count)
  == offsetof(SoftBodyBindingRecord, topology_resource_index)
    + sizeof(core::ResourceIndexT));
static_assert(offsetof(SoftBodyBindingRecord, backend_scalars)
  == offsetof(SoftBodyBindingRecord, kinematic_vertex_byte_offset)
    + sizeof(uint32_t));
static_assert(offsetof(SoftBodyBindingRecord, self_collision)
  == offsetof(SoftBodyBindingRecord, topology_format)
    + sizeof(PhysicsResourceFormat));

//! Joint binding record. The joint blob is stored as a
//! kJoltConstraintBinary entry in the physics_resource_table.
#pragma pack(push, 1)
struct JointBindingRecord {
  world::SceneNodeIndexT node_index_a = 0;
  world::SceneNodeIndexT node_index_b = kWorldAttachmentNodeIndex;
  core::ResourceIndexT constraint_resource_index = core::kNoResourceIndex;
  JointBackendScalars backend_scalars {};
};
#pragma pack(pop)
static_assert(sizeof(JointBindingRecord) == 28);
static_assert(offsetof(JointBindingRecord, constraint_resource_index) == 8);
static_assert(offsetof(JointBindingRecord, backend_scalars) == 12);

//! Vehicle binding record. Full config is in the constraint blob.
#pragma pack(push, 1)
struct VehicleBindingRecord {
  world::SceneNodeIndexT node_index = 0; //!< Root chassis node
  core::ResourceIndexT constraint_resource_index = core::kNoResourceIndex;
  VehicleControllerType controller_type = VehicleControllerType::kWheeled;
  uint32_t wheel_slice_offset = 0; //!< Offset in kVehicleWheel table
  uint32_t wheel_slice_count = 0;
};
#pragma pack(pop)
static_assert(sizeof(VehicleBindingRecord) == 20);
static_assert(offsetof(VehicleBindingRecord, controller_type) == 8);
static_assert(offsetof(VehicleBindingRecord, wheel_slice_offset) == 12);
static_assert(offsetof(VehicleBindingRecord, wheel_slice_count) == 16);

//! Vehicle wheel topology record.
#pragma pack(push, 1)
struct VehicleWheelBindingRecord {
  world::SceneNodeIndexT vehicle_node_index = 0;
  world::SceneNodeIndexT wheel_node_index = 0;
  uint16_t axle_index = 0;
  VehicleWheelSide side = VehicleWheelSide::kLeft;
  VehicleWheelBackendScalars backend_scalars {};
};
#pragma pack(pop)
static_assert(sizeof(VehicleWheelBindingRecord) == 15);
static_assert(offsetof(VehicleWheelBindingRecord, backend_scalars) == 11);

//! Aggregate (group) binding record.
#pragma pack(push, 1)
struct AggregateBindingRecord {
  world::SceneNodeIndexT node_index = 0; //!< Root node of the aggregate group
  uint32_t max_bodies = 0; //!< Pre-allocation size
  uint32_t filter_overlap = 1; //!< Boolean: disable self-collision?
  AggregateAuthority authority = AggregateAuthority::kSimulation;
};
#pragma pack(pop)
static_assert(sizeof(AggregateBindingRecord) == 13);
static_assert(offsetof(AggregateBindingRecord, authority) == 12);

} // namespace oxygen::data::pak::physics

// NOLINTEND(*-avoid-c-arrays,*-magic-numbers)
OXYGEN_DIAGNOSTIC_POP
