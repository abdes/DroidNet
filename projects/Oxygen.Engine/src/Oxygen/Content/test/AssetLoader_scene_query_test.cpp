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
#include "Fixtures/LooseCookedTestWriter.h"

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/SceneRecordDefaults.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Data/test/Fixtures/SceneAssetBuilder.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace data = oxygen::data;
namespace world = oxygen::data::pak::world;
using data::AssetKey;
using data::ComponentType;
using data::NodeId;
using data::schema::OverrideId;
using data::testing::SceneAssetBuilder;
using oxygen::content::IAssetLoader;
using oxygen::content::testing::AssetLoaderBasicTest;
using SceneIssueCode = IAssetLoader::SceneHierarchyIssueCode;

constexpr world::SceneNodeIndexT kRoot = 0U;
constexpr world::SceneNodeIndexT kHolder = 1U;
constexpr world::SceneNodeIndexT kBody = 0U;
constexpr world::SceneNodeIndexT kWheel = 1U;
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

auto AssetReference(const AssetKey& key, const data::AssetType type)
  -> data::KeyReference
{
  return {
    .key = key,
    .kind = data::KeyReferenceKind::kAsset,
    .expected_type = type,
  };
}

//! `Body` with a perspective camera and a child `Wheel` at x = 1, which
//! renders the wheel geometry with its rim painted, and holds a point light.
auto Model() -> SceneAssetBuilder
{
  auto model = SceneAssetBuilder {};
  model.AddNode("Body");
  model.AddNode("Wheel", kBody);
  model.Node(kWheel).translation[0] = kModelX;
  auto camera = world::MakeDefaultRecord<world::PerspectiveCameraRecord>();
  camera.node_index = kBody;
  model.AddComponents(
    ComponentType::kPerspectiveCamera, std::vector { camera });
  model.AddComponents(ComponentType::kRenderable,
    std::vector {
      world::RenderableRecord {
        .node_index = kWheel,
        .geometry_key = WheelGeometryKey(),
      },
    });
  auto light = world::MakeDefaultRecord<world::PointLightRecord>();
  light.node_index = kWheel;
  model.AddComponents(ComponentType::kPointLight, std::vector { light });
  auto revision = oxygen::base::Sha256Digest {};
  revision.fill(1U);
  auto paint = world::OverrideRecord {
    .target = { .node_index = kWheel, .node_path = world::kNoNodePath },
    .override_id = OverrideId::kGeometrySurfaceMaterial,
    .part_id = RimSurface(),
    .layout_revision = revision,
  };
  paint.value.as_asset = PaintKey();
  model.AddComponents(ComponentType::kOverride, std::vector { paint });
  return model;
}

//! A scene with a root `<name>Root` and its child `<name>Holder`, which
//! instances `source`; each change sets a position X below the holder.
struct InstancingSpec {
  std::string_view name;
  AssetKey source;
  std::vector<std::pair<std::vector<NodeId>, float>> changes;
};

auto Instancing(const InstancingSpec& spec) -> SceneAssetBuilder
{
  auto builder = SceneAssetBuilder {};
  builder.AddNode(std::string(spec.name) + "Root");
  builder.AddNode(std::string(spec.name) + "Holder", kRoot);
  builder.AddComponents(ComponentType::kSceneInstance,
    std::vector {
      world::SceneInstanceRecord {
        .node_index = kHolder,
        .scene_key = spec.source,
      },
    });
  if (spec.changes.empty()) {
    return builder;
  }
  auto paths = std::vector<world::NodePathRecord> {};
  auto elements = std::vector<world::NodePathElement> {};
  auto overrides = std::vector<world::OverrideRecord> {};
  for (const auto& [path, x] : spec.changes) {
    auto change = world::OverrideRecord {
      .target = { .node_index = kHolder,
        .node_path = static_cast<uint32_t>(paths.size()), },
      .override_id = OverrideId::kTransformPositionX,
    };
    change.value.as_float = x;
    overrides.push_back(change);
    paths.push_back({
      .first = static_cast<uint32_t>(elements.size()),
      .count = static_cast<uint32_t>(path.size()),
    });
    for (const auto& id : path) {
      elements.push_back({ .node_id = id });
    }
  }
  builder.AddComponents(ComponentType::kOverride, overrides);
  builder.AddComponents(ComponentType::kNodePath, paths);
  builder.AddComponents(ComponentType::kNodePathElement, elements);
  return builder;
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
    body_ = model.Node(kBody).node_id;
    wheel_ = model.Node(kWheel).node_id;
    auto car = Instancing({
      .name = "Car",
      .source = ModelKey(),
      .changes = { { { wheel_ }, kCarX } },
    });
    car_root_ = car.Node(kRoot).node_id;
    car_holder_ = car.Node(kHolder).node_id;
    auto street = Instancing({
      .name = "Street",
      .source = CarKey(),
      .changes = { { { car_holder_, wheel_ }, kStreetX } },
    });
    street_root_ = street.Node(kRoot).node_id;
    street_holder_ = street.Node(kHolder).node_id;
    auto garage = Instancing({ .name = "Garage", .source = MissingKey() });
    garage_holder_ = garage.Node(kHolder).node_id;
    auto loop_a = Instancing({ .name = "LoopA", .source = LoopBKey() });
    loop_a_holder_ = loop_a.Node(kHolder).node_id;
    auto loop_b = Instancing({ .name = "LoopB", .source = LoopAKey() });
    loop_b_holder_ = loop_b.Node(kHolder).node_id;

    const auto root = temp_dir_ / "scene_query";
    auto writer = oxygen::content::testing::LooseCookedTestWriter(root);
    const auto write_scene
      = [&writer](const AssetKey& key, const std::string_view name,
          const SceneAssetBuilder& builder,
          std::vector<data::KeyReference> references) {
          const auto file = std::string(name) + ".oscene";
          writer.WriteAssetDescriptor(key, data::AssetType::kScene,
            "/test/query/" + file, file, builder.Bytes(),
            data::AssetReferences::Create({}, std::move(references)).value());
        };
    write_scene(ModelKey(), "Model", model,
      {
        AssetReference(WheelGeometryKey(), data::AssetType::kGeometry),
        AssetReference(PaintKey(), data::AssetType::kMaterial),
      });
    write_scene(CarKey(), "Car", car,
      { AssetReference(ModelKey(), data::AssetType::kScene) });
    write_scene(StreetKey(), "Street", street,
      { AssetReference(CarKey(), data::AssetType::kScene) });
    write_scene(GarageKey(), "Garage", garage,
      { AssetReference(MissingKey(), data::AssetType::kScene) });
    write_scene(LoopAKey(), "LoopA", loop_a,
      { AssetReference(LoopBKey(), data::AssetType::kScene) });
    write_scene(LoopBKey(), "LoopB", loop_b,
      { AssetReference(LoopAKey(), data::AssetType::kScene) });

    auto geometry = data::pak::geometry::GeometryAssetDesc {};
    geometry.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kGeometry);
    writer.WriteAssetDescriptor(WheelGeometryKey(), data::AssetType::kGeometry,
      std::string(kWheelGeometryPath), "Wheel.ogeo",
      data::MakeDescriptorBytes(geometry));
    auto paint = data::pak::render::MaterialAssetDesc {};
    paint.header.asset_type = static_cast<uint8_t>(data::AssetType::kMaterial);
    paint.header.version = data::pak::render::kMaterialAssetVersion;
    writer.WriteAssetDescriptor(PaintKey(), data::AssetType::kMaterial,
      std::string(kPaintPath), "Paint.omat", data::MakeDescriptorBytes(paint));
    static_cast<void>(writer.Finish());
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

  ASSERT_TRUE(street.has_value());
  ASSERT_TRUE(car.has_value());
  ASSERT_TRUE(model.has_value());
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

  ASSERT_TRUE(wheel.has_value());
  EXPECT_EQ(wheel->name, "Wheel");
  ASSERT_TRUE(wheel->geometry.has_value());
  EXPECT_EQ(wheel->geometry->key, WheelGeometryKey());
  EXPECT_EQ(wheel->geometry->virtual_path, kWheelGeometryPath);
  EXPECT_TRUE(ValueOf(*wheel, OverrideId::kPointLightRange).has_value());
  EXPECT_TRUE(ValueOf(*wheel, OverrideId::kNodeVisibility).has_value());
  EXPECT_FALSE(ValueOf(*wheel, OverrideId::kPerspectiveCameraFieldOfViewDegrees)
      .has_value());
  const auto paint
    = ValueOf(*wheel, OverrideId::kGeometrySurfaceMaterial, RimSurface());
  ASSERT_TRUE(paint.has_value());
  const auto* paint_asset = std::get_if<IAssetLoader::SceneNodeAsset>(&*paint);
  ASSERT_NE(paint_asset, nullptr);
  EXPECT_EQ(paint_asset->key, PaintKey());
  EXPECT_EQ(paint_asset->virtual_path, kPaintPath);

  ASSERT_TRUE(body.has_value());
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

  ASSERT_TRUE(hierarchy.has_value());
  EXPECT_EQ(hierarchy->nodes.size(), 2U);
  ASSERT_EQ(hierarchy->issues.size(), 1U);
  EXPECT_EQ(hierarchy->issues.front().code, SceneIssueCode::kSceneUnavailable);
  EXPECT_EQ(hierarchy->issues.front().node, std::optional<uint32_t>(kHolder));
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

  ASSERT_TRUE(hierarchy.has_value());
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

  ASSERT_TRUE(hierarchy.has_value());
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

} // namespace
