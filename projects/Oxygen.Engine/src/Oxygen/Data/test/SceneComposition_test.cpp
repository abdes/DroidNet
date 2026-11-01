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
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneComposition.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace data = oxygen::data;
namespace world = data::pak::world;
using data::AssetKey;
using data::CompositionIssueCode;
using data::NodeId;
using data::schema::OverrideId;
using oxygen::cooker::testing::SceneBuilder;
using oxygen::cooker::testing::SceneNode;
using oxygen::cooker::testing::SceneSlot;
using oxygen::cooker::testing::ScriptParam;

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

auto FloatValue(const float value) -> world::OverrideValue
{
  auto result = world::OverrideValue {};
  result.as_float = value;
  return result;
}

//! A change of `override_id` to `value` below the instance root `root`.
auto Change(const world::SceneNodeIndexT root, const uint32_t path,
  const OverrideId override_id, const float value) -> world::OverrideRecord
{
  return {
    .target = { .node_index = root, .node_path = path },
    .override_id = override_id,
    .value = FloatValue(value),
  };
}

//! `Body` with a child `Wheel` at x = 1; the wheel has a script slot.
struct Model final {
  SceneBuilder scene { "Model" };
  SceneNode body;
  SceneNode wheel;
  SceneSlot spin;

  Model()
    : body(scene.Node("Body"))
    , wheel(scene.Node("Wheel", body))
    , spin(scene.ScriptSlot(
        wheel, AssetKey::FromVirtualPath("/test/Spin.oscript")))
  {
    scene.Place(wheel, { .translation = { kModelX, 0.0F, 0.0F } });
  }
};

//! A scene `scene` whose root `root` has a child `holder` that instances
//! `source`.
struct InstancingSpec {
  std::string_view scene;
  std::string_view root;
  std::string_view holder;
  AssetKey source;
};

struct Instancing final {
  SceneBuilder scene;
  SceneNode root;
  SceneNode holder;

  explicit Instancing(const InstancingSpec& spec)
    : scene(std::string(spec.scene))
    , root(scene.Node(spec.root))
    , holder(scene.Node(spec.holder, root))
  {
    scene.Instance(holder, spec.source);
  }
};

//! Serves the scenes a test registers.
class Scenes {
public:
  auto Add(const AssetKey& key, const SceneBuilder& builder)
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
    auto model = Model {};
    wheel_ = model.scene.IdOf(model.wheel);
    wheel_index_ = model.wheel.Index();
    slot_ = model.spin.Id();
    scenes_.Add(ModelKey(), model.scene);

    auto car = Instancing({
      .scene = "Car",
      .root = "CarRoot",
      .holder = "ModelHolder",
      .source = ModelKey(),
    });
    holder_ = car.scene.IdOf(car.holder);
    holder_index_ = car.holder.Index();
    car_root_ = car.scene.IdOf(car.root);
    car.scene.Change(car.holder, { wheel_ }, OverrideId::kTransformPositionX,
      FloatValue(kCarX));
    car_ = scenes_.Add(CarKey(), car.scene);
  }

  [[nodiscard]] auto Scenes() -> class Scenes& { return scenes_; }
  [[nodiscard]] auto Wheel() const -> NodeId { return wheel_; }
  [[nodiscard]] auto WheelIndex() const -> world::SceneNodeIndexT
  {
    return wheel_index_;
  }
  [[nodiscard]] auto Holder() const -> NodeId { return holder_; }
  //! The index of the instance root in the car; scenes built as `Instancing`
  //! builds them place theirs there too.
  [[nodiscard]] auto HolderIndex() const -> world::SceneNodeIndexT
  {
    return holder_index_;
  }
  [[nodiscard]] auto CarRoot() const -> NodeId { return car_root_; }
  [[nodiscard]] auto Slot() const -> const data::ScriptSlotId& { return slot_; }
  [[nodiscard]] auto Car() const -> std::shared_ptr<const data::SceneAsset>
  {
    return car_;
  }

private:
  class Scenes scenes_;
  NodeId wheel_;
  world::SceneNodeIndexT wheel_index_ = 0;
  NodeId holder_;
  world::SceneNodeIndexT holder_index_ = 0;
  NodeId car_root_;
  data::ScriptSlotId slot_;
  std::shared_ptr<const data::SceneAsset> car_;
};

} // namespace

//! The outermost scene's change wins two nesting levels down: the street's
//! change of the wheel replaces the car's, which replaces the model's value
//! (SI-10).
NOLINT_TEST_F(SceneCompositionTest, OuterChangeWinsTwoLevelsDown)
{
  auto street = Instancing({
    .scene = "Street",
    .root = "Street",
    .holder = "Car",
    .source = CarKey(),
  });
  street.scene.Change(street.holder, { Holder(), Wheel() },
    OverrideId::kTransformPositionX, FloatValue(kStreetX));
  const auto scene = Scenes().Add(StreetKey(), street.scene);

  const auto composed = data::ComposeScene(scene, Scenes().Resolver());

  ASSERT_EQ(composed.occurrences.size(), 3U);
  EXPECT_TRUE(composed.unresolved.empty());
  EXPECT_TRUE(composed.issues.empty());
  const auto& model = composed.occurrences.at(2U);
  EXPECT_EQ(model.parent, std::optional<std::size_t> { 1U });
  EXPECT_EQ(model.outer_root, street.holder.Index());
  const auto& wheel = model.nodes.at(WheelIndex());
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
  const auto& wheel = composed.occurrences.at(1U).nodes.at(WheelIndex());
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
  auto street = Instancing({
    .scene = "Street",
    .root = "Street",
    .holder = "Car",
    .source = CarKey(),
  });
  street.scene.Change(street.holder, { stranger },
    OverrideId::kTransformPositionX, FloatValue(kStreetX));
  street.scene.Change(street.holder, { CarRoot(), Wheel() },
    OverrideId::kTransformPositionX, FloatValue(kStreetX));
  street.scene.Change(street.holder, { Holder(), Wheel() },
    OverrideId::kPerspectiveCameraNearPlane, FloatValue(kStreetX));
  const auto scene = Scenes().Add(StreetKey(), street.scene);

  const auto composed = data::ComposeScene(scene, Scenes().Resolver());

  ASSERT_EQ(composed.occurrences.size(), 3U);
  ASSERT_EQ(composed.unresolved.size(), 3U);
  for (const auto& unresolved : composed.unresolved) {
    EXPECT_EQ(unresolved.supplier, 0U);
    EXPECT_EQ(unresolved.instance_root, street.holder.Index());
  }
  EXPECT_EQ(
    composed.unresolved.front().change.path, (std::vector { stranger }));
  // The car's own change still applies.
  EXPECT_FLOAT_EQ(
    composed.occurrences.at(2U).nodes.at(WheelIndex()).node.translation[0],
    kCarX);
}

//! A missing instanced scene is an issue on its instance root; the scene
//! itself composes.
NOLINT_TEST_F(SceneCompositionTest, MissingInstancedSceneIsAnIssue)
{
  const auto absent = AssetKey::FromVirtualPath("/test/Absent.oscene");
  const auto street = Instancing({
    .scene = "Street",
    .root = "Street",
    .holder = "Car",
    .source = absent,
  });
  const auto scene = Scenes().Add(StreetKey(), street.scene);

  const auto composed = data::ComposeScene(scene, Scenes().Resolver());

  ASSERT_EQ(composed.occurrences.size(), 1U);
  const auto* issue = FindIssue(composed, CompositionIssueCode::kSceneMissing);
  ASSERT_NE(issue, nullptr);
  EXPECT_EQ(issue->occurrence, 0U);
  EXPECT_EQ(issue->node_index, std::optional { street.holder.Index() });
  EXPECT_EQ(issue->scene, absent);
}

//! A scene reached again on its own path is a cycle; its branch ends.
NOLINT_TEST_F(SceneCompositionTest, CycleEndsItsBranch)
{
  const auto loop_key = AssetKey::FromVirtualPath("/test/Loop.oscene");
  const auto loop = Instancing({
    .scene = "Loop",
    .root = "Loop",
    .holder = "Back",
    .source = StreetKey(),
  });
  Scenes().Add(loop_key, loop.scene);
  const auto street = Instancing({
    .scene = "Street",
    .root = "Street",
    .holder = "Ahead",
    .source = loop_key,
  });
  const auto scene = Scenes().Add(StreetKey(), street.scene);

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
    .scene = "Street",
    .root = "Street",
    .holder = "Car",
    .source = CarKey(),
  });
  street.scene.Change(street.holder, { Holder(), Wheel() },
    OverrideId::kTransformScaleY, FloatValue(kStreetX));
  const auto scene = Scenes().Add(StreetKey(), street.scene);
  const auto composed = data::ComposeScene(scene, Scenes().Resolver());
  ASSERT_EQ(composed.occurrences.size(), 3U);

  const auto node = data::ComposeNode(composed, 2U, WheelIndex());

  const auto& expected = composed.occurrences.at(2U).nodes.at(WheelIndex());
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
    .scene = "Street",
    .root = "Street",
    .holder = "Car",
    .source = CarKey(),
  });
  street.scene.ScriptChange(street.holder, { Holder(), Wheel() }, Slot(),
    ScriptParam { .key = "speed", .value = kSpeed });
  street.scene.ScriptChange(street.holder, { Holder(), Wheel() },
    data::ScriptSlotId::FromStableIdentity("/test/slot/other"),
    ScriptParam { .key = "speed", .value = kSpeed });
  const auto scene = Scenes().Add(StreetKey(), street.scene);

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
  const auto street = Instancing({
    .scene = "Street",
    .root = "Street",
    .holder = "Car",
    .source = CarKey(),
  });
  const auto scene = Scenes().Add(StreetKey(), street.scene);
  const auto composed = data::ComposeScene(scene, Scenes().Resolver());
  ASSERT_EQ(composed.occurrences.size(), 3U);
  const auto& wheel_scene = composed.occurrences.at(2U).scene;
  const auto change = [&street](const OverrideId id) -> data::SceneChange {
    return { .change = Change(street.holder.Index(), 0U, id, kStreetX) };
  };

  const auto moved = data::CheckChange(
    wheel_scene, WheelIndex(), change(OverrideId::kTransformScaleY));
  const auto camera = data::CheckChange(
    wheel_scene, WheelIndex(), change(OverrideId::kPerspectiveCameraNearPlane));
  const auto slot = data::CheckChange(wheel_scene, WheelIndex(),
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
    .change = Change(HolderIndex(), 0U, OverrideId::kTransformScaleY, kStreetX),
  };
  const auto parameter = data::SceneChange {
    .path = path,
    .change = data::ParameterChange {
      .slot_id = Slot(),
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
      .target = data::ParameterAddress { .slot_id = Slot(), .key = "speed" },
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
      .instance_root = HolderIndex(),
      .changes = changes,
      .ancestors = ancestors,
    },
    Scenes().Resolver());
  const auto cycle = data::ComposeInstance(
    {
      .source = StreetKey(),
      .instance_root = HolderIndex(),
      .changes = {},
      .ancestors = ancestors,
    },
    Scenes().Resolver());

  ASSERT_EQ(composed.occurrences.size(), 2U);
  EXPECT_EQ(composed.occurrences.front().nodes.front().path,
    (std::vector { CarRoot() }));
  const auto& wheel = composed.occurrences.at(1U).nodes.at(WheelIndex());
  EXPECT_FLOAT_EQ(wheel.node.translation[0], kStreetX);
  ASSERT_EQ(wheel.changes.size(), 1U);
  EXPECT_EQ(wheel.changes.front().supplier, data::kRequester);
  EXPECT_TRUE(cycle.occurrences.empty());
  EXPECT_NE(FindIssue(cycle, CompositionIssueCode::kCycle), nullptr);
}
