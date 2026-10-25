//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: SceneAsset node identity, node paths and the references that name
// nodes an instance brings in (overrides, script parameter changes, the
// starting camera).

#include <memory>
#include <stdexcept>
#include <vector>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/test/Fixtures/SceneAssetBuilder.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace data = oxygen::data;
namespace world = data::pak::world;
namespace script = data::pak::scripting;
using data::ComponentType;
using data::NodeId;
using data::schema::OverrideId;
using data::testing::SceneAssetBuilder;

constexpr world::SceneNodeIndexT kInstanceRoot = 1U;

auto Lamp() -> NodeId { return NodeId::FromStableIdentity("/test/Lamp"); }
auto Bulb() -> NodeId { return NodeId::FromStableIdentity("/test/Bulb"); }

//! A root and an instance root (node 1) whose instance holds `Lamp/Bulb`.
auto InstancingScene() -> SceneAssetBuilder
{
  auto builder = SceneAssetBuilder {};
  builder.AddNode("Root");
  builder.AddNode("Lamps", 0U);
  builder.AddComponents(ComponentType::kSceneInstance,
    std::vector {
      world::SceneInstanceRecord {
        .node_index = kInstanceRoot,
        .scene_key = data::AssetKey::FromVirtualPath("/test/Lamp.oscene"),
      },
    });
  builder.AddComponents(ComponentType::kNodePath,
    std::vector { world::NodePathRecord { .first = 0U, .count = 2U } });
  builder.AddComponents(ComponentType::kNodePathElement,
    std::vector {
      world::NodePathElement { .node_id = Lamp() },
      world::NodePathElement { .node_id = Bulb() },
    });
  return builder;
}

//! A change of the instanced node's position.
auto PositionChange(const world::NodeRef& target) -> world::OverrideRecord
{
  auto record = world::OverrideRecord {
    .target = target,
    .override_id = OverrideId::kTransformPositionX,
  };
  record.value.as_float = 2.0F;
  return record;
}

//! Composition finds nodes by ID, so a scene refuses a node without one and
//! two nodes with one ID.
NOLINT_TEST(SceneNodeReferencesTest, RejectsMissingAndRepeatedNodeIds)
{
  auto missing = SceneAssetBuilder {};
  missing.AddNode("Root");
  missing.Node(0U).node_id = {};
  auto repeated = SceneAssetBuilder {};
  repeated.AddNode("Root");
  repeated.AddNode("Child", 0U);
  repeated.Node(1U).node_id = repeated.Node(0U).node_id;

  EXPECT_THROW(static_cast<void>(missing.Build()), std::runtime_error);
  EXPECT_THROW(static_cast<void>(repeated.Build()), std::runtime_error);
}

//! A change addresses an instanced node by its instance root and the path of
//! node IDs below it, shallowest first.
NOLINT_TEST(SceneNodeReferencesTest, ReadsTheChangeOfAnInstancedNode)
{
  auto builder = InstancingScene();
  builder.AddComponents(ComponentType::kOverride,
    std::vector {
      PositionChange({ .node_index = kInstanceRoot, .node_path = 0U }),
    });

  const auto scene = builder.Build();

  const auto changes = scene->GetComponents<world::OverrideRecord>();
  ASSERT_EQ(changes.size(), 1U);
  EXPECT_EQ(changes[0].target.node_index, kInstanceRoot);
  EXPECT_EQ(
    scene->GetNodePath(changes[0].target), (std::vector { Lamp(), Bulb() }));
  EXPECT_TRUE(scene->GetNodePath({ .node_index = 0U }).empty());
}

//! A node of the scene stores its own values in its records: only an
//! asset-sourced property is an override of it.
NOLINT_TEST(SceneNodeReferencesTest, RejectsAnOwnedNodeChangeOfARecordValue)
{
  auto builder = InstancingScene();
  builder.AddComponents(ComponentType::kOverride,
    std::vector { PositionChange({ .node_index = kInstanceRoot }) });

  EXPECT_THROW(static_cast<void>(builder.Build()), std::runtime_error);
}

//! A path runs below an instance root and names a path the scene holds.
NOLINT_TEST(SceneNodeReferencesTest, RejectsInvalidNodePaths)
{
  auto not_a_root = InstancingScene();
  not_a_root.AddComponents(ComponentType::kOverride,
    std::vector { PositionChange({ .node_index = 0U, .node_path = 0U }) });
  auto out_of_range = InstancingScene();
  out_of_range.AddComponents(ComponentType::kOverride,
    std::vector {
      PositionChange({ .node_index = kInstanceRoot, .node_path = 1U }),
    });
  auto bad_elements = SceneAssetBuilder {};
  bad_elements.AddNode("Root");
  bad_elements.AddComponents(ComponentType::kNodePath,
    std::vector { world::NodePathRecord { .first = 0U, .count = 3U } });

  EXPECT_THROW(static_cast<void>(not_a_root.Build()), std::runtime_error);
  EXPECT_THROW(static_cast<void>(out_of_range.Build()), std::runtime_error);
  EXPECT_THROW(static_cast<void>(bad_elements.Build()), std::runtime_error);
}

//! A script parameter change names an instanced node, its slot and the key.
NOLINT_TEST(SceneNodeReferencesTest, ReadsScriptParameterChanges)
{
  auto builder = InstancingScene();
  auto change = script::ScriptParamOverrideRecord {
    .target = { .node_index = kInstanceRoot, .node_path = 0U },
    .slot_id = data::ScriptSlotId::FromStableIdentity("/test/slot"),
  };
  change.param.key_offset = builder.AddString("speed");
  change.param.type = script::ScriptParamType::kFloat;
  change.param.value.as_float = 1.5F;
  auto owned = change;
  owned.target = { .node_index = kInstanceRoot };
  auto rejected = InstancingScene();
  rejected.AddString("speed");
  builder.AddComponents(
    ComponentType::kScriptParamOverride, std::vector { change });
  rejected.AddComponents(
    ComponentType::kScriptParamOverride, std::vector { owned });

  const auto scene = builder.Build();

  EXPECT_EQ(
    scene->GetComponents<script::ScriptParamOverrideRecord>().size(), 1U);
  EXPECT_THROW(static_cast<void>(rejected.Build()), std::runtime_error);
}

//! The starting camera may be a node an instance brings in; one of the
//! scene's own nodes must own a camera.
NOLINT_TEST(SceneNodeReferencesTest, AcceptsAnInstancedStartingCamera)
{
  auto instanced = InstancingScene();
  instanced.SetStartingCamera(
    world::NodeRef { .node_index = kInstanceRoot, .node_path = 0U });
  auto owned = InstancingScene();
  owned.SetStartingCamera(kInstanceRoot);

  const auto scene = instanced.Build();

  const auto camera = scene->GetStartingCamera();
  if (!camera.has_value()) {
    FAIL() << "the scene has no starting camera";
  }
  EXPECT_EQ(camera->node_path, 0U);
  EXPECT_THROW(static_cast<void>(owned.Build()), std::runtime_error);
}

} // namespace
