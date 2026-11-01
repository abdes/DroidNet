//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace data = oxygen::data;
namespace world = oxygen::data::pak::world;
using data::AssetKey;
using data::NodeId;
using data::schema::OverrideId;
using oxygen::content::IAssetLoader;
using oxygen::content::testing::AssetLoaderBasicTest;
using oxygen::cooker::testing::NodeTransform;
using oxygen::cooker::testing::SceneBuilder;
using oxygen::cooker::testing::SceneNode;
using SceneIssueCode = IAssetLoader::SceneHierarchyIssueCode;

constexpr float kModelX = 1.0F;
constexpr float kCarX = 2.0F;
constexpr float kStreetX = 3.0F;

auto ModelKey() -> AssetKey
{
  return AssetKey::FromVirtualPath("/test/query/Model.oscene");
}
auto CarKey() -> AssetKey
{
  return AssetKey::FromVirtualPath("/test/query/Car.oscene");
}
auto StreetKey() -> AssetKey
{
  return AssetKey::FromVirtualPath("/test/query/Street.oscene");
}
auto GarageKey() -> AssetKey
{
  return AssetKey::FromVirtualPath("/test/query/Garage.oscene");
}
auto MissingKey() -> AssetKey
{
  return AssetKey::FromVirtualPath("/test/query/Missing.oscene");
}
auto LoopAKey() -> AssetKey
{
  return AssetKey::FromVirtualPath("/test/query/LoopA.oscene");
}
auto LoopBKey() -> AssetKey
{
  return AssetKey::FromVirtualPath("/test/query/LoopB.oscene");
}
auto WheelGeometryKey() -> AssetKey
{
  return AssetKey::FromVirtualPath("/test/query/Wheel.ogeo");
}
auto PaintKey() -> AssetKey
{
  return AssetKey::FromVirtualPath("/test/query/Paint.omat");
}
auto RimSurface() -> data::SurfaceId
{
  return data::SurfaceId::FromStableIdentity("/test/query/rim");
}

constexpr std::string_view kWheelGeometryPath = "/test/query/Wheel.ogeo";
constexpr std::string_view kPaintPath = "/test/query/Paint.omat";

//! `Body` with a perspective camera and a child `Wheel` at x = 1, which
//! renders the wheel geometry with its rim painted, and holds a point light.
struct ModelFixture {
  SceneBuilder scene;
  SceneNode body;
  SceneNode wheel;
};

auto Model() -> ModelFixture
{
  auto scene = SceneBuilder { "Model" };
  const auto body = scene.Node("Body");
  const auto wheel = scene.Node("Wheel", body);
  scene.Place(wheel, NodeTransform { .translation = { kModelX, 0.0F, 0.0F } });
  scene.PerspectiveCamera(body);
  scene.Renderable(wheel, WheelGeometryKey());
  scene.PointLight(wheel);
  auto revision = oxygen::base::Sha256Digest {};
  revision.fill(1U);
  auto paint = world::OverrideValue {};
  paint.as_asset = PaintKey();
  scene.Override(
    wheel, OverrideId::kGeometrySurfaceMaterial, paint, RimSurface(), revision);
  return { .scene = std::move(scene), .body = body, .wheel = wheel };
}

//! A scene with a root `<name>Root` and its child `<name>Holder`, which
//! instances `source`; each change sets a position X below the holder.
struct InstancingSpec {
  std::string_view name;
  AssetKey source;
  std::vector<std::pair<std::vector<NodeId>, float>> changes;
};

struct InstancingFixture {
  SceneBuilder scene;
  SceneNode root;
  SceneNode holder;
};

auto Instancing(const InstancingSpec& spec) -> InstancingFixture
{
  auto scene = SceneBuilder { std::string(spec.name) };
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

//! Mounts one loose cooked root: a street that instances a car that instances
//! the model, each changing the wheel's position; a garage that instances a
//! scene that is not mounted; and two scenes that instance each other.
class AssetLoaderSceneQueryTest : public AssetLoaderBasicTest {
protected:
  auto SetUp() -> void override
  {
    AssetLoaderBasicTest::SetUp();
    auto model = Model();
    body_ = model.scene.IdOf(model.body);
    wheel_ = model.scene.IdOf(model.wheel);
    auto car = Instancing({
      .name = "Car",
      .source = ModelKey(),
      .changes = { { { wheel_ }, kCarX } },
    });
    car_root_ = car.scene.IdOf(car.root);
    car_holder_ = car.scene.IdOf(car.holder);
    auto street = Instancing({
      .name = "Street",
      .source = CarKey(),
      .changes = { { { car_holder_, wheel_ }, kStreetX } },
    });
    street_root_ = street.scene.IdOf(street.root);
    street_holder_ = street.scene.IdOf(street.holder);
    auto garage = Instancing({ .name = "Garage", .source = MissingKey() });
    garage_holder_ = garage.scene.IdOf(garage.holder);
    garage_holder_index_ = garage.holder.Index();
    auto loop_a = Instancing({ .name = "LoopA", .source = LoopBKey() });
    loop_a_holder_ = loop_a.scene.IdOf(loop_a.holder);
    auto loop_b = Instancing({ .name = "LoopB", .source = LoopAKey() });
    loop_b_holder_ = loop_b.scene.IdOf(loop_b.holder);

    const auto root = TempDir() / "scene_query";
    auto source = oxygen::cooker::testing::LooseCookedRoot { "test" };
    const auto write_scene
      = [&source](const AssetKey& key, const std::string_view name,
          const SceneBuilder& scene) -> void {
      source.Add({
        .type = data::AssetType::kScene,
        .leaf = "query/" + std::string(name) + ".oscene",
        .descriptor = scene.Bytes(),
        .references = scene.References(),
        .key = key,
      });
    };
    write_scene(ModelKey(), "Model", model.scene);
    write_scene(CarKey(), "Car", car.scene);
    write_scene(StreetKey(), "Street", street.scene);
    write_scene(GarageKey(), "Garage", garage.scene);
    write_scene(LoopAKey(), "LoopA", loop_a.scene);
    write_scene(LoopBKey(), "LoopB", loop_b.scene);

    EXPECT_EQ(source.Geometry("query/Wheel.ogeo"), WheelGeometryKey());
    EXPECT_EQ(source.Material("query/Paint.omat"), PaintKey());
    oxygen::cooker::testing::Materialize(source.Files(), root);
    asset_loader_->AddLooseCookedRoot(root);
  }

  NodeId body_;
  NodeId wheel_;
  NodeId car_root_;
  NodeId car_holder_;
  NodeId street_root_;
  NodeId street_holder_;
  NodeId garage_holder_;
  NodeId loop_a_holder_;
  NodeId loop_b_holder_;
  world::SceneNodeIndexT garage_holder_index_ = 0U;
};

//! The outermost scene's change wins, two nesting levels down; each level read
//! alone applies its own changes.
NOLINT_TEST_F(AssetLoaderSceneQueryTest, NodeValuesApplyNestedChanges)
{
  const auto street = asset_loader_->ReadMountedSceneNode(
    StreetKey(), std::array { street_holder_, car_holder_, wheel_ });
  const auto car = asset_loader_->ReadMountedSceneNode(
    CarKey(), std::array { car_holder_, wheel_ });
  const auto model
    = asset_loader_->ReadMountedSceneNode(ModelKey(), std::array { wheel_ });

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
  const auto wheel = asset_loader_->ReadMountedSceneNode(
    CarKey(), std::array { car_holder_, wheel_ });
  const auto body = asset_loader_->ReadMountedSceneNode(
    CarKey(), std::array { car_holder_, body_ });

  ASSERT_HAS_VALUE(wheel);
  EXPECT_EQ(wheel->name, "Wheel");
  ASSERT_HAS_VALUE(wheel->geometry);
  EXPECT_EQ(wheel->geometry->key, WheelGeometryKey());
  EXPECT_EQ(wheel->geometry->virtual_path, kWheelGeometryPath);
  EXPECT_TRUE(ValueOf(*wheel, OverrideId::kPointLightRange).has_value());
  EXPECT_TRUE(ValueOf(*wheel, OverrideId::kNodeVisibility).has_value());
  EXPECT_FALSE(ValueOf(*wheel, OverrideId::kPerspectiveCameraFieldOfViewDegrees)
      .has_value());
  const auto paint
    = ValueOf(*wheel, OverrideId::kGeometrySurfaceMaterial, RimSurface());
  ASSERT_HAS_VALUE(paint);
  const auto* paint_asset = std::get_if<IAssetLoader::SceneNodeAsset>(&*paint);
  ASSERT_NE(paint_asset, nullptr);
  EXPECT_EQ(paint_asset->key, PaintKey());
  EXPECT_EQ(paint_asset->virtual_path, kPaintPath);

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
  EXPECT_TRUE(asset_loader_
      ->ReadMountedSceneNode(GarageKey(), std::array { garage_holder_ })
      .has_value());
  EXPECT_FALSE(asset_loader_
      ->ReadMountedSceneNode(GarageKey(), std::array { garage_holder_, body_ })
      .has_value());

  const auto hierarchy = asset_loader_->ReadMountedSceneHierarchy(GarageKey());

  ASSERT_HAS_VALUE(hierarchy);
  EXPECT_EQ(hierarchy->nodes.size(), 2U);
  ASSERT_EQ(hierarchy->issues.size(), 1U);
  EXPECT_EQ(hierarchy->issues.front().code, SceneIssueCode::kSceneUnavailable);
  EXPECT_EQ(hierarchy->issues.front().node,
    std::optional<uint32_t>(garage_holder_index_));
  EXPECT_EQ(hierarchy->issues.front().scene_key, MissingKey());
}

//! Scenes that instance each other end at the cycle: the nodes before it
//! read, none after it, and the hierarchy reports it.
NOLINT_TEST_F(AssetLoaderSceneQueryTest, CycleEndsTheComposition)
{
  EXPECT_TRUE(asset_loader_
      ->ReadMountedSceneNode(
        LoopAKey(), std::array { loop_a_holder_, loop_b_holder_ })
      .has_value());
  EXPECT_FALSE(asset_loader_
      ->ReadMountedSceneNode(LoopAKey(),
        std::array { loop_a_holder_, loop_b_holder_, loop_a_holder_ })
      .has_value());

  const auto hierarchy = asset_loader_->ReadMountedSceneHierarchy(LoopAKey());

  ASSERT_HAS_VALUE(hierarchy);
  EXPECT_EQ(hierarchy->nodes.size(), 4U);
  ASSERT_EQ(hierarchy->issues.size(), 1U);
  EXPECT_EQ(hierarchy->issues.front().code, SceneIssueCode::kCycle);
  EXPECT_EQ(hierarchy->issues.front().scene_key, LoopAKey());
}

//! A key no mounted source provides, or a path that names no node, reads
//! nothing.
NOLINT_TEST_F(AssetLoaderSceneQueryTest, UnknownKeyOrPathReadsNothing)
{
  EXPECT_FALSE(
    asset_loader_->ReadMountedSceneNode(MissingKey(), std::array { body_ })
      .has_value());
  EXPECT_FALSE(asset_loader_->ReadMountedSceneHierarchy(MissingKey()));
  EXPECT_FALSE(
    asset_loader_->ReadMountedSceneNode(CarKey(), std::array { wheel_ })
      .has_value());
  EXPECT_FALSE(
    asset_loader_->ReadMountedSceneNode(CarKey(), std::vector<NodeId> {})
      .has_value());
}

//! The hierarchy of a two-level nesting lists every node by its path below
//! the scene read, with its parent, defining scene and instanced scene.
NOLINT_TEST_F(AssetLoaderSceneQueryTest, HierarchyListsNestedNodes)
{
  const auto hierarchy = asset_loader_->ReadMountedSceneHierarchy(StreetKey());

  ASSERT_HAS_VALUE(hierarchy);
  EXPECT_TRUE(hierarchy->issues.empty());
  using Path = std::vector<NodeId>;
  const auto paths = std::vector<Path> {
    { street_root_ },
    { street_holder_ },
    { street_holder_, car_root_ },
    { street_holder_, car_holder_ },
    { street_holder_, car_holder_, body_ },
    { street_holder_, car_holder_, wheel_ },
  };
  ASSERT_EQ(hierarchy->nodes.size(), paths.size());
  for (std::size_t index = 0; index < paths.size(); ++index) {
    EXPECT_EQ(hierarchy->nodes.at(index).path, paths.at(index)) << index;
  }
  const auto& wheel = hierarchy->nodes.at(5);
  EXPECT_EQ(wheel.name, "Wheel");
  EXPECT_EQ(wheel.scene_key, ModelKey());
  EXPECT_EQ(wheel.parent, std::optional<uint32_t>(4U));
  EXPECT_EQ(hierarchy->nodes.at(1).instance, std::optional(CarKey()));
  EXPECT_EQ(hierarchy->nodes.at(3).instance, std::optional(ModelKey()));
  EXPECT_FALSE(hierarchy->nodes.at(0).parent.has_value());
}

//! Each node names the geometry it renders, by its mounted virtual path; a
//! node that renders nothing names none.
NOLINT_TEST_F(AssetLoaderSceneQueryTest, HierarchyNodesNameTheirGeometry)
{
  const auto hierarchy = asset_loader_->ReadMountedSceneHierarchy(StreetKey());

  ASSERT_HAS_VALUE(hierarchy);
  const auto& wheel = hierarchy->nodes.at(5);
  const auto& body = hierarchy->nodes.at(4);
  ASSERT_HAS_VALUE(wheel.geometry);
  EXPECT_EQ(wheel.geometry->key, WheelGeometryKey());
  EXPECT_EQ(wheel.geometry->virtual_path, kWheelGeometryPath);
  EXPECT_FALSE(body.geometry.has_value());
}

//! Each node says whether it has a camera, so a scene can start through a
//! camera an instance brings in.
NOLINT_TEST_F(
  AssetLoaderSceneQueryTest, HierarchyNodesSayWhetherTheyHaveACamera)
{
  const auto hierarchy = asset_loader_->ReadMountedSceneHierarchy(StreetKey());

  ASSERT_HAS_VALUE(hierarchy);
  EXPECT_TRUE(hierarchy->nodes.at(4).camera);
  EXPECT_FALSE(hierarchy->nodes.at(5).camera);
}

} // namespace
