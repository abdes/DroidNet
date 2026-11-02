//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: the structure a scene descriptor must have before the scene reader
// and the scene loader admit it: its hierarchy, its component directory, the
// order of its node tables, its node paths and its environment block.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <Oxygen/Cooker/testing/DescriptorDefects.h>
#include <Oxygen/Cooker/testing/ProductRefusal.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace data = oxygen::data;
namespace world = data::pak::world;
using data::ComponentType;
using data::CookedDefect;
using data::NodeId;
using oxygen::cooker::testing::DescriptorDefects;
using oxygen::cooker::testing::SceneBuilder;
using oxygen::cooker::testing::SceneNode;
using oxygen::cooker::testing::SceneRefusal;

//! A root with two children, each with a local fog volume, and a renderable
//! under the first.
struct TwoChildScene final {
  SceneBuilder scene { "SceneStructureTest" };
  SceneNode root;
  SceneNode first;
  SceneNode second;

  TwoChildScene()
    : root(scene.Node("Root"))
    , first(scene.Node("First", root))
    , second(scene.Node("Second", root))
  {
    scene.LocalFogVolume(first);
    scene.LocalFogVolume(second);
    scene.Renderable(
      first, data::AssetKey::FromVirtualPath("/test/Crate.ogeo"));
  }
};

//! A node of a scene and the parent a defect gives it.
struct ParentEdit final {
  uint32_t node = 0U;
  uint32_t parent = 0U;
};

//! Gives the node of `edit` its parent in `bytes`.
auto Reparent(std::vector<std::byte>& bytes, const ParentEdit edit) -> void
{
  auto node = DescriptorDefects::NodeAt(bytes, edit.node);
  node.parent_index = edit.parent;
  DescriptorDefects::SetNode(bytes, edit.node, node);
}

//! Node 0 is the scene's root, and every parent chain ends at a root.
NOLINT_TEST(SceneStructureTest, RefusesAHierarchyThatIsNotAForest)
{
  const auto fixture = TwoChildScene {};
  const auto valid = fixture.scene.Bytes();
  const auto first = fixture.first.Index();
  const auto second = fixture.second.Index();

  auto cycle = valid;
  Reparent(cycle, { .node = first, .parent = second });
  Reparent(cycle, { .node = second, .parent = first });
  auto root_below = valid;
  Reparent(root_below, { .node = second, .parent = second });
  Reparent(root_below, { .node = fixture.root.Index(), .parent = second });

  EXPECT_EQ(SceneRefusal(valid), std::nullopt);
  EXPECT_EQ(SceneRefusal(cycle), CookedDefect::kInvalidReference);
  EXPECT_EQ(SceneRefusal(root_below), CookedDefect::kInvalidReference);
}

//! Each table of the directory has a component type the format defines, at
//! most once, with the row size of that type.
NOLINT_TEST(SceneStructureTest, RefusesAnInvalidComponentDirectory)
{
  const auto valid = TwoChildScene {}.scene.Bytes();
  constexpr uint32_t kUnknownComponentType = 0x58585858U; // 'XXXX'

  auto unknown = valid;
  DescriptorDefects::SetComponentTableType(
    unknown, ComponentType::kLocalFogVolume, kUnknownComponentType);
  auto repeated = valid;
  DescriptorDefects::SetComponentTableType(repeated,
    ComponentType::kLocalFogVolume,
    static_cast<uint32_t>(ComponentType::kRenderable));
  auto resized = valid;
  DescriptorDefects::SetComponentEntrySize(resized,
    ComponentType::kLocalFogVolume, sizeof(world::LocalFogVolumeRecord) + 1U);

  EXPECT_EQ(SceneRefusal(unknown), CookedDefect::kInvalidValue);
  EXPECT_EQ(SceneRefusal(repeated), CookedDefect::kDuplicate);
  EXPECT_EQ(SceneRefusal(resized), CookedDefect::kWrongRowSize);
}

//! A table of node components holds one record per node, in node order,
//! for nodes of the scene; a lookup by node relies on it.
NOLINT_TEST(SceneStructureTest, RefusesNodeTablesOutOfNodeOrder)
{
  const auto fixture = TwoChildScene {};
  const auto valid = fixture.scene.Bytes();
  const auto volume = [&valid](const uint32_t index) -> auto {
    return DescriptorDefects::ComponentAt<world::LocalFogVolumeRecord>(
      valid, ComponentType::kLocalFogVolume, index);
  };

  auto swapped = valid;
  DescriptorDefects::SetComponent(
    swapped, ComponentType::kLocalFogVolume, 0U, volume(1U));
  DescriptorDefects::SetComponent(
    swapped, ComponentType::kLocalFogVolume, 1U, volume(0U));
  auto repeated = valid;
  DescriptorDefects::SetComponent(
    repeated, ComponentType::kLocalFogVolume, 1U, volume(0U));
  auto outside = valid;
  auto renderable = DescriptorDefects::ComponentAt<world::RenderableRecord>(
    valid, ComponentType::kRenderable, 0U);
  renderable.node_index = fixture.scene.NodeCount();
  DescriptorDefects::SetComponent(
    outside, ComponentType::kRenderable, 0U, renderable);

  const auto scene = fixture.scene.Build({});
  const auto* found
    = scene->FindComponent<world::LocalFogVolumeRecord>(fixture.second.Index());
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->node_index, fixture.second.Index());
  EXPECT_EQ(SceneRefusal(swapped), CookedDefect::kNotCanonical);
  EXPECT_EQ(SceneRefusal(repeated), CookedDefect::kDuplicate);
  EXPECT_EQ(SceneRefusal(outside), CookedDefect::kInvalidReference);
}

//! Two paths of one scene name two nodes: otherwise two changes of one
//! property of one node would both look unique.
NOLINT_TEST(SceneStructureTest, RefusesTwoPathsToOneNode)
{
  auto scene = SceneBuilder { "SceneStructureTest" };
  const auto lamps = scene.Node("Lamps", scene.Node("Root"));
  scene.Instance(lamps, data::AssetKey::FromVirtualPath("/test/Lamp.oscene"));
  const auto lamp = NodeId::FromStableIdentity("/test/Lamp");
  const auto bulb = NodeId::FromStableIdentity("/test/Bulb");
  auto value = world::OverrideValue {};
  value.as_float = 2.0F;
  scene.Change(lamps, { lamp, bulb },
    data::schema::OverrideId::kTransformPositionX, value);
  scene.Change(lamps, { lamp, NodeId::FromStableIdentity("/test/Shade") },
    data::schema::OverrideId::kTransformPositionX, value);
  const auto valid = scene.Bytes();

  auto aliased = valid;
  const auto path = DescriptorDefects::ComponentAt<world::NodePathRecord>(
    valid, ComponentType::kNodePath, 1U);
  auto last = DescriptorDefects::ComponentAt<world::NodePathElement>(
    valid, ComponentType::kNodePathElement, path.first + path.count - 1U);
  last.node_id = bulb;
  DescriptorDefects::SetComponent(aliased, ComponentType::kNodePathElement,
    path.first + path.count - 1U, last);

  EXPECT_EQ(SceneRefusal(valid), std::nullopt);
  EXPECT_EQ(SceneRefusal(aliased), CookedDefect::kDuplicate);
}

//! The environment block ends every scene, its record count is bounded by
//! its bytes before anything is sized by it, and each record has a system
//! type the format defines.
NOLINT_TEST(SceneStructureTest, RefusesAnInvalidEnvironmentBlock)
{
  auto fixture = TwoChildScene {};
  fixture.scene.Environment(world::FogEnvironmentRecord {});
  const auto valid = fixture.scene.Bytes();
  constexpr uint32_t kUnknownEnvironmentType = 0xFFFFU;

  auto missing = valid;
  DescriptorDefects::DropEnvironmentBlock(missing);
  auto overcounted = valid;
  DescriptorDefects::SetEnvironmentRecordCount(
    overcounted, std::numeric_limits<uint32_t>::max());
  auto unknown = valid;
  DescriptorDefects::SetEnvironmentRecordType(
    unknown, world::EnvironmentComponentType::kFog, kUnknownEnvironmentType);

  EXPECT_EQ(SceneRefusal(missing), CookedDefect::kMissingPart);
  EXPECT_EQ(SceneRefusal(overcounted), CookedDefect::kOutOfBounds);
  EXPECT_EQ(SceneRefusal(unknown), CookedDefect::kInvalidValue);
}

} // namespace
