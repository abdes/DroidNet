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

//! Describes a cooked Jolt physics binary blob stored in the physics_region.
#pragma pack(push, 1)
struct PhysicsResourceDesc {
  core::OffsetT data_offset = 0; //!< Absolute offset to cooked Jolt data
  core::DataBlobSizeT size_bytes = 0; //!< Size in bytes
  PhysicsResourceFormat format = PhysicsResourceFormat::kJoltShapeBinary;
  uint8_t reserved[3] = {};
  uint64_t content_hash = 0; //!< First 8 bytes of SHA256
};
#pragma pack(pop)
static_assert(sizeof(PhysicsResourceDesc) == 24);

//! Physics surface properties asset.
#pragma pack(push, 1)
struct PhysicsMaterialAssetDesc {
  core::AssetHeader header;
  float friction = 0.5F;
  float restitution = 0.0F;
  float density = 1000.0F; //!< kg/m^3
  PhysicsCombineMode combine_mode_friction = PhysicsCombineMode::kAverage;
  PhysicsCombineMode combine_mode_restitution = PhysicsCombineMode::kAverage;
  uint8_t reserved[19] = {};
};
#pragma pack(pop)
static_assert(sizeof(PhysicsMaterialAssetDesc) == 128);

// Cooked payload ref (8 bytes).
#pragma pack(push, 1)
struct CookedShapePayloadRef {
  core::ResourceIndexT resource_index = kInvalidShapePayloadRefIndex;
  ShapePayloadType payload_type = kInvalidShapePayloadType;
  uint8_t reserved[3] = {};
};
#pragma pack(pop)
static_assert(sizeof(CookedShapePayloadRef) == 8);
static_assert(offsetof(CookedShapePayloadRef, resource_index) == 0);
static_assert(offsetof(CookedShapePayloadRef, payload_type) == 4);
static_assert(offsetof(CookedShapePayloadRef, reserved) == 5);

// Fixed tagged-union payload for per-shape params (80 bytes).
#pragma pack(push, 1)
union ShapeParams {
  struct SphereParams {
    float radius = 0.0F;
    float reserved[19] = {};
  } sphere;
  struct CapsuleParams {
    float radius = 0.0F;
    float half_height = 0.0F;
    float reserved[18] = {};
  } capsule;
  struct BoxParams {
    float half_extents[3] = { 0.0F, 0.0F, 0.0F };
    float reserved[17] = {};
  } box;
  struct CylinderParams {
    float radius = 0.0F;
    float half_height = 0.0F;
    float reserved[18] = {};
  } cylinder;
  struct ConeParams {
    float radius = 0.0F;
    float half_height = 0.0F;
    float reserved[18] = {};
  } cone;
  struct ConvexHullParams {
    float reserved[20] = {};
  } convex_hull;
  struct TriangleMeshParams {
    float reserved[20] = {};
  } triangle_mesh;
  struct HeightFieldParams {
    float reserved[20] = {};
  } height_field;
  struct PlaneParams {
    float normal[3] = { 0.0F, 0.0F, 0.0F };
    float distance = 0.0F;
    float reserved[16] = {};
  } plane;
  struct WorldBoundaryParams {
    WorldBoundaryMode boundary_mode = WorldBoundaryMode::kInvalid;
    float limits_min[3] = { 0.0F, 0.0F, 0.0F };
    float limits_max[3] = { 0.0F, 0.0F, 0.0F };
    float reserved[13] = {};
  } world_boundary;
  struct CompoundParams {
    uint32_t reserved_u32 = 0;
    float reserved[19] = {};
  } compound;
  float raw[20] = {};
};
#pragma pack(pop)
static_assert(sizeof(ShapeParams) == 80);

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
  uint8_t reserved[8] = {};
};
#pragma pack(pop)
static_assert(sizeof(CollisionShapeAssetDesc) == 268);

//! Per-component binding table directory entry (20 bytes).
//! Mirrors SceneComponentTableDesc layout for consistency.
#pragma pack(push, 1)
struct PhysicsComponentTableDesc {
  PhysicsBindingType binding_type
    = PhysicsBindingType::kUnknown; //!< Physics sidecar binding type (NOT a
                                    //!< SceneNode component)
  core::ResourceTable table = {}; //!< Offset/count/entry_size
};
#pragma pack(pop)
static_assert(sizeof(PhysicsComponentTableDesc) == 20);

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
  AssetKey target_scene_key = {}; //!< Must match the paired SceneAsset key
  uint32_t target_node_count = 0; //!< Expected node count for identity check
  uint32_t component_table_count = 0;
  core::OffsetT component_table_directory_offset
    = 0; //!< Offset to PhysicsComponentTableDesc[]
  uint8_t reserved[129] = {};
};
#pragma pack(pop)
static_assert(sizeof(PhysicsSceneAssetDesc) == 256);

//! Rigid body binding record (64 bytes).
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

  AssetKey shape_asset_key
    = kInvalidCollisionShapeAssetKey; //!< CollisionShapeAssetDesc
  AssetKey material_asset_key
    = kInvalidPhysicsMaterialAssetKey; //!< PhysicsMaterialAssetDesc

  uint8_t reserved[20] = {};
};
#pragma pack(pop)
static_assert(sizeof(RigidBodyBindingRecord) == 88);

//! Collider-only binding record (32 bytes) — static trigger/sensor shape.
#pragma pack(push, 1)
struct ColliderBindingRecord {
  world::SceneNodeIndexT node_index = 0;
  AssetKey shape_asset_key = kInvalidCollisionShapeAssetKey;
  AssetKey material_asset_key = kInvalidPhysicsMaterialAssetKey;
  uint16_t collision_layer = 0;
  uint32_t collision_mask = 0xFFFFFFFF;
  uint8_t reserved[14] = {};
};
#pragma pack(pop)
static_assert(sizeof(ColliderBindingRecord) == 56);

//! Character controller binding record (48 bytes).
#pragma pack(push, 1)
struct CharacterBindingRecord {
  world::SceneNodeIndexT node_index = 0;
  AssetKey shape_asset_key = kInvalidCollisionShapeAssetKey;
  float mass = 80.0F;
  float max_slope_angle = 0.7854F; //!< ~45 deg in radians
  float step_height = 0.3F;
  float max_strength = 100.0F;
  uint16_t collision_layer = 0;
  uint32_t collision_mask = 0xFFFFFFFF;
  uint8_t reserved[18] = {};
};
#pragma pack(pop)
static_assert(sizeof(CharacterBindingRecord) == 60);

//! Soft body binding record (48 bytes).
#pragma pack(push, 1)
struct SoftBodyBindingRecord {
  world::SceneNodeIndexT node_index = 0;
  uint32_t cluster_count = 0;
  float stiffness = 0.0F;
  float damping = 0.0F;
  float edge_compliance = 0.0F;
  float shear_compliance = 0.0F;
  float bend_compliance = 1.0F;
  SoftBodyTetherMode tether_mode = SoftBodyTetherMode::kNone;
  uint8_t reserved0[3] = {};
  float tether_max_distance_multiplier = 1.0F;
  uint8_t reserved1[12] = {};
};
#pragma pack(pop)
static_assert(sizeof(SoftBodyBindingRecord) == 48);

//! Joint binding record (32 bytes). The joint blob is stored as a
//! kJoltConstraintBinary entry in the physics_resource_table.
#pragma pack(push, 1)
struct JointBindingRecord {
  world::SceneNodeIndexT node_index_a = 0; //!< Body A
  world::SceneNodeIndexT node_index_b
    = kWorldAttachmentNodeIndex; //!< Body B (`kWorldAttachmentNodeIndex` =
                                 //!< world attachment)
  core::ResourceIndexT constraint_resource_index = core::kNoResourceIndex;
  uint8_t reserved[20] = {};
};
#pragma pack(pop)
static_assert(sizeof(JointBindingRecord) == 32);

//! Vehicle binding record (32 bytes). Full config is in the constraint blob.
#pragma pack(push, 1)
struct VehicleBindingRecord {
  world::SceneNodeIndexT node_index = 0; //!< Root chassis node
  core::ResourceIndexT constraint_resource_index = core::kNoResourceIndex;
  uint8_t reserved[24] = {};
};
#pragma pack(pop)
static_assert(sizeof(VehicleBindingRecord) == 32);

//! Aggregate (group) binding record (28 bytes).
#pragma pack(push, 1)
struct AggregateBindingRecord {
  world::SceneNodeIndexT node_index = 0; //!< Root node of the aggregate group
  uint32_t max_bodies = 0; //!< Pre-allocation size
  uint32_t filter_overlap = 1; //!< Boolean: disable self-collision?
  AggregateAuthority authority = AggregateAuthority::kSimulation;
  uint8_t reserved[15] = {};
};
#pragma pack(pop)
static_assert(sizeof(AggregateBindingRecord) == 28);

} // namespace oxygen::data::pak::physics

// NOLINTEND(*-avoid-c-arrays,*-magic-numbers)
OXYGEN_DIAGNOSTIC_POP
