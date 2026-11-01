//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Cooker/testing/DescriptorDefects.h>
#include <Oxygen/Cooker/testing/GeometryBuilder.h>
#include <Oxygen/Cooker/testing/PhysicsSidecarBuilder.h>
#include <Oxygen/Cooker/testing/ProductLoad.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::cooker::testing::DescriptorDefects;
using oxygen::cooker::testing::GeometryBuilder;
using oxygen::cooker::testing::PhysicsSidecarBuilder;
using oxygen::cooker::testing::SceneBuilder;
namespace data = oxygen::data;
namespace world = oxygen::data::pak::world;
namespace physics = oxygen::data::pak::physics;
using data::schema::OverrideId;

auto Key(const char* path) -> data::AssetKey
{
  return data::AssetKey::FromVirtualPath(path);
}

//! Whether the scene reader refuses `bytes`.
auto Refused(const std::vector<std::byte>& bytes) -> bool
{
  try {
    static_cast<void>(data::SceneAsset(
      Key("/Game/Main.oscene"), std::span<const std::byte>(bytes)));
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

//! A scene with a node, a renderable with an override, a script slot and a
//! fog record: every table a defect can break.
auto Scene() -> SceneBuilder
{
  auto main = SceneBuilder { "Main" };
  const auto root = main.Node("root");
  const auto crate = main.Node("crate", root);
  main.Renderable(crate, Key("/Game/Crate.ogeo"));
  main.Override(crate, OverrideId::kGeometrySurfaceVisibility,
    { .as_bool = 1U }, data::SurfaceId::FromStableIdentity("rim"),
    oxygen::base::Sha256Digest { 1U });
  static_cast<void>(main.ScriptSlot(root, Key("/Game/Spin.oscript")));
  main.Environment(world::FogEnvironmentRecord {});
  return main;
}

//! The valid scene the defects start from reads back.
NOLINT_TEST(DescriptorDefectsTest, TheValidSceneReads)
{
  EXPECT_FALSE(Refused(Scene().Bytes()));
}

//! Each scene defect breaks one rule the scene reader checks.
NOLINT_TEST(DescriptorDefectsTest, EachSceneDefectIsRefused)
{
  const auto valid = Scene().Bytes();
  const auto refused = [&valid](const auto& defect) -> bool {
    auto bytes = valid;
    defect(bytes);
    return Refused(bytes);
  };

  EXPECT_TRUE(refused([](auto& bytes) -> void {
    DescriptorDefects::SetVersion(
      bytes, static_cast<uint8_t>(world::kSceneAssetVersion - 1U));
  }));
  EXPECT_TRUE(refused([](auto& bytes) -> void {
    DescriptorDefects::PointNodeTablePastTheEnd(bytes);
  }));
  EXPECT_TRUE(refused([](auto& bytes) -> void {
    DescriptorDefects::SetSceneFlags(bytes, 1U << 31U);
  }));
  EXPECT_TRUE(refused([](auto& bytes) -> void {
    DescriptorDefects::SetStartingCamera(bytes, { .node_index = 0U });
  }));
  EXPECT_TRUE(refused([](auto& bytes) -> void {
    auto node = DescriptorDefects::NodeAt(bytes, 1U);
    node.parent_index = 7U;
    DescriptorDefects::SetNode(bytes, 1U, node);
  }));
  EXPECT_TRUE(refused([](auto& bytes) -> void {
    auto change = DescriptorDefects::ComponentAt<world::OverrideRecord>(
      bytes, data::ComponentType::kOverride, 0U);
    change.value.as_bool = 2U;
    DescriptorDefects::SetComponent(
      bytes, data::ComponentType::kOverride, 0U, change);
  }));
  EXPECT_TRUE(refused([](auto& bytes) -> void {
    auto slot = DescriptorDefects::ScriptSlotAt(bytes, 0U);
    slot.script_asset_key = {};
    DescriptorDefects::SetScriptSlot(bytes, 0U, slot);
  }));
  EXPECT_TRUE(refused([](auto& bytes) -> void {
    DescriptorDefects::SetEnvironmentRecordSize(bytes,
      world::EnvironmentComponentType::kFog,
      sizeof(world::FogEnvironmentRecord) - 4U);
  }));
}

//! A record defect writes the record it reads back.
NOLINT_TEST(DescriptorDefectsTest, RecordsReadBackWhereTheyAreWritten)
{
  auto bytes = Scene().Bytes();
  auto node = DescriptorDefects::NodeAt(bytes, 1U);
  node.translation[0] = 4.0F;

  DescriptorDefects::SetNode(bytes, 1U, node);

  EXPECT_EQ(DescriptorDefects::NodeAt(bytes, 1U).translation[0], 4.0F);
  EXPECT_EQ(DescriptorDefects::EnvironmentRecord(
              bytes, world::EnvironmentComponentType::kFog)
              .size(),
    sizeof(world::FogEnvironmentRecord));
  EXPECT_THROW(
    static_cast<void>(DescriptorDefects::ComponentAt<world::RenderableRecord>(
      bytes, data::ComponentType::kPointLight, 0U)),
    std::invalid_argument);
}

//! The sidecar defects break its pairing with its scene, which the sidecar
//! reports, or a rule the sidecar reader checks: its type or a table.
NOLINT_TEST(DescriptorDefectsTest, SidecarDefectsBreakOneRule)
{
  auto main = SceneBuilder { "Main" };
  const auto root = main.Node("root");
  const auto scene = main.Bytes();
  auto bindings = PhysicsSidecarBuilder {};
  bindings.RigidBody(root, Key("/Game/Crate.oshape"), Key("/Game/Wood.opmat"));
  const auto valid
    = bindings.Bytes(Key("/Game/Main.oscene"), scene, "PhysicsScene");
  const auto read
    = [](const std::vector<std::byte>& bytes) -> data::PhysicsSceneAsset {
    return { Key("/Game/Main.opscene"), std::span<const std::byte>(bytes) };
  };

  auto moved = valid;
  DescriptorDefects::SetSidecarScene(moved, Key("/Game/Other.oscene"));
  auto counted = valid;
  DescriptorDefects::SetSidecarNodeCount(counted, 2U);
  auto hashed = valid;
  DescriptorDefects::CorruptSidecarSceneHash(hashed);
  auto typed = valid;
  DescriptorDefects::SetAssetType(typed, data::AssetType::kScene);
  auto sized = valid;
  DescriptorDefects::SetBindingEntrySize(sized,
    physics::PhysicsBindingType::kRigidBody,
    sizeof(physics::RigidBodyBindingRecord) + 1U);

  EXPECT_EQ(read(moved).GetTargetSceneKey(), Key("/Game/Other.oscene"));
  EXPECT_EQ(read(counted).GetTargetNodeCount(), 2U);
  EXPECT_FALSE(std::ranges::equal(read(hashed).GetTargetSceneContentHash(),
    read(valid).GetTargetSceneContentHash()));
  EXPECT_THROW(static_cast<void>(read(typed)), std::runtime_error);
  EXPECT_THROW(static_cast<void>(read(sized)), std::runtime_error);
}

//! A cleared surface leaves a geometry the product loader refuses, and only
//! a surface the geometry names can be cleared.
NOLINT_TEST(DescriptorDefectsTest, ClearedSurfaceIsRefused)
{
  const auto surface = data::SurfaceId::FromStableIdentity("crate");
  auto crate = GeometryBuilder { "Crate" };
  crate.Submesh(crate.ProceduralLod(), surface);
  auto bytes = crate.Bytes();
  const auto references = crate.References();

  DescriptorDefects::ClearSurface(bytes, surface);

  EXPECT_THROW(
    oxygen::cooker::testing::detail::CheckWithLoader("geometry", bytes,
      references, oxygen::content::loaders::LoadGeometryAsset),
    std::logic_error);
  EXPECT_THROW(
    DescriptorDefects::ClearSurface(bytes, surface), std::invalid_argument);
}

} // namespace
