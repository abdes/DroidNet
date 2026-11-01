//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: SceneAsset node identity, node paths and the references that name
// nodes an instance brings in (overrides, script parameter changes, the
// starting camera).

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <vector>

#include <Oxygen/Cooker/testing/DescriptorDefects.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace data = oxygen::data;
namespace world = data::pak::world;
namespace script = data::pak::scripting;
using data::ComponentType;
using data::NodeId;
using data::schema::OverrideId;
using oxygen::cooker::testing::DescriptorDefects;
using oxygen::cooker::testing::SceneBuilder;
using oxygen::cooker::testing::SceneNode;
using oxygen::cooker::testing::ScriptParam;

//! The nodes `Lamp/Bulb` the instanced scene brings in; they belong to that
//! scene, not to the one under test.
auto Lamp() -> NodeId { return NodeId::FromStableIdentity("/test/Lamp"); }
auto Bulb() -> NodeId { return NodeId::FromStableIdentity("/test/Bulb"); }

//! A slot of the instanced scene.
auto LampSlot() -> data::ScriptSlotId
{
  return data::ScriptSlotId::FromStableIdentity("/test/slot");
}

auto SceneKey() -> data::AssetKey
{
  return data::AssetKey::FromVirtualPath("/test/Scene.oscene");
}

//! A root and an instance root whose instance holds `Lamp/Bulb`.
struct InstancingScene final {
  SceneBuilder scene { "SceneNodeReferencesTest" };
  SceneNode root;
  SceneNode lamps;

  InstancingScene()
    : root(scene.Node("Root"))
    , lamps(scene.Node("Lamps", root))
  {
    scene.Instance(lamps, data::AssetKey::FromVirtualPath("/test/Lamp.oscene"));
  }
};

//! A change of the instanced node's position.
auto PositionValue() -> world::OverrideValue
{
  auto value = world::OverrideValue {};
  value.as_float = 2.0F;
  return value;
}

auto Read(const std::vector<std::byte>& descriptor)
  -> std::shared_ptr<data::SceneAsset>
{
  return std::make_shared<data::SceneAsset>(SceneKey(), descriptor);
}

//! Composition finds nodes by ID, so a scene refuses a node without one and
//! two nodes with one ID.
NOLINT_TEST(SceneNodeReferencesTest, RejectsMissingAndRepeatedNodeIds)
{
  auto single = SceneBuilder { "SceneNodeReferencesTest" };
  single.Node("Root");
  auto missing = single.Bytes();
  auto node = DescriptorDefects::NodeAt(missing, 0U);
  node.node_id = {};
  DescriptorDefects::SetNode(missing, 0U, node);

  auto pair = SceneBuilder { "SceneNodeReferencesTest" };
  pair.Node("Child", pair.Node("Root"));
  auto repeated = pair.Bytes();
  node = DescriptorDefects::NodeAt(repeated, 1U);
  node.node_id = DescriptorDefects::NodeAt(repeated, 0U).node_id;
  DescriptorDefects::SetNode(repeated, 1U, node);

  EXPECT_THROW(static_cast<void>(Read(missing)), std::runtime_error);
  EXPECT_THROW(static_cast<void>(Read(repeated)), std::runtime_error);
}

//! A change addresses an instanced node by its instance root and the path of
//! node IDs below it, shallowest first.
NOLINT_TEST(SceneNodeReferencesTest, ReadsTheChangeOfAnInstancedNode)
{
  auto fixture = InstancingScene {};
  fixture.scene.Change(fixture.lamps, { Lamp(), Bulb() },
    OverrideId::kTransformPositionX, PositionValue());

  const auto scene = fixture.scene.Build(SceneKey());

  const auto changes = scene->GetComponents<world::OverrideRecord>();
  ASSERT_EQ(changes.size(), 1U);
  EXPECT_EQ(changes[0].target.node_index, fixture.lamps.Index());
  EXPECT_EQ(
    scene->GetNodePath(changes[0].target), (std::vector { Lamp(), Bulb() }));
  EXPECT_TRUE(
    scene->GetNodePath({ .node_index = fixture.root.Index() }).empty());
}

//! A node of the scene stores its own values in its records: only an
//! asset-sourced property is an override of it.
NOLINT_TEST(SceneNodeReferencesTest, RejectsAnOwnedNodeChangeOfARecordValue)
{
  auto fixture = InstancingScene {};
  fixture.scene.Change(fixture.lamps, { Lamp(), Bulb() },
    OverrideId::kTransformPositionX, PositionValue());
  auto descriptor = fixture.scene.Bytes();
  auto record = DescriptorDefects::ComponentAt<world::OverrideRecord>(
    descriptor, ComponentType::kOverride, 0U);
  record.target.node_path = world::kNoNodePath;
  DescriptorDefects::SetComponent(
    descriptor, ComponentType::kOverride, 0U, record);

  EXPECT_THROW(static_cast<void>(Read(descriptor)), std::runtime_error);
}

//! A path runs below an instance root and names a path the scene holds.
NOLINT_TEST(SceneNodeReferencesTest, RejectsInvalidNodePaths)
{
  auto fixture = InstancingScene {};
  fixture.scene.Change(fixture.lamps, { Lamp(), Bulb() },
    OverrideId::kTransformPositionX, PositionValue());
  const auto valid = fixture.scene.Bytes();
  const auto change = DescriptorDefects::ComponentAt<world::OverrideRecord>(
    valid, ComponentType::kOverride, 0U);

  auto not_a_root = valid;
  auto record = change;
  record.target.node_index = fixture.root.Index();
  DescriptorDefects::SetComponent(
    not_a_root, ComponentType::kOverride, 0U, record);

  auto out_of_range = valid;
  record = change;
  record.target.node_path = change.target.node_path + 1U;
  DescriptorDefects::SetComponent(
    out_of_range, ComponentType::kOverride, 0U, record);

  auto bad_elements = valid;
  auto path = DescriptorDefects::ComponentAt<world::NodePathRecord>(
    valid, ComponentType::kNodePath, 0U);
  path.count = 3U;
  DescriptorDefects::SetComponent(
    bad_elements, ComponentType::kNodePath, 0U, path);

  EXPECT_THROW(static_cast<void>(Read(not_a_root)), std::runtime_error);
  EXPECT_THROW(static_cast<void>(Read(out_of_range)), std::runtime_error);
  EXPECT_THROW(static_cast<void>(Read(bad_elements)), std::runtime_error);
}

//! A script parameter change names an instanced node, its slot and the key.
NOLINT_TEST(SceneNodeReferencesTest, ReadsScriptParameterChanges)
{
  auto fixture = InstancingScene {};
  fixture.scene.ScriptChange(fixture.lamps, { Lamp(), Bulb() }, LampSlot(),
    ScriptParam { .key = "speed", .value = 1.5F });
  auto rejected = fixture.scene.Bytes();
  auto owned
    = DescriptorDefects::ComponentAt<script::ScriptParamOverrideRecord>(
      rejected, ComponentType::kScriptParamOverride, 0U);
  owned.target.node_path = world::kNoNodePath;
  DescriptorDefects::SetComponent(
    rejected, ComponentType::kScriptParamOverride, 0U, owned);

  const auto scene = fixture.scene.Build(SceneKey());

  EXPECT_EQ(
    scene->GetComponents<script::ScriptParamOverrideRecord>().size(), 1U);
  EXPECT_THROW(static_cast<void>(Read(rejected)), std::runtime_error);
}

//! The starting camera may be a node an instance brings in; one of the
//! scene's own nodes must own a camera.
NOLINT_TEST(SceneNodeReferencesTest, AcceptsAnInstancedStartingCamera)
{
  auto fixture = InstancingScene {};
  fixture.scene.StartingCamera(fixture.lamps, { Lamp(), Bulb() });
  auto owned = fixture.scene.Bytes();
  DescriptorDefects::SetStartingCamera(
    owned, world::NodeRef { .node_index = fixture.lamps.Index() });

  const auto scene = fixture.scene.Build(SceneKey());

  const auto camera = scene->GetStartingCamera();
  if (!camera.has_value()) {
    FAIL() << "the scene has no starting camera";
  }
  EXPECT_EQ(camera->node_path, 0U);
  EXPECT_THROW(static_cast<void>(Read(owned)), std::runtime_error);
}

} // namespace
