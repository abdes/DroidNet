//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: ComposeScene, ComposeInstance, ComposeNode, ComposeSlots,
// CheckChange and AddressOf (scene instances 7.6, 7.8): which change wins,
// unresolved changes, missing scenes and cycles.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneComposition.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/test/Fixtures/SceneAssetBuilder.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace data = oxygen::data;
namespace world = data::pak::world;
namespace script = data::pak::scripting;
using data::AssetKey;
using data::ComponentType;
using data::CompositionIssueCode;
using data::NodeId;
using data::schema::OverrideId;
using data::testing::SceneAssetBuilder;

constexpr world::SceneNodeIndexT kHolder = 1U;
constexpr world::SceneNodeIndexT kWheel = 1U;
constexpr float kModelX = 1.0F;
constexpr float kCarX = 2.0F;
constexpr float kStreetX = 3.0F;
constexpr float kSpeed = 5.0F;

auto ModelKey() -> AssetKey
{
  return AssetKey::FromVirtualPath("/test/Model.oscene");
}
auto CarKey() -> AssetKey
{
  return AssetKey::FromVirtualPath("/test/Car.oscene");
}
auto StreetKey() -> AssetKey
{
  return AssetKey::FromVirtualPath("/test/Street.oscene");
}

auto SlotId() -> data::ScriptSlotId
{
  return data::ScriptSlotId::FromStableIdentity("/test/slot/wheel");
}

//! A change of `override_id` to `value` below the instance root `root`.
auto Change(const world::SceneNodeIndexT root, const uint32_t path,
  const OverrideId override_id, const float value) -> world::OverrideRecord
{
  auto record = world::OverrideRecord {
    .target = { .node_index = root, .node_path = path },
    .override_id = override_id,
  };
  record.value.as_float = value;
  return record;
}

//! `Body` with a child `Wheel` at x = 1; the wheel has a script slot.
auto Model() -> SceneAssetBuilder
{
  auto model = SceneAssetBuilder {};
  model.AddNode("Body");
  model.AddNode("Wheel", 0U);
  model.Node(kWheel).translation[0] = kModelX;
  model.AddScripting(kWheel,
    std::vector {
      script::ScriptSlotRecord {
        .slot_id = SlotId(),
        .script_asset_key = AssetKey::FromVirtualPath("/test/Spin.oscript"),
      },
    });
  return model;
}

//! A scene whose root `root` has a child `holder` (node 1) that instances
//! `source`, with the node path table `paths`.
struct InstancingSpec {
  std::string_view root;
  std::string_view holder;
  AssetKey source;
  std::vector<std::vector<NodeId>> paths;
};

auto Instancing(const InstancingSpec& spec) -> SceneAssetBuilder
{
  auto builder = SceneAssetBuilder {};
  builder.AddNode(spec.root);
  builder.AddNode(spec.holder, 0U);
  builder.AddComponents(ComponentType::kSceneInstance,
    std::vector {
      world::SceneInstanceRecord {
        .node_index = kHolder,
        .scene_key = spec.source,
      },
    });
  auto records = std::vector<world::NodePathRecord> {};
  auto elements = std::vector<world::NodePathElement> {};
  for (const auto& path : spec.paths) {
    records.push_back({
      .first = static_cast<uint32_t>(elements.size()),
      .count = static_cast<uint32_t>(path.size()),
    });
    for (const auto& id : path) {
      elements.push_back({ .node_id = id });
    }
  }
  if (!records.empty()) {
    builder.AddComponents(ComponentType::kNodePath, records);
    builder.AddComponents(ComponentType::kNodePathElement, elements);
  }
  return builder;
}

//! Serves the scenes a test registers.
class Scenes {
public:
  auto Add(const AssetKey& key, const SceneAssetBuilder& builder)
    -> std::shared_ptr<const data::SceneAsset>
  {
    auto scene = std::shared_ptr<const data::SceneAsset>(builder.Build(key));
    scenes_.insert_or_assign(key, scene);
    return scene;
  }

  [[nodiscard]] auto Resolver() const -> data::SceneResolver
  {
    return
      [this](const AssetKey& key) -> std::shared_ptr<const data::SceneAsset> {
        const auto found = scenes_.find(key);
        return found != scenes_.end() ? found->second : nullptr;
      };
  }

private:
  std::unordered_map<AssetKey, std::shared_ptr<const data::SceneAsset>> scenes_;
};

auto FindIssue(const data::ComposedScene& composed,
  const CompositionIssueCode code) -> const data::CompositionIssue*
{
  const auto found
    = std::ranges::find(composed.issues, code, &data::CompositionIssue::code);
  return found != composed.issues.end() ? &*found : nullptr;
}

//! A street instances a car that instances the model; both change the
//! wheel's position.
class SceneCompositionTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    auto model = Model();
    wheel_ = model.Node(kWheel).node_id;
    scenes_.Add(ModelKey(), model);

    auto car = Instancing({
      .root = "CarRoot",
      .holder = "ModelHolder",
      .source = ModelKey(),
      .paths = { { wheel_ } },
    });
    holder_ = car.Node(kHolder).node_id;
    car_root_ = car.Node(0U).node_id;
    car.AddComponents(ComponentType::kOverride,
      std::vector {
        Change(kHolder, 0U, OverrideId::kTransformPositionX, kCarX),
      });
    car_ = scenes_.Add(CarKey(), car);
  }

  [[nodiscard]] auto Scenes() -> class Scenes& { return scenes_; }
  [[nodiscard]] auto Wheel() const -> NodeId { return wheel_; }
  [[nodiscard]] auto Holder() const -> NodeId { return holder_; }
  [[nodiscard]] auto CarRoot() const -> NodeId { return car_root_; }
  [[nodiscard]] auto Car() const -> std::shared_ptr<const data::SceneAsset>
  {
    return car_;
  }

private:
  class Scenes scenes_;
  NodeId wheel_;
  NodeId holder_;
  NodeId car_root_;
  std::shared_ptr<const data::SceneAsset> car_;
};

} // namespace

//! The outermost scene's change wins two nesting levels down: the street's
//! change of the wheel replaces the car's, which replaces the model's value
//! (SI-10).
NOLINT_TEST_F(SceneCompositionTest, OuterChangeWinsTwoLevelsDown)
{
  auto street = Instancing({
    .root = "Street",
    .holder = "Car",
    .source = CarKey(),
    .paths = { { Holder(), Wheel() } },
  });
  street.AddComponents(ComponentType::kOverride,
    std::vector {
      Change(kHolder, 0U, OverrideId::kTransformPositionX, kStreetX),
    });
  const auto scene = Scenes().Add(StreetKey(), street);

  const auto composed = data::ComposeScene(scene, Scenes().Resolver());

  ASSERT_EQ(composed.occurrences.size(), 3U);
  EXPECT_TRUE(composed.unresolved.empty());
  EXPECT_TRUE(composed.issues.empty());
  const auto& model = composed.occurrences.at(2U);
  EXPECT_EQ(model.parent, std::optional<std::size_t> { 1U });
  EXPECT_EQ(model.outer_root, kHolder);
  const auto& wheel = model.nodes.at(kWheel);
  EXPECT_EQ(wheel.path, (std::vector { Holder(), Wheel() }));
  EXPECT_FLOAT_EQ(wheel.node.translation[0], kStreetX);
  ASSERT_EQ(wheel.changes.size(), 1U);
  EXPECT_EQ(wheel.changes.front().supplier, 0U);
}

//! Without an outer change, the nearer level's change applies.
NOLINT_TEST_F(SceneCompositionTest, NestedChangeAppliesWithoutAnOuterOne)
{
  const auto composed = data::ComposeScene(Car(), Scenes().Resolver());

  ASSERT_EQ(composed.occurrences.size(), 2U);
  const auto& wheel = composed.occurrences.at(1U).nodes.at(kWheel);
  EXPECT_EQ(wheel.path, (std::vector { Wheel() }));
  EXPECT_FLOAT_EQ(wheel.node.translation[0], kCarX);
  ASSERT_EQ(wheel.changes.size(), 1U);
  EXPECT_EQ(wheel.changes.front().supplier, 0U);
  EXPECT_TRUE(composed.occurrences.front().nodes.front().path.empty());
}

//! A change whose element names no node, one that passes through a node that
//! is not an instance root, and one of a record the node does not have are
//! unresolved; the rest of the scene composes.
NOLINT_TEST_F(SceneCompositionTest, ChangesThatDoNotResolveAreReported)
{
  const auto stranger = NodeId::FromStableIdentity("/test/Stranger");
  auto street = Instancing({ .root = "Street",
    .holder = "Car",
    .source = CarKey(),
    .paths = {
      { stranger },
      { CarRoot(), Wheel() },
      { Holder(), Wheel() },
    }, });
  street.AddComponents(ComponentType::kOverride,
    std::vector {
      Change(kHolder, 0U, OverrideId::kTransformPositionX, kStreetX),
      Change(kHolder, 1U, OverrideId::kTransformPositionX, kStreetX),
      Change(kHolder, 2U, OverrideId::kPerspectiveCameraNearPlane, kStreetX),
    });
  const auto scene = Scenes().Add(StreetKey(), street);

  const auto composed = data::ComposeScene(scene, Scenes().Resolver());

  ASSERT_EQ(composed.occurrences.size(), 3U);
  ASSERT_EQ(composed.unresolved.size(), 3U);
  for (const auto& unresolved : composed.unresolved) {
    EXPECT_EQ(unresolved.supplier, 0U);
    EXPECT_EQ(unresolved.instance_root, kHolder);
  }
  EXPECT_EQ(
    composed.unresolved.front().change.path, (std::vector { stranger }));
  // The car's own change still applies.
  EXPECT_FLOAT_EQ(
    composed.occurrences.at(2U).nodes.at(kWheel).node.translation[0], kCarX);
}

//! A missing instanced scene is an issue on its instance root; the scene
//! itself composes.
NOLINT_TEST_F(SceneCompositionTest, MissingInstancedSceneIsAnIssue)
{
  const auto absent = AssetKey::FromVirtualPath("/test/Absent.oscene");
  const auto scene = Scenes().Add(StreetKey(),
    Instancing(
      { .root = "Street", .holder = "Car", .source = absent, .paths = {} }));

  const auto composed = data::ComposeScene(scene, Scenes().Resolver());

  ASSERT_EQ(composed.occurrences.size(), 1U);
  const auto* issue = FindIssue(composed, CompositionIssueCode::kSceneMissing);
  ASSERT_NE(issue, nullptr);
  EXPECT_EQ(issue->occurrence, 0U);
  EXPECT_EQ(issue->node_index, std::optional { kHolder });
  EXPECT_EQ(issue->scene, absent);
}

//! A scene reached again on its own path is a cycle; its branch ends.
NOLINT_TEST_F(SceneCompositionTest, CycleEndsItsBranch)
{
  const auto loop_key = AssetKey::FromVirtualPath("/test/Loop.oscene");
  Scenes().Add(loop_key,
    Instancing({
      .root = "Loop",
      .holder = "Back",
      .source = StreetKey(),
      .paths = {},
    }));
  const auto scene = Scenes().Add(StreetKey(),
    Instancing({
      .root = "Street",
      .holder = "Ahead",
      .source = loop_key,
      .paths = {},
    }));

  const auto composed = data::ComposeScene(scene, Scenes().Resolver());

  ASSERT_EQ(composed.occurrences.size(), 2U);
  const auto* issue = FindIssue(composed, CompositionIssueCode::kCycle);
  ASSERT_NE(issue, nullptr);
  EXPECT_EQ(issue->occurrence, 1U);
  EXPECT_EQ(issue->scene, StreetKey());
}

//! ComposeNode recomputes a node exactly as ComposeScene composed it.
NOLINT_TEST_F(SceneCompositionTest, ComposeNodeEqualsComposeScene)
{
  auto street = Instancing({
    .root = "Street",
    .holder = "Car",
    .source = CarKey(),
    .paths = { { Holder(), Wheel() } },
  });
  street.AddComponents(ComponentType::kOverride,
    std::vector {
      Change(kHolder, 0U, OverrideId::kTransformScaleY, kStreetX),
    });
  const auto scene = Scenes().Add(StreetKey(), street);
  const auto composed = data::ComposeScene(scene, Scenes().Resolver());
  ASSERT_EQ(composed.occurrences.size(), 3U);

  const auto node = data::ComposeNode(composed, 2U, kWheel);

  const auto& expected = composed.occurrences.at(2U).nodes.at(kWheel);
  EXPECT_EQ(node.path, expected.path);
  EXPECT_FLOAT_EQ(node.node.translation[0], expected.node.translation[0]);
  EXPECT_FLOAT_EQ(node.node.scale[1], expected.node.scale[1]);
  ASSERT_EQ(node.changes.size(), expected.changes.size());
  for (std::size_t i = 0; i < node.changes.size(); ++i) {
    EXPECT_EQ(
      node.changes.at(i).override_id, expected.changes.at(i).override_id);
    EXPECT_EQ(node.changes.at(i).supplier, expected.changes.at(i).supplier);
  }
}

//! A script parameter change reaches the slot it names; one naming a slot
//! the node does not have is unresolved.
NOLINT_TEST_F(SceneCompositionTest, ParameterChangesReachTheirSlot)
{
  auto street = Instancing({
    .root = "Street",
    .holder = "Car",
    .source = CarKey(),
    .paths = { { Holder(), Wheel() } },
  });
  const auto key = street.AddString("speed");
  const auto change
    = [&](const data::ScriptSlotId& slot) -> script::ScriptParamOverrideRecord {
    auto record = script::ScriptParamOverrideRecord {
      .target = { .node_index = kHolder, .node_path = 0U },
      .slot_id = slot,
    };
    record.param.key_offset = key;
    record.param.type = script::ScriptParamType::kFloat;
    record.param.value.as_float = kSpeed;
    return record;
  };
  // The cooked table is sorted by node, slot and key.
  auto changes = std::vector {
    change(SlotId()),
    change(data::ScriptSlotId::FromStableIdentity("/test/slot/other")),
  };
  std::ranges::sort(changes, {}, &script::ScriptParamOverrideRecord::slot_id);
  street.AddComponents(ComponentType::kScriptParamOverride, changes);
  const auto scene = Scenes().Add(StreetKey(), street);

  const auto composed = data::ComposeScene(scene, Scenes().Resolver());

  ASSERT_EQ(composed.occurrences.size(), 3U);
  const auto& slots = composed.occurrences.at(2U).slots;
  ASSERT_EQ(slots.size(), 1U);
  ASSERT_EQ(slots.front().parameters.size(), 1U);
  const auto& parameter = slots.front().parameters.front();
  EXPECT_EQ(parameter.key, "speed");
  EXPECT_EQ(parameter.supplier, 0U);
  ASSERT_TRUE(std::holds_alternative<float>(parameter.value));
  EXPECT_FLOAT_EQ(std::get<float>(parameter.value), kSpeed);
  ASSERT_EQ(composed.unresolved.size(), 1U);
  EXPECT_TRUE(std::holds_alternative<data::ParameterChange>(
    composed.unresolved.front().change.change));

  const auto recomposed = data::ComposeSlots(composed, 2U);

  ASSERT_EQ(recomposed.size(), slots.size());
  ASSERT_EQ(recomposed.front().parameters.size(), 1U);
  EXPECT_EQ(recomposed.front().parameters.front().key, "speed");
  EXPECT_EQ(recomposed.front().parameters.front().supplier, 0U);
  EXPECT_FLOAT_EQ(
    std::get<float>(recomposed.front().parameters.front().value), kSpeed);
}

//! CheckChange applies a change to a node's own records: a property of a
//! record the node has, a property of one it lacks, and a slot it lacks.
NOLINT_TEST_F(SceneCompositionTest, CheckChangeReadsTheNodesRecords)
{
  auto street = Instancing({
    .root = "Street",
    .holder = "Car",
    .source = CarKey(),
    .paths = { { Holder(), Wheel() } },
  });
  const auto scene = Scenes().Add(StreetKey(), street);
  const auto composed = data::ComposeScene(scene, Scenes().Resolver());
  ASSERT_EQ(composed.occurrences.size(), 3U);
  const auto& wheel_scene = composed.occurrences.at(2U).scene;
  const auto change = [](const OverrideId id) -> data::SceneChange {
    return { .change = Change(kHolder, 0U, id, kStreetX) };
  };

  const auto moved = data::CheckChange(
    wheel_scene, kWheel, change(OverrideId::kTransformScaleY));
  const auto camera = data::CheckChange(
    wheel_scene, kWheel, change(OverrideId::kPerspectiveCameraNearPlane));
  const auto slot = data::CheckChange(wheel_scene, kWheel,
    data::SceneChange {
      .change = data::ParameterChange {
        .slot_id = data::ScriptSlotId::FromStableIdentity("/test/slot/other"),
        .key = "speed",
        .value = kSpeed,
      },
    });

  EXPECT_EQ(moved, data::ChangeOutcome::kApplied);
  EXPECT_EQ(camera, data::ChangeOutcome::kUnresolved);
  EXPECT_EQ(slot, data::ChangeOutcome::kUnresolved);
}

//! A change's address is its path and the property and part, or the slot and
//! key, it sets.
NOLINT_TEST_F(SceneCompositionTest, AddressOfNamesWhatAChangeSets)
{
  const auto path = std::vector { Holder(), Wheel() };
  const auto property = data::SceneChange {
    .path = path,
    .change = Change(kHolder, 0U, OverrideId::kTransformScaleY, kStreetX),
  };
  const auto parameter = data::SceneChange {
    .path = path,
    .change = data::ParameterChange {
      .slot_id = SlotId(),
      .key = "speed",
      .value = kSpeed,
    },
  };

  EXPECT_EQ(data::AddressOf(property),
    (data::ChangeAddress {
      .path = path,
      .target = data::PropertyAddress {
        .override_id = OverrideId::kTransformScaleY,
      },
    }));
  EXPECT_EQ(data::AddressOf(parameter),
    (data::ChangeAddress {
      .path = path,
      .target = data::ParameterAddress { .slot_id = SlotId(), .key = "speed" },
    }));
}

//! An instance composition applies its requester's changes, which win, and
//! refuses a source on the requester's own path.
NOLINT_TEST_F(SceneCompositionTest, InstanceCompositionAppliesRequesterChanges)
{
  auto record = Change(0U, 0U, OverrideId::kTransformPositionX, kStreetX);
  const auto changes = std::vector {
    data::SceneChange { .path = { Holder(), Wheel() }, .change = record },
  };
  const auto ancestors = std::vector { StreetKey() };

  const auto composed = data::ComposeInstance(
    {
      .source = CarKey(),
      .instance_root = kHolder,
      .changes = changes,
      .ancestors = ancestors,
    },
    Scenes().Resolver());
  const auto cycle = data::ComposeInstance(
    {
      .source = StreetKey(),
      .instance_root = kHolder,
      .changes = {},
      .ancestors = ancestors,
    },
    Scenes().Resolver());

  ASSERT_EQ(composed.occurrences.size(), 2U);
  EXPECT_EQ(composed.occurrences.front().nodes.front().path,
    (std::vector { CarRoot() }));
  const auto& wheel = composed.occurrences.at(1U).nodes.at(kWheel);
  EXPECT_FLOAT_EQ(wheel.node.translation[0], kStreetX);
  ASSERT_EQ(wheel.changes.size(), 1U);
  EXPECT_EQ(wheel.changes.front().supplier, data::kRequester);
  EXPECT_TRUE(cycle.occurrences.empty());
  EXPECT_NE(FindIssue(cycle, CompositionIssueCode::kCycle), nullptr);
}
