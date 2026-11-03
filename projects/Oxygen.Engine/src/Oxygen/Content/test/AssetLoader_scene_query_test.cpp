//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "AssetLoader_test.h"

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace data = oxygen::data;
namespace world = oxygen::data::pak::world;
using data::AssetKey;
using data::NodeId;
using data::schema::OverrideId;
using oxygen::content::AssetLoader;
using oxygen::content::IAssetLoader;
using oxygen::content::testing::AssetLoaderPoolTest;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::NodeTransform;
using oxygen::cooker::testing::SceneBuilder;
using oxygen::cooker::testing::SceneNode;
using SceneIssueCode = IAssetLoader::SceneHierarchyIssueCode;
using NodePath = std::vector<NodeId>;

constexpr float kModelX = 1.0F;
constexpr float kCarX = 2.0F;
constexpr float kStreetX = 3.0F;

// The leaves of the mounted root; every key and virtual path derives from one.
constexpr std::string_view kModel = "query/Model.oscene";
constexpr std::string_view kCar = "query/Car.oscene";
constexpr std::string_view kStreet = "query/Street.oscene";
constexpr std::string_view kGarage = "query/Garage.oscene";
constexpr std::string_view kMissing = "query/Missing.oscene";
constexpr std::string_view kLoopA = "query/LoopA.oscene";
constexpr std::string_view kLoopB = "query/LoopB.oscene";
constexpr std::string_view kWheelGeometry = "query/Wheel.ogeo";
constexpr std::string_view kPaint = "query/Paint.omat";

auto RimSurface() -> data::SurfaceId
{
  return data::SurfaceId::FromStableIdentity("/test/query/rim");
}

//! `Body` with a perspective camera and a child `Wheel` at x = 1, which
//! renders the geometry with its rim painted, and holds a point light.
struct ModelScene final {
  SceneBuilder scene;
  SceneNode body;
  SceneNode wheel;
};

//! The assets the model's wheel renders with.
struct ModelAssets final {
  AssetKey geometry;
  AssetKey paint;
};

auto Model(const ModelAssets& assets) -> ModelScene
{
  const auto& [geometry, paint] = assets;
  auto scene = SceneBuilder { "Model" };
  const auto body = scene.Node("Body");
  const auto wheel = scene.Node("Wheel", body);
  scene.Place(wheel, NodeTransform { .translation = { kModelX, 0.0F, 0.0F } });
  scene.PerspectiveCamera(body);
  scene.Renderable(wheel, geometry);
  scene.PointLight(wheel);
  auto revision = oxygen::base::Sha256Digest {};
  revision.fill(1U);
  auto value = world::OverrideValue {};
  value.as_asset = paint;
  scene.Override(
    wheel, OverrideId::kGeometrySurfaceMaterial, value, RimSurface(), revision);
  return { .scene = std::move(scene), .body = body, .wheel = wheel };
}

//! A scene with a root `<name>Root` and its child `<name>Holder`, which
//! instances `source`; each change sets a position X below the holder.
struct InstancingSpec final {
  std::string_view name;
  AssetKey source;
  std::vector<std::pair<NodePath, float>> changes;
};

struct InstancingScene final {
  SceneBuilder scene;
  SceneNode root;
  SceneNode holder;
};

auto Instancing(const InstancingSpec& spec) -> InstancingScene
{
  auto scene = SceneBuilder { spec.name };
  const auto root = scene.Node(std::string(spec.name) + "Root");
  const auto holder = scene.Node(std::string(spec.name) + "Holder", root);
  scene.Instance(holder, spec.source);
  for (const auto& [path, x] : spec.changes) {
    auto value = world::OverrideValue {};
    value.as_float = x;
    scene.Change(holder, path, OverrideId::kTransformPositionX, value);
  }
  return { .scene = std::move(scene), .root = root, .holder = holder };
}

auto ValueOf(const IAssetLoader::SceneNodeValues& values, const OverrideId id,
  const data::SurfaceId part = {})
  -> std::optional<IAssetLoader::SceneNodeValue>
{
  const auto found = std::ranges::find_if(values.properties,
    [id, part](const IAssetLoader::SceneNodeProperty& property) -> bool {
      return property.override_id == id && property.part_id == part;
    });
  return found != values.properties.end() ? std::optional(found->value)
                                          : std::nullopt;
}

auto PositionXOf(const IAssetLoader::SceneNodeValues& values)
  -> std::optional<float>
{
  const auto value = ValueOf(values, OverrideId::kTransformPositionX);
  const auto* x = value.has_value() ? std::get_if<float>(&*value) : nullptr;
  return x != nullptr ? std::optional(*x) : std::nullopt;
}

//! The index of the hierarchy node at `path`; none when no node is there.
auto IndexOf(const IAssetLoader::SceneHierarchy& hierarchy,
  const std::span<const NodeId> path) -> std::optional<uint32_t>
{
  const auto found = std::ranges::find_if(hierarchy.nodes,
    [path](const IAssetLoader::SceneHierarchyNode& node) -> bool {
      return std::ranges::equal(node.path, path);
    });
  return found != hierarchy.nodes.end()
    ? std::optional(
        static_cast<uint32_t>(std::distance(hierarchy.nodes.begin(), found)))
    : std::nullopt;
}

//! What the callback bridges delivered, and on which thread.
struct BridgedReads final {
  std::optional<IAssetLoader::SceneHierarchy> hierarchy;
  std::optional<IAssetLoader::SceneNodeValues> node;
  std::thread::id hierarchy_thread;
  std::thread::id node_thread;
};

auto ReadThroughBridges(AssetLoader* loader, const AssetKey key, NodePath path,
  BridgedReads* reads) -> oxygen::co::Co<>
{
  oxygen::co::Event hierarchy_read;
  oxygen::co::Event node_read;
  OXCO_WITH_NURSERY(nursery)
  {
    co_await nursery.Start(&AssetLoader::ActivateAsync, loader);
    loader->Run();
    loader->StartReadMountedSceneHierarchy(key,
      [reads, &hierarchy_read](
        std::optional<IAssetLoader::SceneHierarchy> hierarchy) -> void {
        reads->hierarchy = std::move(hierarchy);
        reads->hierarchy_thread = std::this_thread::get_id();
        hierarchy_read.Trigger();
      });
    loader->StartReadMountedSceneNode(key, std::move(path),
      [reads, &node_read](
        std::optional<IAssetLoader::SceneNodeValues> node) -> void {
        reads->node = std::move(node);
        reads->node_thread = std::this_thread::get_id();
        node_read.Trigger();
      });
    co_await hierarchy_read;
    co_await node_read;
    loader->Stop();
    co_return oxygen::co::kJoin;
  };
}

//! Mounts one loose cooked root: a street that instances a car that instances
//! the model, each changing the wheel's position; a garage that instances a
//! scene that is not mounted; and two scenes that instance each other.
class AssetLoaderSceneQueryTest : public AssetLoaderPoolTest {
protected:
  auto SetUp() -> void override
  {
    const auto geometry = content_.Geometry(kWheelGeometry);
    const auto paint = content_.Material(kPaint);
    auto model = Model({ .geometry = geometry, .paint = paint });
    body_ = model.scene.IdOf(model.body);
    wheel_ = model.scene.IdOf(model.wheel);
    auto car = Instancing({
      .name = "Car",
      .source = Key(kModel),
      .changes = { { { wheel_ }, kCarX } },
    });
    car_root_ = car.scene.IdOf(car.root);
    car_holder_ = car.scene.IdOf(car.holder);
    auto street = Instancing({
      .name = "Street",
      .source = Key(kCar),
      .changes = { { { car_holder_, wheel_ }, kStreetX } },
    });
    street_root_ = street.scene.IdOf(street.root);
    street_holder_ = street.scene.IdOf(street.holder);
    auto garage = Instancing({ .name = "Garage", .source = Key(kMissing) });
    garage_holder_ = garage.scene.IdOf(garage.holder);
    auto loop_a = Instancing({ .name = "LoopA", .source = Key(kLoopB) });
    loop_a_holder_ = loop_a.scene.IdOf(loop_a.holder);
    auto loop_b = Instancing({ .name = "LoopB", .source = Key(kLoopA) });
    loop_b_holder_ = loop_b.scene.IdOf(loop_b.holder);

    content_.Scene(kModel, model.scene);
    content_.Scene(kCar, car.scene);
    content_.Scene(kStreet, street.scene);
    content_.Scene(kGarage, garage.scene);
    content_.Scene(kLoopA, loop_a.scene);
    content_.Scene(kLoopB, loop_b.scene);
    const auto root = TempDir() / "scene_query";
    oxygen::cooker::testing::Materialize(content_.Files(), root);
    loader_.AddLooseCookedRoot(root);
  }

  [[nodiscard]] auto Key(const std::string_view leaf) const -> AssetKey
  {
    return content_.KeyOf(leaf);
  }

  [[nodiscard]] auto ReadNode(const std::string_view scene, NodePath path)
    -> std::optional<IAssetLoader::SceneNodeValues>
  {
    return oxygen::co::Run(
      loop_, loader_.ReadMountedSceneNodeAsync(Key(scene), std::move(path)));
  }

  [[nodiscard]] auto ReadHierarchy(const std::string_view scene)
    -> std::optional<IAssetLoader::SceneHierarchy>
  {
    return oxygen::co::Run(
      loop_, loader_.ReadMountedSceneHierarchyAsync(Key(scene)));
  }

  //! The street's hierarchy, which every hierarchy test reads.
  [[nodiscard]] auto StreetHierarchy() -> IAssetLoader::SceneHierarchy
  {
    auto hierarchy = ReadHierarchy(kStreet);
    EXPECT_TRUE(hierarchy.has_value());
    return hierarchy.value_or(IAssetLoader::SceneHierarchy {});
  }

  [[nodiscard]] auto StreetWheel() const -> NodePath
  {
    return { street_holder_, car_holder_, wheel_ };
  }
  [[nodiscard]] auto StreetBody() const -> NodePath
  {
    return { street_holder_, car_holder_, body_ };
  }

  LooseCookedRoot content_ { "test" };
  NodeId body_;
  NodeId wheel_;
  NodeId car_root_;
  NodeId car_holder_;
  NodeId street_root_;
  NodeId street_holder_;
  NodeId garage_holder_;
  NodeId loop_a_holder_;
  NodeId loop_b_holder_;
};

//! The outermost scene's change wins, two nesting levels down; each level read
//! alone applies its own changes.
NOLINT_TEST_F(AssetLoaderSceneQueryTest, NodeValuesApplyNestedChanges)
{
  const auto street = ReadNode(kStreet, StreetWheel());
  const auto car = ReadNode(kCar, { car_holder_, wheel_ });
  const auto model = ReadNode(kModel, { wheel_ });

  ASSERT_HAS_VALUE(street);
  ASSERT_HAS_VALUE(car);
  ASSERT_HAS_VALUE(model);
  EXPECT_EQ(PositionXOf(*street), kStreetX);
  EXPECT_EQ(PositionXOf(*car), kCarX);
  EXPECT_EQ(PositionXOf(*model), kModelX);
}

//! A node's values: its name, its records' properties, its geometry and the
//! geometry values it replaces, assets by their mounted virtual paths.
NOLINT_TEST_F(AssetLoaderSceneQueryTest, NodeValuesHoldEveryComponent)
{
  const auto wheel = ReadNode(kCar, { car_holder_, wheel_ });
  const auto body = ReadNode(kCar, { car_holder_, body_ });

  ASSERT_HAS_VALUE(wheel);
  EXPECT_EQ(wheel->name, "Wheel");
  ASSERT_HAS_VALUE(wheel->geometry);
  EXPECT_EQ(wheel->geometry->key, Key(kWheelGeometry));
  EXPECT_EQ(
    wheel->geometry->virtual_path, content_.VirtualPath(kWheelGeometry));
  EXPECT_TRUE(ValueOf(*wheel, OverrideId::kPointLightRange).has_value());
  EXPECT_TRUE(ValueOf(*wheel, OverrideId::kNodeVisibility).has_value());
  EXPECT_FALSE(ValueOf(*wheel, OverrideId::kPerspectiveCameraFieldOfViewDegrees)
      .has_value());
  const auto paint
    = ValueOf(*wheel, OverrideId::kGeometrySurfaceMaterial, RimSurface());
  ASSERT_HAS_VALUE(paint);
  const auto* paint_asset = std::get_if<IAssetLoader::SceneNodeAsset>(&*paint);
  ASSERT_NE(paint_asset, nullptr);
  EXPECT_EQ(paint_asset->key, Key(kPaint));
  EXPECT_EQ(paint_asset->virtual_path, content_.VirtualPath(kPaint));

  ASSERT_HAS_VALUE(body);
  EXPECT_EQ(body->name, "Body");
  EXPECT_FALSE(body->geometry.has_value());
  EXPECT_TRUE(ValueOf(*body, OverrideId::kPerspectiveCameraFieldOfViewDegrees)
      .has_value());
  EXPECT_FALSE(ValueOf(*body, OverrideId::kPointLightRange).has_value());
}

//! A scene's own node reads; a node below an instanced scene that is not
//! mounted does not, and the hierarchy reports the instance.
NOLINT_TEST_F(AssetLoaderSceneQueryTest, UnreadableInstancedSceneHasNoNodes)
{
  EXPECT_TRUE(ReadNode(kGarage, { garage_holder_ }).has_value());
  EXPECT_FALSE(ReadNode(kGarage, { garage_holder_, body_ }).has_value());

  const auto hierarchy = ReadHierarchy(kGarage);

  ASSERT_HAS_VALUE(hierarchy);
  EXPECT_EQ(hierarchy->nodes.size(), 2U);
  ASSERT_EQ(hierarchy->issues.size(), 1U);
  const auto& issue = hierarchy->issues.front();
  EXPECT_EQ(issue.code, SceneIssueCode::kSceneUnavailable);
  EXPECT_EQ(issue.node, IndexOf(*hierarchy, std::array { garage_holder_ }));
  EXPECT_EQ(issue.scene_key, Key(kMissing));
}

//! Scenes that instance each other end at the cycle: the nodes before it
//! read, none after it, and the hierarchy reports it.
NOLINT_TEST_F(AssetLoaderSceneQueryTest, CycleEndsTheComposition)
{
  EXPECT_TRUE(ReadNode(kLoopA, { loop_a_holder_, loop_b_holder_ }).has_value());
  EXPECT_FALSE(
    ReadNode(kLoopA, { loop_a_holder_, loop_b_holder_, loop_a_holder_ })
      .has_value());

  const auto hierarchy = ReadHierarchy(kLoopA);

  ASSERT_HAS_VALUE(hierarchy);
  EXPECT_EQ(hierarchy->nodes.size(), 4U);
  ASSERT_EQ(hierarchy->issues.size(), 1U);
  EXPECT_EQ(hierarchy->issues.front().code, SceneIssueCode::kCycle);
  EXPECT_EQ(hierarchy->issues.front().scene_key, Key(kLoopA));
}

//! A key no mounted source provides, or a path that names no node, reads
//! nothing.
NOLINT_TEST_F(AssetLoaderSceneQueryTest, UnknownKeyOrPathReadsNothing)
{
  EXPECT_FALSE(ReadNode(kMissing, { body_ }).has_value());
  EXPECT_FALSE(ReadHierarchy(kMissing).has_value());
  EXPECT_FALSE(ReadNode(kCar, { wheel_ }).has_value());
  EXPECT_FALSE(ReadNode(kCar, {}).has_value());
}

//! The hierarchy of a two-level nesting lists every node by its path below
//! the scene read, in composition order, with its parent, defining scene and
//! instanced scene.
NOLINT_TEST_F(AssetLoaderSceneQueryTest, HierarchyListsNestedNodes)
{
  const auto hierarchy = StreetHierarchy();

  EXPECT_TRUE(hierarchy.issues.empty());
  const auto paths = std::vector<NodePath> {
    { street_root_ },
    { street_holder_ },
    { street_holder_, car_root_ },
    { street_holder_, car_holder_ },
    StreetBody(),
    StreetWheel(),
  };
  ASSERT_EQ(hierarchy.nodes.size(), paths.size());
  for (const auto& [node, path] : std::views::zip(hierarchy.nodes, paths)) {
    EXPECT_EQ(node.path, path);
  }
  const auto& street_root = hierarchy.nodes.front();
  const auto& street_holder = hierarchy.nodes.at(1);
  const auto& car_holder = hierarchy.nodes.at(3);
  const auto& wheel = hierarchy.nodes.back();
  EXPECT_FALSE(street_root.parent.has_value());
  EXPECT_EQ(street_holder.instance, std::optional(Key(kCar)));
  EXPECT_EQ(car_holder.instance, std::optional(Key(kModel)));
  EXPECT_EQ(wheel.name, "Wheel");
  EXPECT_EQ(wheel.scene_key, Key(kModel));
  EXPECT_EQ(wheel.parent, IndexOf(hierarchy, StreetBody()));
}

//! Each node names the geometry it renders by its mounted virtual path and
//! says whether it has a camera, so a scene can start through a camera an
//! instance brings in.
NOLINT_TEST_F(AssetLoaderSceneQueryTest, HierarchyNodesNameGeometryAndCameras)
{
  const auto hierarchy = StreetHierarchy();

  const auto wheel = IndexOf(hierarchy, StreetWheel());
  const auto body = IndexOf(hierarchy, StreetBody());
  ASSERT_HAS_VALUE(wheel);
  ASSERT_HAS_VALUE(body);
  const auto& wheel_node = hierarchy.nodes.at(*wheel);
  const auto& body_node = hierarchy.nodes.at(*body);
  ASSERT_HAS_VALUE(wheel_node.geometry);
  EXPECT_EQ(wheel_node.geometry->key, Key(kWheelGeometry));
  EXPECT_EQ(
    wheel_node.geometry->virtual_path, content_.VirtualPath(kWheelGeometry));
  EXPECT_FALSE(wheel_node.camera);
  EXPECT_FALSE(body_node.geometry.has_value());
  EXPECT_TRUE(body_node.camera);
}

//! The callback bridges deliver what the async reads return, on the owning
//! thread.
NOLINT_TEST_F(AssetLoaderSceneQueryTest, BridgesCompleteOnTheOwningThread)
{
  const auto expected_nodes = StreetHierarchy().nodes.size();
  auto reads = BridgedReads {};

  oxygen::co::Run(
    loop_, ReadThroughBridges(&loader_, Key(kStreet), StreetWheel(), &reads));

  const auto owner = std::this_thread::get_id();
  ASSERT_HAS_VALUE(reads.hierarchy);
  ASSERT_HAS_VALUE(reads.node);
  EXPECT_EQ(reads.hierarchy->nodes.size(), expected_nodes);
  EXPECT_EQ(PositionXOf(*reads.node), kStreetX);
  EXPECT_EQ(reads.hierarchy_thread, owner);
  EXPECT_EQ(reads.node_thread, owner);
}

} // namespace
