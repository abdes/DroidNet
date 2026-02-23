
//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/PhysicsSceneLoader.h>
#include <Oxygen/Content/SourceToken.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Testing/GTest.h>

#include "Fixtures/PhysicsLoaderTestFixtures.h"

using oxygen::content::loaders::LoadPhysicsSceneAsset;

namespace {

namespace pak7 = oxygen::data::pak::v7;

class PhysicsSceneLoaderHappyPathTest
  : public oxygen::content::testing::PhysicsSceneFixtureBase { };

class PhysicsSceneLoaderValidationTest
  : public oxygen::content::testing::PhysicsSceneFixtureBase { };

class PhysicsSceneLoaderTypeTest
  : public oxygen::content::testing::PhysicsSceneFixtureBase { };

// ============================================================================
// Tests — happy path
// ============================================================================

NOLINT_TEST_F(
  PhysicsSceneLoaderHappyPathTest, Load_Minimal_ZeroBindings_Succeeds)
{
  WriteAs(writer_, MakeMinimalDesc());
  ASSERT_TRUE(writer_.Flush());

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

NOLINT_TEST_F(PhysicsSceneLoaderHappyPathTest, Load_OneRigidBodyRecord_Succeeds)
{
  pak7::RigidBodyBindingRecord rec {};
  rec.node_index = 3;
  rec.body_type = pak7::PhysicsBodyType::kKinematic;
  rec.mass = 42.0F;

  WriteOneRigidBodyTable(rec);

  auto asset = LoadPhysicsSceneAsset(MakeContext());

  ASSERT_NE(asset, nullptr);
  auto rigid = asset->GetBindings<pak7::RigidBodyBindingRecord>();
  ASSERT_EQ(rigid.size(), 1U);
  EXPECT_EQ(rigid[0].node_index, 3U);
  EXPECT_EQ(rigid[0].body_type, pak7::PhysicsBodyType::kKinematic);
  EXPECT_FLOAT_EQ(rigid[0].mass, 42.0F);
}

NOLINT_TEST_F(
  PhysicsSceneLoaderHappyPathTest, Load_MultipleBindingTypes_Succeeds)
{
  // Layout:
  //   [PhysicsSceneAssetDesc 256B]
  //   [PhysicsComponentTableDesc 20B] x2  (dir at offset 256, size 40B)
  //   [RigidBodyBindingRecord 64B]         (at offset 296)
  //   [ColliderBindingRecord  32B]         (at offset 360)

  constexpr uint32_t kDescSize = sizeof(pak7::PhysicsSceneAssetDesc); // 256
  constexpr uint32_t kEntrySize = sizeof(pak7::PhysicsComponentTableDesc); // 20
  constexpr uint32_t kDirOffset = kDescSize; // 256
  constexpr uint32_t kRigidOffset = kDirOffset + 2 * kEntrySize; // 296
  constexpr uint32_t kColliderOffset
    = kRigidOffset + sizeof(pak7::RigidBodyBindingRecord); // 360

  pak7::PhysicsSceneAssetDesc desc = MakeMinimalDesc();
  desc.component_table_count = 2;
  desc.component_table_directory_offset = kDirOffset;

  pak7::PhysicsComponentTableDesc rigid_entry {};
  rigid_entry.binding_type = pak7::PhysicsBindingType::kRigidBody;
  rigid_entry.table.offset = kRigidOffset;
  rigid_entry.table.count = 1;
  rigid_entry.table.entry_size = sizeof(pak7::RigidBodyBindingRecord);

  pak7::PhysicsComponentTableDesc collider_entry {};
  collider_entry.binding_type = pak7::PhysicsBindingType::kCollider;
  collider_entry.table.offset = kColliderOffset;
  collider_entry.table.count = 1;
  collider_entry.table.entry_size = sizeof(pak7::ColliderBindingRecord);

  pak7::RigidBodyBindingRecord rb {};
  rb.node_index = 1;

  pak7::ColliderBindingRecord col {};
  col.node_index = 2;

  WriteAs(writer_, desc);
  WriteAs(writer_, rigid_entry);
  WriteAs(writer_, collider_entry);
  WriteAs(writer_, rb);
  WriteAs(writer_, col);
  ASSERT_TRUE(writer_.Flush());

  auto asset = LoadPhysicsSceneAsset(MakeContext());

  ASSERT_NE(asset, nullptr);
  EXPECT_EQ(asset->GetBindings<pak7::RigidBodyBindingRecord>().size(), 1U);
  EXPECT_EQ(asset->GetBindings<pak7::ColliderBindingRecord>().size(), 1U);
  EXPECT_EQ(
    asset->GetBindings<pak7::RigidBodyBindingRecord>()[0].node_index, 1U);
  EXPECT_EQ(
    asset->GetBindings<pak7::ColliderBindingRecord>()[0].node_index, 2U);
}

NOLINT_TEST_F(PhysicsSceneLoaderHappyPathTest, Load_AllBindingTypes_Succeeds)
{
  constexpr uint32_t kDescSize = sizeof(pak7::PhysicsSceneAssetDesc);
  constexpr uint32_t kEntrySize = sizeof(pak7::PhysicsComponentTableDesc);
  constexpr uint32_t kTableCount = 7;
  constexpr uint32_t kDirOffset = kDescSize;
  constexpr uint32_t kDataBaseOffset = kDirOffset + (kTableCount * kEntrySize);

  pak7::RigidBodyBindingRecord rb {};
  rb.node_index = 11;
  rb.mass = 3.25F;

  pak7::ColliderBindingRecord col {};
  col.node_index = 12;
  col.collision_layer = 7;

  pak7::CharacterBindingRecord chr {};
  chr.node_index = 13;
  chr.max_strength = 22.5F;

  pak7::SoftBodyBindingRecord soft {};
  soft.node_index = 14;
  soft.cluster_count = 9;

  pak7::JointBindingRecord joint {};
  joint.node_index_a = 15;
  joint.node_index_b = 16;

  pak7::VehicleBindingRecord veh {};
  veh.node_index = 17;

  pak7::AggregateBindingRecord agg {};
  agg.node_index = 18;
  agg.max_bodies = 24;

  uint32_t cursor = kDataBaseOffset;
  const auto add_entry
    = [&](const pak7::PhysicsBindingType type,
        const uint32_t size) -> pak7::PhysicsComponentTableDesc {
    pak7::PhysicsComponentTableDesc e {};
    e.binding_type = type;
    e.table.offset = cursor;
    e.table.count = 1;
    e.table.entry_size = size;
    cursor += size;
    return e;
  };

  auto desc = MakeMinimalDesc();
  desc.component_table_count = kTableCount;
  desc.component_table_directory_offset = kDirOffset;

  std::array<pak7::PhysicsComponentTableDesc, kTableCount> entries {
    add_entry(pak7::PhysicsBindingType::kRigidBody,
      sizeof(pak7::RigidBodyBindingRecord)),
    add_entry(
      pak7::PhysicsBindingType::kCollider, sizeof(pak7::ColliderBindingRecord)),
    add_entry(pak7::PhysicsBindingType::kCharacter,
      sizeof(pak7::CharacterBindingRecord)),
    add_entry(
      pak7::PhysicsBindingType::kSoftBody, sizeof(pak7::SoftBodyBindingRecord)),
    add_entry(
      pak7::PhysicsBindingType::kJoint, sizeof(pak7::JointBindingRecord)),
    add_entry(
      pak7::PhysicsBindingType::kVehicle, sizeof(pak7::VehicleBindingRecord)),
    add_entry(pak7::PhysicsBindingType::kAggregate,
      sizeof(pak7::AggregateBindingRecord)),
  };

  WriteAs(writer_, desc);
  for (const auto& entry : entries) {
    WriteAs(writer_, entry);
  }
  WriteAs(writer_, rb);
  WriteAs(writer_, col);
  WriteAs(writer_, chr);
  WriteAs(writer_, soft);
  WriteAs(writer_, joint);
  WriteAs(writer_, veh);
  WriteAs(writer_, agg);
  ASSERT_TRUE(writer_.Flush());

  auto asset = LoadPhysicsSceneAsset(MakeContext());
  ASSERT_NE(asset, nullptr);

  ASSERT_EQ(asset->GetBindings<pak7::RigidBodyBindingRecord>().size(), 1U);
  ASSERT_EQ(asset->GetBindings<pak7::ColliderBindingRecord>().size(), 1U);
  ASSERT_EQ(asset->GetBindings<pak7::CharacterBindingRecord>().size(), 1U);
  ASSERT_EQ(asset->GetBindings<pak7::SoftBodyBindingRecord>().size(), 1U);
  ASSERT_EQ(asset->GetBindings<pak7::JointBindingRecord>().size(), 1U);
  ASSERT_EQ(asset->GetBindings<pak7::VehicleBindingRecord>().size(), 1U);
  ASSERT_EQ(asset->GetBindings<pak7::AggregateBindingRecord>().size(), 1U);

  EXPECT_EQ(
    asset->GetBindings<pak7::RigidBodyBindingRecord>()[0].node_index, 11U);
  EXPECT_EQ(
    asset->GetBindings<pak7::ColliderBindingRecord>()[0].node_index, 12U);
  EXPECT_EQ(
    asset->GetBindings<pak7::CharacterBindingRecord>()[0].node_index, 13U);
  EXPECT_EQ(
    asset->GetBindings<pak7::SoftBodyBindingRecord>()[0].node_index, 14U);
  EXPECT_EQ(
    asset->GetBindings<pak7::JointBindingRecord>()[0].node_index_a, 15U);
  EXPECT_EQ(
    asset->GetBindings<pak7::JointBindingRecord>()[0].node_index_b, 16U);
  EXPECT_EQ(
    asset->GetBindings<pak7::VehicleBindingRecord>()[0].node_index, 17U);
  EXPECT_EQ(
    asset->GetBindings<pak7::AggregateBindingRecord>()[0].node_index, 18U);
}

// ============================================================================
// Tests — validation / error paths
// ============================================================================

NOLINT_TEST_F(PhysicsSceneLoaderValidationTest, Load_WrongAssetType_Throws)
{
  auto desc = MakeMinimalDesc();
  desc.header.asset_type
    = static_cast<uint8_t>(oxygen::data::AssetType::kScene); // wrong
  WriteAs(writer_, desc);
  ASSERT_TRUE(writer_.Flush());

  EXPECT_THROW(
    { (void)LoadPhysicsSceneAsset(MakeContext()); }, std::runtime_error);
}

NOLINT_TEST_F(PhysicsSceneLoaderValidationTest, Load_WrongVersion_Throws)
{
  auto desc = MakeMinimalDesc();
  desc.header.version = 0xFF; // unsupported
  WriteAs(writer_, desc);
  ASSERT_TRUE(writer_.Flush());

  EXPECT_THROW(
    { (void)LoadPhysicsSceneAsset(MakeContext()); }, std::runtime_error);
}

NOLINT_TEST_F(PhysicsSceneLoaderValidationTest, Load_TruncatedDescriptor_Throws)
{
  // Write only half of a PhysicsSceneAssetDesc (stream too short).
  std::vector<std::byte> half(
    sizeof(pak7::PhysicsSceneAssetDesc) / 2, std::byte { 0 });
  ASSERT_TRUE(writer_.WriteBlob(std::span<const std::byte>(half)));
  ASSERT_TRUE(writer_.Flush());

  EXPECT_THROW(
    { (void)LoadPhysicsSceneAsset(MakeContext()); }, std::runtime_error);
}

NOLINT_TEST_F(PhysicsSceneLoaderValidationTest, Load_EntrySizeMismatch_Throws)
{
  struct Case {
    pak7::PhysicsBindingType type;
    uint32_t expected_size;
  };

  const std::array<Case, 7> cases { {
    { pak7::PhysicsBindingType::kRigidBody,
      sizeof(pak7::RigidBodyBindingRecord) },
    { pak7::PhysicsBindingType::kCollider,
      sizeof(pak7::ColliderBindingRecord) },
    { pak7::PhysicsBindingType::kCharacter,
      sizeof(pak7::CharacterBindingRecord) },
    { pak7::PhysicsBindingType::kSoftBody,
      sizeof(pak7::SoftBodyBindingRecord) },
    { pak7::PhysicsBindingType::kJoint, sizeof(pak7::JointBindingRecord) },
    { pak7::PhysicsBindingType::kVehicle, sizeof(pak7::VehicleBindingRecord) },
    { pak7::PhysicsBindingType::kAggregate,
      sizeof(pak7::AggregateBindingRecord) },
  } };

  for (const auto& tc : cases) {
    MockStream local_stream {};
    Writer local_writer(local_stream);
    Reader local_reader(local_stream);

    constexpr uint32_t kDirOffset = sizeof(pak7::PhysicsSceneAssetDesc);
    constexpr uint32_t kDataOffset
      = kDirOffset + sizeof(pak7::PhysicsComponentTableDesc);

    auto desc = MakeMinimalDesc();
    desc.component_table_count = 1;
    desc.component_table_directory_offset = kDirOffset;

    pak7::PhysicsComponentTableDesc entry {};
    entry.binding_type = tc.type;
    entry.table.offset = kDataOffset;
    entry.table.count = 1;
    entry.table.entry_size = tc.expected_size + 1; // force mismatch

    WriteAs(local_writer, desc);
    WriteAs(local_writer, entry);
    std::vector<std::byte> payload(entry.table.entry_size, std::byte { 0 });
    ASSERT_TRUE(local_writer.WriteBlob(std::span<const std::byte>(payload)));
    ASSERT_TRUE(local_writer.Flush());

    ASSERT_TRUE(local_stream.Seek(0));
    const auto context = oxygen::content::LoaderContext {
      .current_asset_key = oxygen::data::AssetKey {},
      .desc_reader = &local_reader,
      .work_offline = true,
      .parse_only = true,
    };

    EXPECT_THROW({ (void)LoadPhysicsSceneAsset(context); }, std::runtime_error);
  }
}

NOLINT_TEST_F(PhysicsSceneLoaderHappyPathTest,
  Load_RigidBodyTable_DataReadable_EntryFieldsCorrect)
{
  // Verify that the data blob captured by the loader faithfully encodes the
  // serialized record; GetBindings() must return the same field values.
  pak7::RigidBodyBindingRecord rec {};
  rec.node_index = 7;
  rec.body_type = pak7::PhysicsBodyType::kDynamic;
  rec.motion_quality = pak7::PhysicsMotionQuality::kLinearCast;
  rec.mass = 10.5F;
  rec.linear_damping = 0.1F;
  rec.angular_damping = 0.2F;
  rec.gravity_factor = 0.5F;
  rec.shape_asset_index = oxygen::data::pak::ResourceIndexT { 3u };
  rec.material_asset_index = oxygen::data::pak::ResourceIndexT { 1u };

  WriteOneRigidBodyTable(rec);

  auto asset = LoadPhysicsSceneAsset(MakeContext());
  ASSERT_NE(asset, nullptr);

  auto rigid = asset->GetBindings<pak7::RigidBodyBindingRecord>();
  ASSERT_EQ(rigid.size(), 1U);
  const auto& r = rigid[0];
  EXPECT_EQ(r.node_index, 7U);
  EXPECT_EQ(r.body_type, pak7::PhysicsBodyType::kDynamic);
  EXPECT_EQ(r.motion_quality, pak7::PhysicsMotionQuality::kLinearCast);
  EXPECT_FLOAT_EQ(r.mass, 10.5F);
  EXPECT_FLOAT_EQ(r.linear_damping, 0.1F);
  EXPECT_FLOAT_EQ(r.angular_damping, 0.2F);
  EXPECT_FLOAT_EQ(r.gravity_factor, 0.5F);
  EXPECT_EQ(r.shape_asset_index, 3U);
  EXPECT_EQ(r.material_asset_index, 1U);
}

NOLINT_TEST_F(PhysicsSceneLoaderTypeTest, Load_AssetTypeIsPhysicsScene)
{
  WriteAs(writer_, MakeMinimalDesc());
  ASSERT_TRUE(writer_.Flush());

  auto asset = LoadPhysicsSceneAsset(MakeContext());
  ASSERT_NE(asset, nullptr);
  EXPECT_EQ(asset->GetTypeId(), oxygen::data::PhysicsSceneAsset::ClassTypeId());
}

} // namespace
