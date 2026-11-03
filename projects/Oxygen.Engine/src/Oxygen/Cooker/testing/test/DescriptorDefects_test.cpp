//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Cooker/testing/DescriptorDefects.h>
#include <Oxygen/Cooker/testing/GeometryBuilder.h>
#include <Oxygen/Cooker/testing/PhysicsSidecarBuilder.h>
#include <Oxygen/Cooker/testing/ProductLoad.h>
#include <Oxygen/Cooker/testing/ProductRefusal.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::cooker::testing::DescriptorDefects;
using oxygen::cooker::testing::GeometryBuilder;
using oxygen::cooker::testing::PhysicsSidecarBuilder;
using oxygen::cooker::testing::SceneBuilder;
using oxygen::cooker::testing::SceneRefusal;
namespace data = oxygen::data;
namespace world = oxygen::data::pak::world;
namespace physics = oxygen::data::pak::physics;
using data::CookedDefect;
using data::schema::OverrideId;

//! A FourCC no component type uses: 'XXXX'.
constexpr uint32_t kUnknownComponentType = 0x58585858U;

//! An environment system type the format does not define.
constexpr uint32_t kUnknownEnvironmentType = 0xFFFFU;

auto Key(const char* path) -> data::AssetKey
{
  return data::AssetKey::FromVirtualPath(path);
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
  EXPECT_EQ(SceneRefusal(Scene().Bytes()), std::nullopt);
}

//! Each scene defect breaks one rule, which the scene reader and the scene
//! loader refuse with its defect.
NOLINT_TEST(DescriptorDefectsTest, EachSceneDefectBreaksOneRule)
{
  const auto valid = Scene().Bytes();
  const auto refusal = [&valid](const auto& defect) -> auto {
    auto bytes = valid;
    defect(bytes);
    return SceneRefusal(bytes);
  };

  EXPECT_EQ(refusal([](auto& bytes) -> void {
    DescriptorDefects::SetVersion(
      bytes, static_cast<uint8_t>(world::kSceneAssetVersion - 1U));
  }),
    CookedDefect::kUnsupportedVersion);
  EXPECT_EQ(refusal([](auto& bytes) -> void {
    DescriptorDefects::PointNodeTablePastTheEnd(bytes);
  }),
    CookedDefect::kOutOfBounds);
  EXPECT_EQ(refusal([](auto& bytes) -> void {
    DescriptorDefects::SetSceneFlags(bytes, 1U << 31U);
  }),
    CookedDefect::kInvalidValue);
  EXPECT_EQ(refusal([](auto& bytes) -> void {
    DescriptorDefects::SetStartingCamera(bytes, { .node_index = 0U });
  }),
    CookedDefect::kInvalidReference);
  EXPECT_EQ(refusal([](auto& bytes) -> void {
    auto node = DescriptorDefects::NodeAt(bytes, 1U);
    node.parent_index = 7U;
    DescriptorDefects::SetNode(bytes, 1U, node);
  }),
    CookedDefect::kInvalidReference);
  EXPECT_EQ(refusal([](auto& bytes) -> void {
    auto change = DescriptorDefects::ComponentAt<world::OverrideRecord>(
      bytes, data::ComponentType::kOverride, 0U);
    change.value.as_bool = 2U;
    DescriptorDefects::SetComponent(
      bytes, data::ComponentType::kOverride, 0U, change);
  }),
    CookedDefect::kInvalidValue);
  EXPECT_EQ(refusal([](auto& bytes) -> void {
    auto slot = DescriptorDefects::ScriptSlotAt(bytes, 0U);
    slot.script_asset_key = {};
    DescriptorDefects::SetScriptSlot(bytes, 0U, slot);
  }),
    CookedDefect::kMissingPart);
  EXPECT_EQ(refusal([](auto& bytes) -> void {
    DescriptorDefects::SetComponentTableType(
      bytes, data::ComponentType::kScripting, kUnknownComponentType);
  }),
    CookedDefect::kInvalidValue);
  EXPECT_EQ(refusal([](auto& bytes) -> void {
    DescriptorDefects::SetComponentEntrySize(bytes,
      data::ComponentType::kScripting,
      sizeof(oxygen::data::pak::scripting::ScriptingComponentRecord) + 1U);
  }),
    CookedDefect::kWrongRowSize);
  EXPECT_EQ(refusal([](auto& bytes) -> void {
    DescriptorDefects::SetEnvironmentRecordSize(bytes,
      world::EnvironmentComponentType::kFog,
      sizeof(world::FogEnvironmentRecord) - 4U);
  }),
    CookedDefect::kSizeMismatch);
  EXPECT_EQ(refusal([](auto& bytes) -> void {
    DescriptorDefects::SetEnvironmentRecordType(
      bytes, world::EnvironmentComponentType::kFog, kUnknownEnvironmentType);
  }),
    CookedDefect::kInvalidValue);
  EXPECT_EQ(refusal([](auto& bytes) -> void {
    DescriptorDefects::SetEnvironmentRecordCount(bytes, 2U);
  }),
    CookedDefect::kOutOfBounds);
  EXPECT_EQ(refusal([](auto& bytes) -> void {
    DescriptorDefects::DropEnvironmentBlock(bytes);
  }),
    CookedDefect::kMissingPart);
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
//! reports, or a rule the sidecar reader checks: its type, its backend or a
//! table.
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
  auto unbacked = valid;
  DescriptorDefects::SetSidecarBackend(
    unbacked, physics::PhysicsBackend::kNone);
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
  EXPECT_EQ(read(valid).GetBackend(), physics::PhysicsBackend::kJolt);
  EXPECT_FALSE(std::ranges::equal(read(hashed).GetTargetSceneContentHash(),
    read(valid).GetTargetSceneContentHash()));
  EXPECT_THROW(static_cast<void>(read(unbacked)), std::runtime_error);
  EXPECT_THROW(static_cast<void>(read(typed)), std::runtime_error);
  EXPECT_THROW(static_cast<void>(read(sized)), std::runtime_error);
}

//! A binding edit replaces the record it names, past the vertex lists of the
//! soft bodies before it, and a table type edit retypes its table.
NOLINT_TEST(DescriptorDefectsTest, BindingEditsChangeTheRecordTheyName)
{
  auto main = SceneBuilder { "Main" };
  const auto cloth = main.Node("Cloth");
  const auto flag = main.Node("Flag");
  const auto scene = main.Bytes();
  auto bindings = PhysicsSidecarBuilder {};
  bindings
    .SoftBody(cloth, Key("/Game/Cloth.opres"), {}, { .pinned = { 0U, 1U } })
    .SoftBody(flag, Key("/Game/Flag.opres"), {}, { .kinematic = { 2U } });
  const auto valid
    = bindings.Bytes(Key("/Game/Main.oscene"), scene, "PhysicsScene");
  const auto read
    = [](const std::vector<std::byte>& bytes) -> data::PhysicsSceneAsset {
    return { Key("/Game/Main.opscene"), std::span<const std::byte>(bytes) };
  };

  auto stiffened = valid;
  auto body
    = DescriptorDefects::BindingAt<physics::SoftBodyBindingRecord>(valid, 1U);
  body.solver_iteration_count = 7U;
  DescriptorDefects::SetBinding(stiffened, 1U, body);
  auto retyped = valid;
  DescriptorDefects::SetBindingTableType(
    retyped, physics::PhysicsBindingType::kSoftBody, 0xFFFFU);

  EXPECT_EQ(body.topology_asset_key, Key("/Game/Flag.opres"));
  const auto asset = read(stiffened);
  const auto bodies = asset.GetBindings<physics::SoftBodyBindingRecord>();
  ASSERT_EQ(bodies.size(), 2U);
  EXPECT_EQ(bodies[1].solver_iteration_count, 7U);
  EXPECT_THROW(static_cast<void>(read(retyped)), std::runtime_error);
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
