
//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>

#include "Fixtures/PhysicsLoaderTestFixtures.h"

#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/Loaders/PhysicsSceneLoader.h>
#include <Oxygen/Cooker/testing/PhysicsSidecarBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::content::loaders::LoadPhysicsSceneAsset;

namespace {

namespace pak7 = oxygen::data::pak::physics;
using oxygen::cooker::testing::PhysicsSidecarBuilder;

auto ShapeKey() -> oxygen::data::AssetKey
{
  return oxygen::data::AssetKey::FromVirtualPath("/test/Shape.ocshape");
}

auto MaterialKey() -> oxygen::data::AssetKey
{
  return oxygen::data::AssetKey::FromVirtualPath("/test/Material.opmat");
}

auto ConstraintKey() -> oxygen::data::AssetKey
{
  return oxygen::data::AssetKey::FromVirtualPath("/test/Constraint.opcon");
}

class PhysicsSceneLoaderHappyPathTest
  : public oxygen::content::testing::PhysicsSceneFixtureBase { };

class PhysicsSceneLoaderTypeTest
  : public oxygen::content::testing::PhysicsSceneFixtureBase { };

// ============================================================================
// Tests — happy path
// ============================================================================

NOLINT_TEST_F(PhysicsSceneLoaderHappyPathTest, LoadMinimalZeroBindingsSucceeds)
{
  WriteBytes(SidecarBytes());

  auto asset = LoadPhysicsSceneAsset(MakeContext());

  ASSERT_NE(asset, nullptr);
  EXPECT_EQ(asset->GetBindings<pak7::RigidBodyBindingRecord>().size(), 0U);
  EXPECT_EQ(asset->GetBindings<pak7::ColliderBindingRecord>().size(), 0U);
  EXPECT_EQ(asset->GetBindings<pak7::CharacterBindingRecord>().size(), 0U);
  EXPECT_EQ(asset->GetBindings<pak7::SoftBodyBindingRecord>().size(), 0U);
  EXPECT_EQ(asset->GetBindings<pak7::JointBindingRecord>().size(), 0U);
  EXPECT_EQ(asset->GetBindings<pak7::VehicleBindingRecord>().size(), 0U);
  EXPECT_EQ(asset->GetBindings<pak7::AggregateBindingRecord>().size(), 0U);
}

NOLINT_TEST_F(PhysicsSceneLoaderHappyPathTest, LoadOneRigidBodyRecordSucceeds)
{
  constexpr uint32_t kNodeCount = 4U;
  AddNodes(kNodeCount);
  pak7::RigidBodyBindingRecord rec {};
  rec.body_type = pak7::PhysicsBodyType::kKinematic;
  rec.mass = 42.0F;
  auto bindings = PhysicsSidecarBuilder {};
  bindings.RigidBody(NodeAt(kNodeCount - 1U), ShapeKey(), MaterialKey(), rec);

  WriteBytes(SidecarBytes(bindings));

  auto asset = LoadPhysicsSceneAsset(MakeContext());

  ASSERT_NE(asset, nullptr);
  auto rigid = asset->GetBindings<pak7::RigidBodyBindingRecord>();
  ASSERT_EQ(rigid.size(), 1U);
  EXPECT_EQ(oxygen::base::CheckedAt(rigid, 0).node_index,
    NodeAt(kNodeCount - 1U).Index());
  EXPECT_EQ(oxygen::base::CheckedAt(rigid, 0).body_type,
    pak7::PhysicsBodyType::kKinematic);
  EXPECT_FLOAT_EQ(oxygen::base::CheckedAt(rigid, 0).mass, 42.0F);
}

NOLINT_TEST_F(PhysicsSceneLoaderHappyPathTest, LoadMultipleBindingTypesSucceeds)
{
  AddNodes(3U);
  auto bindings = PhysicsSidecarBuilder {};
  bindings.RigidBody(NodeAt(1U), ShapeKey(), MaterialKey());
  bindings.Collider(NodeAt(2U), ShapeKey(), MaterialKey());
  WriteBytes(SidecarBytes(bindings));

  auto asset = LoadPhysicsSceneAsset(MakeContext());

  ASSERT_NE(asset, nullptr);
  EXPECT_EQ(asset->GetBindings<pak7::RigidBodyBindingRecord>().size(), 1U);
  EXPECT_EQ(asset->GetBindings<pak7::ColliderBindingRecord>().size(), 1U);
  EXPECT_EQ(oxygen::base::CheckedAt(
              asset->GetBindings<pak7::RigidBodyBindingRecord>(), 0)
              .node_index,
    NodeAt(1U).Index());
  EXPECT_EQ(oxygen::base::CheckedAt(
              asset->GetBindings<pak7::ColliderBindingRecord>(), 0)
              .node_index,
    NodeAt(2U).Index());
}

NOLINT_TEST_F(PhysicsSceneLoaderHappyPathTest, LoadAllBindingTypesSucceeds)
{
  // The bindings name nodes 11 to 18 of the scene.
  constexpr uint32_t kFirstNode = 11U;
  AddNodes(kFirstNode + 8U);
  const auto node = [this](const uint32_t offset) -> auto {
    return NodeAt(kFirstNode + offset);
  };

  pak7::RigidBodyBindingRecord rb {};
  rb.mass = 3.25F;

  pak7::ColliderBindingRecord col {};
  col.collision_layer = 7;

  pak7::CharacterBindingRecord chr {};
  chr.max_strength = 22.5F;

  pak7::SoftBodyBindingRecord soft {};
  soft.solver_iteration_count = 9;
  auto topology_asset_key_bytes = std::array<oxygen::data::AssetKey::value_type,
    oxygen::data::AssetKey::kSizeBytes> {};
  topology_asset_key_bytes.at(0) = 42U;
  const auto topology_key
    = oxygen::data::AssetKey::FromBytes(topology_asset_key_bytes);
  soft.topology_format
    = pak7::PhysicsResourceFormat::kJoltSoftBodySharedSettingsBinary;

  pak7::AggregateBindingRecord agg {};
  agg.max_bodies = 24;

  auto bindings = PhysicsSidecarBuilder {};
  bindings.RigidBody(node(0U), ShapeKey(), MaterialKey(), rb)
    .Collider(node(1U), ShapeKey(), MaterialKey(), col)
    .Character(node(2U), ShapeKey(), chr)
    .SoftBody(node(3U), topology_key, soft)
    .Joint(node(4U), node(5U), ConstraintKey())
    .Vehicle(node(6U), ConstraintKey())
    .Aggregate(node(7U), agg);
  WriteBytes(SidecarBytes(bindings));

  auto asset = LoadPhysicsSceneAsset(MakeContext());
  ASSERT_NE(asset, nullptr);

  ASSERT_EQ(asset->GetBindings<pak7::RigidBodyBindingRecord>().size(), 1U);
  ASSERT_EQ(asset->GetBindings<pak7::ColliderBindingRecord>().size(), 1U);
  ASSERT_EQ(asset->GetBindings<pak7::CharacterBindingRecord>().size(), 1U);
  ASSERT_EQ(asset->GetBindings<pak7::SoftBodyBindingRecord>().size(), 1U);
  ASSERT_EQ(asset->GetBindings<pak7::JointBindingRecord>().size(), 1U);
  ASSERT_EQ(asset->GetBindings<pak7::VehicleBindingRecord>().size(), 1U);
  ASSERT_EQ(asset->GetBindings<pak7::AggregateBindingRecord>().size(), 1U);

  EXPECT_EQ(oxygen::base::CheckedAt(
              asset->GetBindings<pak7::RigidBodyBindingRecord>(), 0)
              .node_index,
    node(0U).Index());
  EXPECT_EQ(oxygen::base::CheckedAt(
              asset->GetBindings<pak7::ColliderBindingRecord>(), 0)
              .node_index,
    node(1U).Index());
  EXPECT_EQ(oxygen::base::CheckedAt(
              asset->GetBindings<pak7::CharacterBindingRecord>(), 0)
              .node_index,
    node(2U).Index());
  EXPECT_EQ(oxygen::base::CheckedAt(
              asset->GetBindings<pak7::SoftBodyBindingRecord>(), 0)
              .node_index,
    node(3U).Index());
  EXPECT_EQ(oxygen::base::CheckedAt(
              asset->GetBindings<pak7::SoftBodyBindingRecord>(), 0)
              .topology_asset_key,
    topology_key);
  EXPECT_EQ(oxygen::base::CheckedAt(
              asset->GetBindings<pak7::SoftBodyBindingRecord>(), 0)
              .topology_format,
    pak7::PhysicsResourceFormat::kJoltSoftBodySharedSettingsBinary);
  EXPECT_EQ(
    oxygen::base::CheckedAt(asset->GetBindings<pak7::JointBindingRecord>(), 0)
      .node_index_a,
    node(4U).Index());
  EXPECT_EQ(
    oxygen::base::CheckedAt(asset->GetBindings<pak7::JointBindingRecord>(), 0)
      .node_index_b,
    node(5U).Index());
  EXPECT_EQ(
    oxygen::base::CheckedAt(asset->GetBindings<pak7::VehicleBindingRecord>(), 0)
      .node_index,
    node(6U).Index());
  EXPECT_EQ(oxygen::base::CheckedAt(
              asset->GetBindings<pak7::AggregateBindingRecord>(), 0)
              .node_index,
    node(7U).Index());
}

NOLINT_TEST_F(PhysicsSceneLoaderHappyPathTest,
  LoadRigidBodyTableDataReadableEntryFieldsCorrect)
{
  // Verify that the data blob captured by the loader faithfully encodes the
  // serialized record; GetBindings() must return the same field values.
  constexpr uint32_t kNodeCount = 8U;
  AddNodes(kNodeCount);
  pak7::RigidBodyBindingRecord rec {};
  rec.body_type = pak7::PhysicsBodyType::kDynamic;
  rec.motion_quality = pak7::PhysicsMotionQuality::kLinearCast;
  rec.mass = 10.5F;
  rec.linear_damping = 0.1F;
  rec.angular_damping = 0.2F;
  rec.gravity_factor = 0.5F;
  const auto shape_key = oxygen::data::AssetKey::FromBytes({
    0x10,
    0x01,
    0x02,
    0x03,
    0x44,
    0x55,
    0x66,
    0x77,
    0x88,
    0x99,
    0xaa,
    0xbb,
    0xcc,
    0xdd,
    0xee,
    0x01,
  });
  const auto material_key = oxygen::data::AssetKey::FromBytes({
    0x20,
    0x11,
    0x22,
    0x33,
    0x54,
    0x65,
    0x76,
    0x87,
    0x98,
    0xa9,
    0xba,
    0xcb,
    0xdc,
    0xed,
    0xfe,
    0x02,
  });

  auto bindings = PhysicsSidecarBuilder {};
  bindings.RigidBody(NodeAt(kNodeCount - 1U), shape_key, material_key, rec);
  WriteBytes(SidecarBytes(bindings));

  auto asset = LoadPhysicsSceneAsset(MakeContext());
  ASSERT_NE(asset, nullptr);

  auto rigid = asset->GetBindings<pak7::RigidBodyBindingRecord>();
  ASSERT_EQ(rigid.size(), 1U);
  const auto& r = oxygen::base::CheckedAt(rigid, 0);
  EXPECT_EQ(r.node_index, NodeAt(kNodeCount - 1U).Index());
  EXPECT_EQ(r.body_type, pak7::PhysicsBodyType::kDynamic);
  EXPECT_EQ(r.motion_quality, pak7::PhysicsMotionQuality::kLinearCast);
  EXPECT_FLOAT_EQ(r.mass, 10.5F);
  EXPECT_FLOAT_EQ(r.linear_damping, 0.1F);
  EXPECT_FLOAT_EQ(r.angular_damping, 0.2F);
  EXPECT_FLOAT_EQ(r.gravity_factor, 0.5F);
  EXPECT_EQ(r.shape_asset_key, shape_key);
  EXPECT_EQ(r.material_asset_key, material_key);
}

NOLINT_TEST_F(PhysicsSceneLoaderTypeTest, LoadAssetTypeIsPhysicsScene)
{
  WriteBytes(SidecarBytes());

  auto asset = LoadPhysicsSceneAsset(MakeContext());
  ASSERT_NE(asset, nullptr);
  EXPECT_EQ(asset->GetTypeId(), oxygen::data::PhysicsSceneAsset::ClassTypeId());
}

} // namespace
