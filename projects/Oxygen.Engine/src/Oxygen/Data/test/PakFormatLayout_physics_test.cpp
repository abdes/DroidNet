//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <iterator>

#include "PakFormatWireBytes.h"

#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace physics = oxygen::data::pak::physics;

using oxygen::data::pak::testing::Digest;
using oxygen::data::pak::testing::Encoded;
using oxygen::data::pak::testing::HeaderBytes;
using oxygen::data::pak::testing::Key;
using oxygen::data::pak::testing::MakeHeader;
using oxygen::data::pak::testing::WireBytes;

//! A physics resource row is its payload location, format, resource key and
//! digest.
NOLINT_TEST(PakFormatLayoutPhysicsTest, PhysicsResourceDescWireBytes)
{
  const auto row = physics::PhysicsResourceDesc {
    .data_offset = 0x0102030405060708U,
    .size_bytes = 0x11121314U,
    .format = physics::PhysicsResourceFormat::kPhysXConvexMeshBinary,
    .resource_asset_key = Key(0x30U),
    .content_hash = Digest(0x50U),
  };

  const auto expected = WireBytes {}
                          .U64(0x0102030405060708U)
                          .U32(0x11121314U)
                          .U8(5U)
                          .Run(0x30U, 16)
                          .Run(0x50U, 32);

  EXPECT_EQ(Encoded(row), expected.Bytes());
}

//! A physics material is its header, static and dynamic friction,
//! restitution, density and the two combine modes.
NOLINT_TEST(PakFormatLayoutPhysicsTest, PhysicsMaterialAssetDescWireBytes)
{
  const auto material = physics::PhysicsMaterialAssetDesc {
    .header = MakeHeader(),
    .static_friction = 1.5F,
    .dynamic_friction = 2.5F,
    .restitution = 3.5F,
    .density = 4.5F,
    .combine_mode_friction = physics::PhysicsCombineMode::kMax,
    .combine_mode_restitution = physics::PhysicsCombineMode::kMultiply,
  };

  auto expected = WireBytes {};
  expected.Append(HeaderBytes()).Floats(1.5F, 4).U8(2U).U8(3U);

  EXPECT_EQ(Encoded(material), expected.Bytes());
}

//! A cooked shape payload reference is its payload asset key and payload type.
NOLINT_TEST(PakFormatLayoutPhysicsTest, CookedShapePayloadRefWireBytes)
{
  const auto ref = physics::CookedShapePayloadRef {
    .payload_asset_key = Key(0x10U),
    .payload_type = physics::ShapePayloadType::kMesh,
  };

  const auto expected = WireBytes {}.Run(0x10U, 16).U8(2U);

  EXPECT_EQ(Encoded(ref), expected.Bytes());
}

//! A compound shape child is its shape type, radius, half height, half
//! extents, plane normal and distance, boundary mode and limits, local
//! transform, payload key and zeroed reserve.
NOLINT_TEST(PakFormatLayoutPhysicsTest, CompoundShapeChildDescWireBytes)
{
  const auto child = physics::CompoundShapeChildDesc {
    .shape_type = 0x11121314U,
    .radius = 1.5F,
    .half_height = 2.5F,
    .half_extents = { 3.5F, 4.5F, 5.5F, },
    .normal = { 6.5F, 7.5F, 8.5F, },
    .distance = 9.5F,
    .boundary_mode = physics::WorldBoundaryMode::kPlaneSet,
    .limits_min = { 10.5F, 11.5F, 12.5F, },
    .limits_max = { 13.5F, 14.5F, 15.5F, },
    .local_position = { 16.5F, 17.5F, 18.5F, },
    .local_rotation = { 19.5F, 20.5F, 21.5F, 22.5F, },
    .local_scale = { 23.5F, 24.5F, 25.5F, },
    .payload_asset_key = Key(0x30U),
  };

  const auto expected = WireBytes {}
                          .U32(0x11121314U)
                          .Floats(1.5F, 9)
                          .U32(2U)
                          .Floats(10.5F, 16)
                          .Run(0x30U, 16)
                          .Zeros(4);

  EXPECT_EQ(Encoded(child), expected.Bytes());
}

//! A collision shape asset with distinct values and the given shape type; each
//! test sets the shape parameter arm.
auto MakeShape(const physics::ShapeType type)
  -> physics::CollisionShapeAssetDesc
{
  return physics::CollisionShapeAssetDesc {
    .header = MakeHeader(),
    .shape_type = type,
    .local_position = { 1.5F, 2.5F, 3.5F, },
    .local_rotation = { 4.5F, 5.5F, 6.5F, 7.5F, },
    .local_scale = { 8.5F, 9.5F, 10.5F, },
    .is_sensor = 0x11121314U,
    .collision_own_layer = 0x2122232425262728U,
    .collision_target_layers = 0x3132333435363738U,
    .material_asset_key = Key(0x40U),
    .shape_params = {},
    .cooked_shape_ref = {
      .payload_asset_key = Key(0x50U),
      .payload_type = physics::ShapePayloadType::kHeightField,
    },
  };
}

//! The documented bytes of the shape `MakeShape` returns: header, shape type
//! byte `type`, local transform, sensor flag, collision layers, material key,
//! the 80 bytes of `params` and the cooked payload reference.
auto ShapeBytes(const uint8_t type, const WireBytes& params) -> WireBytes
{
  auto bytes = WireBytes {};
  bytes.Append(HeaderBytes())
    .U8(type)
    .Floats(1.5F, 3)
    .Floats(4.5F, 4)
    .Floats(8.5F, 3)
    .U32(0x11121314U)
    .U64(0x2122232425262728U)
    .U64(0x3132333435363738U)
    .Run(0x40U, 16)
    .Append(params)
    .Run(0x50U, 16)
    .U8(3U);
  return bytes;
}

//! A sphere shape is the common shape fields with the sphere arm: radius and
//! a zeroed reserve.
NOLINT_TEST(PakFormatLayoutPhysicsTest, CollisionShapeSphereArmWireBytes)
{
  auto shape = MakeShape(physics::ShapeType::kSphere);
  shape.shape_params.sphere = physics::ShapeParams::SphereParams {
    .radius = 11.5F,
  };

  auto params = WireBytes {};
  params.F32(11.5F).Zeros(76);

  EXPECT_EQ(Encoded(shape), ShapeBytes(1U, params).Bytes());
}

//! A capsule shape is the common shape fields with the capsule arm: radius,
//! half height and a zeroed reserve.
NOLINT_TEST(PakFormatLayoutPhysicsTest, CollisionShapeCapsuleArmWireBytes)
{
  auto shape = MakeShape(physics::ShapeType::kCapsule);
  shape.shape_params.capsule = physics::ShapeParams::CapsuleParams {
    .radius = 11.5F,
    .half_height = 12.5F,
  };

  auto params = WireBytes {};
  params.Floats(11.5F, 2).Zeros(72);

  EXPECT_EQ(Encoded(shape), ShapeBytes(2U, params).Bytes());
}

//! A box shape is the common shape fields with the box arm: half extents and a
//! zeroed reserve.
NOLINT_TEST(PakFormatLayoutPhysicsTest, CollisionShapeBoxArmWireBytes)
{
  auto shape = MakeShape(physics::ShapeType::kBox);
  shape.shape_params.box = physics::ShapeParams::BoxParams {
    .half_extents = { 11.5F, 12.5F, 13.5F, },
  };

  auto params = WireBytes {};
  params.Floats(11.5F, 3).Zeros(68);

  EXPECT_EQ(Encoded(shape), ShapeBytes(3U, params).Bytes());
}

//! A cylinder shape is the common shape fields with the cylinder arm: radius,
//! half height and a zeroed reserve.
NOLINT_TEST(PakFormatLayoutPhysicsTest, CollisionShapeCylinderArmWireBytes)
{
  auto shape = MakeShape(physics::ShapeType::kCylinder);
  shape.shape_params.cylinder = physics::ShapeParams::CylinderParams {
    .radius = 11.5F,
    .half_height = 12.5F,
  };

  auto params = WireBytes {};
  params.Floats(11.5F, 2).Zeros(72);

  EXPECT_EQ(Encoded(shape), ShapeBytes(4U, params).Bytes());
}

//! A cone shape is the common shape fields with the cone arm: radius, half
//! height and a zeroed reserve.
NOLINT_TEST(PakFormatLayoutPhysicsTest, CollisionShapeConeArmWireBytes)
{
  auto shape = MakeShape(physics::ShapeType::kCone);
  shape.shape_params.cone = physics::ShapeParams::ConeParams {
    .radius = 11.5F,
    .half_height = 12.5F,
  };

  auto params = WireBytes {};
  params.Floats(11.5F, 2).Zeros(72);

  EXPECT_EQ(Encoded(shape), ShapeBytes(5U, params).Bytes());
}

//! A convex hull shape is the common shape fields with the convex hull arm,
//! which holds only a zeroed reserve.
NOLINT_TEST(PakFormatLayoutPhysicsTest, CollisionShapeConvexHullArmWireBytes)
{
  auto shape = MakeShape(physics::ShapeType::kConvexHull);
  shape.shape_params.convex_hull = physics::ShapeParams::ConvexHullParams {};

  auto params = WireBytes {};
  params.Zeros(80);

  EXPECT_EQ(Encoded(shape), ShapeBytes(6U, params).Bytes());
}

//! A triangle mesh shape is the common shape fields with the triangle mesh
//! arm, which holds only a zeroed reserve.
NOLINT_TEST(PakFormatLayoutPhysicsTest, CollisionShapeTriangleMeshArmWireBytes)
{
  auto shape = MakeShape(physics::ShapeType::kTriangleMesh);
  shape.shape_params.triangle_mesh
    = physics::ShapeParams::TriangleMeshParams {};

  auto params = WireBytes {};
  params.Zeros(80);

  EXPECT_EQ(Encoded(shape), ShapeBytes(7U, params).Bytes());
}

//! A height field shape is the common shape fields with the height field arm,
//! which holds only a zeroed reserve.
NOLINT_TEST(PakFormatLayoutPhysicsTest, CollisionShapeHeightFieldArmWireBytes)
{
  auto shape = MakeShape(physics::ShapeType::kHeightField);
  shape.shape_params.height_field = physics::ShapeParams::HeightFieldParams {};

  auto params = WireBytes {};
  params.Zeros(80);

  EXPECT_EQ(Encoded(shape), ShapeBytes(8U, params).Bytes());
}

//! A plane shape is the common shape fields with the plane arm: normal,
//! distance and a zeroed reserve.
NOLINT_TEST(PakFormatLayoutPhysicsTest, CollisionShapePlaneArmWireBytes)
{
  auto shape = MakeShape(physics::ShapeType::kPlane);
  shape.shape_params.plane = physics::ShapeParams::PlaneParams {
    .normal = { 11.5F, 12.5F, 13.5F, },
    .distance = 14.5F,
  };

  auto params = WireBytes {};
  params.Floats(11.5F, 4).Zeros(64);

  EXPECT_EQ(Encoded(shape), ShapeBytes(9U, params).Bytes());
}

//! A world boundary shape is the common shape fields with the world boundary
//! arm: boundary mode, minimum and maximum limits and a zeroed reserve.
NOLINT_TEST(PakFormatLayoutPhysicsTest, CollisionShapeWorldBoundaryArmWireBytes)
{
  auto shape = MakeShape(physics::ShapeType::kWorldBoundary);
  shape.shape_params.world_boundary
    = physics::ShapeParams::WorldBoundaryParams {
        .boundary_mode = physics::WorldBoundaryMode::kPlaneSet,
        .limits_min = { 11.5F, 12.5F, 13.5F, },
        .limits_max = { 14.5F, 15.5F, 16.5F, },
      };

  auto params = WireBytes {};
  params.U32(2U).Floats(11.5F, 6).Zeros(52);

  EXPECT_EQ(Encoded(shape), ShapeBytes(10U, params).Bytes());
}

//! A compound shape is the common shape fields with the compound arm: child
//! count, child byte offset and a zeroed reserve.
NOLINT_TEST(PakFormatLayoutPhysicsTest, CollisionShapeCompoundArmWireBytes)
{
  auto shape = MakeShape(physics::ShapeType::kCompound);
  shape.shape_params.compound = physics::ShapeParams::CompoundParams {
    .child_count = 0x61626364U,
    .child_byte_offset = 0x71727374U,
  };

  auto params = WireBytes {};
  params.U32(0x61626364U).U32(0x71727374U).Zeros(72);

  EXPECT_EQ(Encoded(shape), ShapeBytes(11U, params).Bytes());
}

//! A physics component table descriptor is its binding type and resource
//! table.
NOLINT_TEST(PakFormatLayoutPhysicsTest, PhysicsComponentTableDescWireBytes)
{
  const auto desc = physics::PhysicsComponentTableDesc {
    .binding_type = physics::PhysicsBindingType::kCollider,
    .table = { .offset = 0x0102030405060708U,
      .count = 0x11121314U,
      .entry_size = 0x21222324U, },
  };

  const auto expected = WireBytes {}
                          .U32(0x4C4F4350U)
                          .U64(0x0102030405060708U)
                          .U32(0x11121314U)
                          .U32(0x21222324U);

  EXPECT_EQ(Encoded(desc), expected.Bytes());
}

//! A physics scene is its header, target scene key, node count, component
//! table count and directory offset, the target scene's content hash, and the
//! backend it was cooked for.
NOLINT_TEST(PakFormatLayoutPhysicsTest, PhysicsSceneAssetDescWireBytes)
{
  auto scene = physics::PhysicsSceneAssetDesc {
    .header = MakeHeader(),
    .target_scene_key = Key(0x10U),
    .target_node_count = 0x21222324U,
    .component_table_count = 0x31323334U,
    .component_table_directory_offset = 0x4142434445464748U,
    .backend = physics::PhysicsBackend::kPhysX,
  };
  std::ranges::copy(
    Digest(0x70U), std::ranges::begin(scene.target_scene_content_hash));

  auto expected = WireBytes {};
  expected.Append(HeaderBytes())
    .Run(0x10U, 16)
    .U32(0x21222324U)
    .U32(0x31323334U)
    .U64(0x4142434445464748U)
    .Run(0x70U, 32)
    .U8(static_cast<uint64_t>(physics::PhysicsBackend::kPhysX));

  EXPECT_EQ(Encoded(scene), expected.Bytes());
}

//! A rigid body binding with distinct values; each test sets the backend arm.
auto MakeRigidBody() -> physics::RigidBodyBindingRecord
{
  return physics::RigidBodyBindingRecord {
    .node_index = 0x11121314U,
    .body_type = physics::PhysicsBodyType::kKinematic,
    .motion_quality = physics::PhysicsMotionQuality::kLinearCast,
    .collision_layer = 0x2122U,
    .collision_mask = 0x31323334U,
    .mass = 1.5F,
    .linear_damping = 2.5F,
    .angular_damping = 3.5F,
    .gravity_factor = 4.5F,
    .initial_activation = 0x41424344U,
    .is_sensor = 0x51525354U,
    .shape_asset_key = Key(0x60U),
    .material_asset_key = Key(0x70U),
    .com_override = { 5.5F, 6.5F, 7.5F, },
    .has_com_override = 0x81828384U,
    .inertia_override = { 8.5F, 9.5F, 10.5F, },
    .has_inertia_override = 0x91929394U,
    .max_linear_velocity = 11.5F,
    .max_angular_velocity = 12.5F,
    .allowed_dof_flags = 0xA1A2A3A4U,
  };
}

//! The documented bytes of the binding `MakeRigidBody` returns, followed by
//! the 10 bytes of `scalars`.
auto RigidBodyBytes(const WireBytes& scalars) -> WireBytes
{
  auto bytes = WireBytes {};
  bytes.U32(0x11121314U)
    .U8(2U)
    .U8(1U)
    .U16(0x2122U)
    .U32(0x31323334U)
    .Floats(1.5F, 4)
    .U32(0x41424344U)
    .U32(0x51525354U)
    .Run(0x60U, 16)
    .Run(0x70U, 16)
    .Floats(5.5F, 3)
    .U32(0x81828384U)
    .Floats(8.5F, 3)
    .U32(0x91929394U)
    .Floats(11.5F, 2)
    .U32(0xA1A2A3A4U)
    .Append(scalars);
  return bytes;
}

//! A rigid body binding with the Jolt backend arm: velocity and position step
//! overrides and a zeroed reserve.
NOLINT_TEST(PakFormatLayoutPhysicsTest, RigidBodyBindingJoltArmWireBytes)
{
  auto record = MakeRigidBody();
  record.backend_scalars.jolt = physics::RigidBodyBackendScalars::JoltScalars {
    .num_velocity_steps_override = 0x31U,
    .num_position_steps_override = 0x32U,
  };

  auto scalars = WireBytes {};
  scalars.U8(0x31U).U8(0x32U).Zeros(8);

  EXPECT_EQ(Encoded(record), RigidBodyBytes(scalars).Bytes());
}

//! A rigid body binding with the PhysX backend arm: minimum velocity and
//! position iterations, maximum contact impulse and contact report threshold.
NOLINT_TEST(PakFormatLayoutPhysicsTest, RigidBodyBindingPhysXArmWireBytes)
{
  auto record = MakeRigidBody();
  record.backend_scalars.physx
    = physics::RigidBodyBackendScalars::PhysXScalars {
        .min_velocity_iters = 0x41U,
        .min_position_iters = 0x42U,
        .max_contact_impulse = 13.5F,
        .contact_report_threshold = 14.5F,
      };

  auto scalars = WireBytes {};
  scalars.U8(0x41U).U8(0x42U).F32(13.5F).F32(14.5F);

  EXPECT_EQ(Encoded(record), RigidBodyBytes(scalars).Bytes());
}

//! A collider binding is its node index, shape and material keys, collision
//! layer and mask, and sensor flag.
NOLINT_TEST(PakFormatLayoutPhysicsTest, ColliderBindingRecordWireBytes)
{
  const auto record = physics::ColliderBindingRecord {
    .node_index = 0x11121314U,
    .shape_asset_key = Key(0x20U),
    .material_asset_key = Key(0x30U),
    .collision_layer = 0x4142U,
    .collision_mask = 0x51525354U,
    .is_sensor = 0x61626364U,
  };

  const auto expected = WireBytes {}
                          .U32(0x11121314U)
                          .Run(0x20U, 16)
                          .Run(0x30U, 16)
                          .U16(0x4142U)
                          .U32(0x51525354U)
                          .U32(0x61626364U);

  EXPECT_EQ(Encoded(record), expected.Bytes());
}

//! A character binding with distinct values; each test sets the backend arm.
auto MakeCharacter() -> physics::CharacterBindingRecord
{
  return physics::CharacterBindingRecord {
    .node_index = 0x11121314U,
    .shape_asset_key = Key(0x20U),
    .mass = 1.5F,
    .max_slope_angle = 2.5F,
    .step_height = 3.5F,
    .step_down_distance = 4.5F,
    .max_strength = 5.5F,
    .skin_width = 6.5F,
    .predictive_contact_distance = 7.5F,
    .collision_layer = 0x3132U,
    .collision_mask = 0x41424344U,
    .inner_shape_asset_key = Key(0x50U),
  };
}

//! The documented bytes of the binding `MakeCharacter` returns, followed by
//! the 12 bytes of `scalars`.
auto CharacterBytes(const WireBytes& scalars) -> WireBytes
{
  auto bytes = WireBytes {};
  bytes.U32(0x11121314U)
    .Run(0x20U, 16)
    .Floats(1.5F, 7)
    .U16(0x3132U)
    .U32(0x41424344U)
    .Run(0x50U, 16)
    .Append(scalars);
  return bytes;
}

//! A character binding with the Jolt backend arm: penetration recovery speed,
//! maximum hit count and hit reduction angle.
NOLINT_TEST(PakFormatLayoutPhysicsTest, CharacterBindingJoltArmWireBytes)
{
  auto record = MakeCharacter();
  record.backend_scalars.jolt = physics::CharacterBackendScalars::JoltScalars {
    .penetration_recovery_speed = 8.5F,
    .max_num_hits = 0x61626364U,
    .hit_reduction_cos_max_angle = 9.5F,
  };

  auto scalars = WireBytes {};
  scalars.F32(8.5F).U32(0x61626364U).F32(9.5F);

  EXPECT_EQ(Encoded(record), CharacterBytes(scalars).Bytes());
}

//! A character binding with the PhysX backend arm: contact offset and a zeroed
//! reserve.
NOLINT_TEST(PakFormatLayoutPhysicsTest, CharacterBindingPhysXArmWireBytes)
{
  auto record = MakeCharacter();
  record.backend_scalars.physx
    = physics::CharacterBackendScalars::PhysXScalars {
        .contact_offset = 8.5F,
      };

  auto scalars = WireBytes {};
  scalars.F32(8.5F).Zeros(8);

  EXPECT_EQ(Encoded(record), CharacterBytes(scalars).Bytes());
}

//! A soft body binding with distinct values; each test sets the backend arm.
auto MakeSoftBody() -> physics::SoftBodyBindingRecord
{
  return physics::SoftBodyBindingRecord {
    .node_index = 0x11121314U,
    .edge_compliance = 1.5F,
    .shear_compliance = 2.5F,
    .bend_compliance = 3.5F,
    .volume_compliance = 4.5F,
    .pressure_coefficient = 5.5F,
    .global_damping = 6.5F,
    .restitution = 7.5F,
    .friction = 8.5F,
    .vertex_radius = 9.5F,
    .tether_max_distance_multiplier = 10.5F,
    .solver_iteration_count = 0x21222324U,
    .collision_layer = 0x3132U,
    .collision_mask = 0x41424344U,
    .topology_asset_key = Key(0x50U),
    .pinned_vertex_count = 0x61626364U,
    .pinned_vertex_byte_offset = 0x71727374U,
    .kinematic_vertex_count = 0x81828384U,
    .kinematic_vertex_byte_offset = 0x91929394U,
    .tether_mode = physics::SoftBodyTetherMode::kGeodesic,
    .topology_format
    = physics::PhysicsResourceFormat::kPhysXSoftBodySettingsBinary,
    .self_collision = 0xA1U,
  };
}

//! The documented bytes of the binding `MakeSoftBody` returns, with the 12
//! bytes of `scalars` between the kinematic vertices and the tether mode.
auto SoftBodyBytes(const WireBytes& scalars) -> WireBytes
{
  auto bytes = WireBytes {};
  bytes.U32(0x11121314U)
    .Floats(1.5F, 10)
    .U32(0x21222324U)
    .U16(0x3132U)
    .U32(0x41424344U)
    .Run(0x50U, 16)
    .U32(0x61626364U)
    .U32(0x71727374U)
    .U32(0x81828384U)
    .U32(0x91929394U)
    .Append(scalars)
    .U8(2U)
    .U8(4U)
    .U8(0xA1U);
  return bytes;
}

//! A soft body binding with the Jolt backend arm: velocity and position step
//! counts and gravity factor.
NOLINT_TEST(PakFormatLayoutPhysicsTest, SoftBodyBindingJoltArmWireBytes)
{
  auto record = MakeSoftBody();
  record.backend_scalars.jolt = physics::SoftBodyBackendScalars::JoltScalars {
    .num_velocity_steps = 0xB1B2B3B4U,
    .num_position_steps = 0xC1C2C3C4U,
    .gravity_factor = 11.5F,
  };

  auto scalars = WireBytes {};
  scalars.U32(0xB1B2B3B4U).U32(0xC1C2C3C4U).F32(11.5F);

  EXPECT_EQ(Encoded(record), SoftBodyBytes(scalars).Bytes());
}

//! A soft body binding with the PhysX FEM backend arm: Young's modulus,
//! Poisson's ratio and dynamic friction.
NOLINT_TEST(PakFormatLayoutPhysicsTest, SoftBodyBindingPhysXArmWireBytes)
{
  auto record = MakeSoftBody();
  record.backend_scalars.physx
    = physics::SoftBodyBackendScalars::PhysXFemScalars {
        .youngs_modulus = 11.5F,
        .poissons = 12.5F,
        .dynamic_friction = 13.5F,
      };

  auto scalars = WireBytes {};
  scalars.Floats(11.5F, 3);

  EXPECT_EQ(Encoded(record), SoftBodyBytes(scalars).Bytes());
}

//! A joint binding with distinct values; each test sets the backend arm.
auto MakeJoint() -> physics::JointBindingRecord
{
  return physics::JointBindingRecord {
    .node_index_a = 0x11121314U,
    .node_index_b = 0x21222324U,
    .constraint_asset_key = Key(0x30U),
  };
}

//! The documented bytes of the binding `MakeJoint` returns, followed by the 16
//! bytes of `scalars`.
auto JointBytes(const WireBytes& scalars) -> WireBytes
{
  auto bytes = WireBytes {};
  bytes.U32(0x11121314U).U32(0x21222324U).Run(0x30U, 16).Append(scalars);
  return bytes;
}

//! A joint binding with the Jolt backend arm: velocity and position step
//! overrides and a zeroed reserve.
NOLINT_TEST(PakFormatLayoutPhysicsTest, JointBindingJoltArmWireBytes)
{
  auto record = MakeJoint();
  record.backend_scalars.jolt = physics::JointBackendScalars::JoltScalars {
    .num_velocity_steps_override = 0x41U,
    .num_position_steps_override = 0x42U,
  };

  auto scalars = WireBytes {};
  scalars.U8(0x41U).U8(0x42U).Zeros(14);

  EXPECT_EQ(Encoded(record), JointBytes(scalars).Bytes());
}

//! A joint binding with the PhysX backend arm: two inverse mass scales and two
//! inverse inertia scales.
NOLINT_TEST(PakFormatLayoutPhysicsTest, JointBindingPhysXArmWireBytes)
{
  auto record = MakeJoint();
  record.backend_scalars.physx = physics::JointBackendScalars::PhysXScalars {
    .inv_mass_scale0 = 1.5F,
    .inv_mass_scale1 = 2.5F,
    .inv_inertia_scale0 = 3.5F,
    .inv_inertia_scale1 = 4.5F,
  };

  auto scalars = WireBytes {};
  scalars.Floats(1.5F, 4);

  EXPECT_EQ(Encoded(record), JointBytes(scalars).Bytes());
}

//! A vehicle binding is its node index, constraint key, controller type and
//! wheel slice offset and count.
NOLINT_TEST(PakFormatLayoutPhysicsTest, VehicleBindingRecordWireBytes)
{
  const auto record = physics::VehicleBindingRecord {
    .node_index = 0x11121314U,
    .constraint_asset_key = Key(0x20U),
    .controller_type = physics::VehicleControllerType::kTracked,
    .wheel_slice_offset = 0x31323334U,
    .wheel_slice_count = 0x41424344U,
  };

  const auto expected = WireBytes {}
                          .U32(0x11121314U)
                          .Run(0x20U, 16)
                          .U32(1U)
                          .U32(0x31323334U)
                          .U32(0x41424344U);

  EXPECT_EQ(Encoded(record), expected.Bytes());
}

//! A vehicle wheel binding with distinct values; each test sets the backend
//! arm.
auto MakeWheel() -> physics::VehicleWheelBindingRecord
{
  return physics::VehicleWheelBindingRecord {
    .vehicle_node_index = 0x11121314U,
    .wheel_node_index = 0x21222324U,
    .axle_index = 0x3132U,
    .side = physics::VehicleWheelSide::kRight,
  };
}

//! The documented bytes of the binding `MakeWheel` returns, followed by the 4
//! bytes of `scalars`.
auto WheelBytes(const WireBytes& scalars) -> WireBytes
{
  auto bytes = WireBytes {};
  bytes.U32(0x11121314U).U32(0x21222324U).U16(0x3132U).U8(1U).Append(scalars);
  return bytes;
}

//! A vehicle wheel binding with the Jolt backend arm: wheel castor.
NOLINT_TEST(PakFormatLayoutPhysicsTest, VehicleWheelBindingJoltArmWireBytes)
{
  auto record = MakeWheel();
  record.backend_scalars.jolt
    = physics::VehicleWheelBackendScalars::JoltScalars {
        .wheel_castor = 1.5F,
      };

  auto scalars = WireBytes {};
  scalars.F32(1.5F);

  EXPECT_EQ(Encoded(record), WheelBytes(scalars).Bytes());
}

//! A vehicle wheel binding with the PhysX backend arm, which holds only a
//! zeroed reserve.
NOLINT_TEST(PakFormatLayoutPhysicsTest, VehicleWheelBindingPhysXArmWireBytes)
{
  auto record = MakeWheel();
  record.backend_scalars.physx
    = physics::VehicleWheelBackendScalars::PhysXScalars {};

  auto scalars = WireBytes {};
  scalars.Zeros(4);

  EXPECT_EQ(Encoded(record), WheelBytes(scalars).Bytes());
}

//! An aggregate binding is its node index, body limit, overlap filter flag and
//! authority.
NOLINT_TEST(PakFormatLayoutPhysicsTest, AggregateBindingRecordWireBytes)
{
  const auto record = physics::AggregateBindingRecord {
    .node_index = 0x11121314U,
    .max_bodies = 0x21222324U,
    .filter_overlap = 0x31323334U,
    .authority = physics::AggregateAuthority::kCommand,
  };

  const auto expected
    = WireBytes {}.U32(0x11121314U).U32(0x21222324U).U32(0x31323334U).U8(1U);

  EXPECT_EQ(Encoded(record), expected.Bytes());
}

} // namespace
