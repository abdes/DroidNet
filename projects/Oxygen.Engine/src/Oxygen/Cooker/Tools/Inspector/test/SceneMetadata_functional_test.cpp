//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/Inspector/SceneMetadata.cpp

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Cooked/Validate.h>
#include <Oxygen/Cooker/Tools/Common/ToolApp.h>
#include <Oxygen/Cooker/Tools/Inspector/SceneMetadata.h>
#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Cooker/testing/DescriptorDefects.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneChangeValues.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/SceneRecordFields.h>
#include <Oxygen/Data/Schema/Generated.SceneChanges.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {

namespace world = oxygen::data::pak::world;
using oxygen::content::inspection::SceneMetadataOptions;

//! `RunSceneMetadataReport` as the tool runs it: the error, if any, goes to
//! stderr, and the result is the process exit code.
auto RunSceneMetadataReport(const SceneMetadataOptions& options) -> int
{
  return oxygen::content::toolkit::ExitCodeOf(std::cerr, [&] {
    return oxygen::content::inspection::RunSceneMetadataReport(options);
  });
}

//! Throws when the loose cooked root `root` is not one the runtime loads.
auto ValidateRoot(const std::filesystem::path& root) -> void
{
  using oxygen::content::cooked::CookedContent;
  using oxygen::content::cooked::RealCookerStorage;
  using oxygen::content::cooked::Validate;
  Validate(CookedContent::Open(RealCookerStorage(), root));
}

using oxygen::cooker::testing::DescriptorDefects;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::Materialize;
using oxygen::cooker::testing::NodeFlags;
using oxygen::cooker::testing::NodeTransform;
using oxygen::cooker::testing::SceneBuilder;
using oxygen::cooker::testing::SceneNode;
using oxygen::data::schema::OverrideId;
using oxygen::data::schema::SchemaValue;

//! The library every inspected root is written from: the `Content` root.
auto ContentRoot() -> LooseCookedRoot { return LooseCookedRoot { "Content" }; }

//! Writes a one-descriptor root without the cooker's descriptor validation, so
//! fixtures can also hold the retired or malformed descriptors the inspector
//! must reject.
auto WriteDescriptorRoot(const std::filesystem::path& root,
  const std::string_view leaf, const oxygen::data::AssetType type,
  std::vector<std::byte> bytes) -> void
{
  auto library = ContentRoot();
  library.Add({
    .type = type,
    .leaf = std::string(leaf),
    .descriptor = std::move(bytes),
  });
  Materialize(library.Files(), root);
}

constexpr auto kInspectionLeaf = "Scenes/Inspection.oscene";

//! The scene of one node, `Root`, with the flags `flags`.
struct InspectionScene {
  SceneBuilder builder;
  SceneNode root;
};

auto MakeInspectionScene(const NodeFlags flags = {}) -> InspectionScene
{
  auto builder = SceneBuilder { "Inspection" };
  const auto root = builder.Node("Root");
  builder.Flags(root, flags);
  return { .builder = std::move(builder), .root = root };
}

//! Writes the root holding `scene` as the inspection scene.
auto WriteSceneRoot(
  const std::filesystem::path& root, const InspectionScene& scene) -> void
{
  auto library = ContentRoot();
  library.Scene(kInspectionLeaf, scene.builder);
  Materialize(library.Files(), root);
}

//! Writes the root holding the descriptor `bytes` as the inspection scene,
//! whatever it holds.
auto WriteSceneBytesRoot(
  const std::filesystem::path& root, std::vector<std::byte> bytes) -> void
{
  WriteDescriptorRoot(
    root, kInspectionLeaf, oxygen::data::AssetType::kScene, std::move(bytes));
}

//! Writes the root holding a material of format `version`; a version other than
//! the current one is a defect applied to the valid descriptor.
auto WriteMaterialRoot(const std::filesystem::path& root, const uint8_t version)
  -> void
{
  auto bytes
    = oxygen::cooker::testing::MaterialBuilder { "Inspection" }.Bytes();
  if (version != oxygen::data::pak::render::kMaterialAssetVersion) {
    DescriptorDefects::SetVersion(bytes, version);
  }
  WriteDescriptorRoot(root, "Materials/Inspection.omat",
    oxygen::data::AssetType::kMaterial, std::move(bytes));
}

//! The property `key` of the component `kind` of a report node, or null.
//! A property of a component kind.
struct PropertyName {
  std::string_view kind;
  std::string_view key;
};

auto Property(const nlohmann::json& node, const PropertyName& name)
  -> nlohmann::json
{
  for (const auto& component : node.at("components")) {
    if (component.at("kind").get<std::string>() != name.kind) {
      continue;
    }
    for (const auto& property : component.at("properties")) {
      if (property.at("key").get<std::string>() == name.key) {
        return property;
      }
    }
  }
  return nullptr;
}

//! The value of the property `key` of the component `kind`, or null.
auto Value(const nlohmann::json& node, const PropertyName& name)
  -> nlohmann::json
{
  const auto property = Property(node, name);
  return property.is_null() ? property : property.at("value");
}

//! The report node of `row` with the node ID `id` in occurrence `occurrence`.
auto FindNode(const nlohmann::json& row, const std::size_t occurrence,
  const oxygen::data::NodeId& id) -> nlohmann::json
{
  for (const auto& node : row.at("nodes")) {
    if (node.at("occurrence") == occurrence
      && node.at("node_id") == nostd::to_string(id)) {
      return node;
    }
  }
  return nullptr;
}

//! The report row of the scene with the virtual path `virtual_path`.
auto FindScene(const nlohmann::json& report,
  const std::string_view virtual_path) -> nlohmann::json
{
  for (const auto& row : report.at("scenes")) {
    if (row.at("virtual_path").get<std::string>() == virtual_path) {
      return row;
    }
  }
  return nullptr;
}

constexpr float kModelX = 1.0F;
constexpr float kCarX = 2.0F;
constexpr float kStreetX = 3.0F;
constexpr float kCarRange = 7.0F;
constexpr auto kBodyAngles = std::array { 10.0F, 20.0F, 30.0F };

constexpr auto kModelPath = "/Content/Scenes/Model.oscene";
constexpr auto kCarPath = "/Content/Scenes/Car.oscene";
constexpr auto kStreetPath = "/Content/Scenes/Street.oscene";
constexpr auto kLonelyPath = "/Content/Scenes/Lonely.oscene";
constexpr auto kGonePath = "/Content/Scenes/Gone.oscene";

auto KeyOf(const std::string_view virtual_path) -> oxygen::data::AssetKey
{
  return oxygen::data::AssetKey::FromVirtualPath(virtual_path);
}

auto UnknownNode() -> oxygen::data::NodeId
{
  return oxygen::data::NodeId::FromStableIdentity("/test/node/none");
}

//! The change value of `override_id` set to `value`.
auto Encoded(const OverrideId override_id, const SchemaValue& value)
  -> world::OverrideValue
{
  const auto encoded = oxygen::data::schema::EncodeChange(override_id, value);
  if (!encoded.has_value()) {
    throw std::runtime_error("The test change does not encode");
  }
  return *encoded;
}

//! A scene whose root `root` has a child `holder` that instances `source`.
struct InstancingScene {
  SceneBuilder builder;
  SceneNode holder;
};

struct InstancingSpec {
  std::string_view root;
  std::string_view holder;
  oxygen::data::AssetKey source;
};

auto Instancing(const InstancingSpec& spec) -> InstancingScene
{
  auto builder = SceneBuilder { spec.root };
  const auto top = builder.Node(spec.root);
  const auto child = builder.Node(spec.holder, top);
  builder.Instance(child, spec.source);
  return { .builder = std::move(builder), .holder = child };
}

//! `Body` (rotated) with a child `Wheel` at x = 1 that holds a point light.
struct ModelFixture {
  SceneBuilder builder;
  SceneNode wheel;
};

auto Model() -> ModelFixture
{
  auto builder = SceneBuilder { "Model" };
  const auto body = builder.Node("Body");
  const auto wheel = builder.Node("Wheel", body);
  auto rotated = NodeTransform {};
  std::ranges::copy(
    world::EulerXyzDegreesToQuaternion(kBodyAngles), rotated.rotation.begin());
  builder.Place(body, rotated);
  builder.Place(wheel, { .translation = { kModelX, 0.0F, 0.0F } });
  builder.PointLight(wheel);
  return { .builder = std::move(builder), .wheel = wheel };
}

//! Street instances Car, which instances Model. Car moves the wheel to x = 2
//! and sets its light's range; Street moves it to x = 3 and changes a node
//! Car does not have. Lonely instances a scene the root does not hold.
struct NestedScenes {
  ModelFixture model;
  InstancingScene car;
  InstancingScene street;
  InstancingScene lonely;

  [[nodiscard]] auto WheelId() const -> oxygen::data::NodeId
  {
    return model.builder.IdOf(model.wheel);
  }

  [[nodiscard]] auto CarHolderId() const -> oxygen::data::NodeId
  {
    return car.builder.IdOf(car.holder);
  }
};

auto MakeNestedScenes() -> NestedScenes
{
  auto scenes = NestedScenes {
    .model = Model(),
    .car = Instancing(
      { .root = "Car", .holder = "CarHolder", .source = KeyOf(kModelPath) }),
    .street = Instancing({
      .root = "Street",
      .holder = "StreetHolder",
      .source = KeyOf(kCarPath),
    }),
    .lonely = Instancing({
      .root = "Lonely",
      .holder = "LonelyHolder",
      .source = KeyOf(kGonePath),
    }),
  };
  scenes.car.builder
    .Change(scenes.car.holder, { scenes.WheelId() },
      OverrideId::kTransformPositionX,
      Encoded(OverrideId::kTransformPositionX, kCarX))
    .Change(scenes.car.holder, { scenes.WheelId() },
      OverrideId::kPointLightRange,
      Encoded(OverrideId::kPointLightRange, kCarRange));
  scenes.street.builder
    .Change(scenes.street.holder, { scenes.CarHolderId(), scenes.WheelId() },
      OverrideId::kTransformPositionX,
      Encoded(OverrideId::kTransformPositionX, kStreetX))
    .Change(scenes.street.holder, { UnknownNode() },
      OverrideId::kTransformPositionY,
      Encoded(OverrideId::kTransformPositionY, 1.0F));
  return scenes;
}

auto WriteNestedRoot(
  const std::filesystem::path& root, const NestedScenes& scenes) -> void
{
  constexpr auto leaf = [](const std::string_view virtual_path) -> auto {
    return virtual_path.substr(std::string_view { "/Content/" }.size());
  };
  auto library = ContentRoot();
  library.Scene(leaf(kModelPath), scenes.model.builder);
  library.Scene(leaf(kCarPath), scenes.car.builder);
  library.Scene(leaf(kStreetPath), scenes.street.builder);
  library.Scene(leaf(kLonelyPath), scenes.lonely.builder);
  Materialize(library.Files(), root);
}

class InspectorRootValidationTest : public oxygen::testing::TempDirTest {
protected:
  //! A report beside the cooked root, which is `TempDir()`.
  [[nodiscard]] auto ReportPath(const std::string_view name) const
    -> std::filesystem::path
  {
    return reports_.Path() / name;
  }

  auto Report(const std::filesystem::path& root,
    const std::string_view virtual_path = {}) -> int
  {
    return RunSceneMetadataReport({
      .cooked_root = root.string(),
      .output = ReportPath("scenes.json").string(),
      .virtual_path = std::string(virtual_path),
    });
  }

  auto ReadReport() -> nlohmann::json
  {
    std::ifstream input(ReportPath("scenes.json"));
    auto report = nlohmann::json::parse(input);
    static const auto schema = oxygen::cooker::test::LoadSchema(
      "Tools/Inspector/Schemas/oxygen.cooked-scenes.schema.json");
    EXPECT_THAT(
      oxygen::cooker::test::ValidateJson(schema, report), ::testing::IsEmpty());
    EXPECT_EQ(
      report.at("schema"), oxygen::content::inspection::kSceneMetadataSchema);
    return report;
  }

private:
  oxygen::testing::ScopedTempDir reports_;
};

NOLINT_TEST_F(InspectorRootValidationTest, AcceptsCurrentMaterialVersion)
{
  WriteMaterialRoot(
    TempDir(), oxygen::data::pak::render::kMaterialAssetVersion);
  EXPECT_NO_THROW(ValidateRoot(TempDir()));
}

NOLINT_TEST_F(InspectorRootValidationTest, RejectsRetiredMaterialVersion)
{
  WriteMaterialRoot(TempDir(), 2);
  EXPECT_THROW(ValidateRoot(TempDir()), std::runtime_error);
}

NOLINT_TEST_F(InspectorRootValidationTest, AcceptsCurrentSceneWithSourceModes)
{
  WriteSceneRoot(TempDir(),
    MakeInspectionScene({
      .values = world::kSceneNodeFlag_Visible,
      .inherited = world::kSceneNodeFlag_CastsShadows
        | world::kSceneNodeFlag_ReceivesShadows,
    }));
  EXPECT_NO_THROW(ValidateRoot(TempDir()));
}

NOLINT_TEST_F(InspectorRootValidationTest, RejectsRetiredSceneVersion)
{
  constexpr uint8_t kRetiredSceneVersion = 4U;
  auto bytes = MakeInspectionScene().builder.Bytes();
  DescriptorDefects::SetVersion(bytes, kRetiredSceneVersion);
  WriteSceneBytesRoot(TempDir(), std::move(bytes));
  EXPECT_THROW(ValidateRoot(TempDir()), std::runtime_error);
  EXPECT_EQ(Report(TempDir()), 2);
  const auto report = ReadReport();
  EXPECT_EQ(report.at("complete"), false);
  ASSERT_EQ(report.at("scenes").size(), 1U);
  const auto& row = report.at("scenes").front();
  EXPECT_EQ(row.at("virtual_path"), "/Content/Scenes/Inspection.oscene");
  EXPECT_EQ(row.at("complete"), false);
  EXPECT_THAT(
    row.at("diagnostic").get<std::string>(), testing::HasSubstr("re-cook"));
}

NOLINT_TEST_F(InspectorRootValidationTest, RejectsMalformedSceneFlagSources)
{
  constexpr uint32_t kUnknownFlag = 1U << 31U;
  auto index = size_t { 0 };
  for (const auto [values, inherited] : std::array {
         std::pair { kUnknownFlag, uint32_t { 0 } },
         std::pair { uint32_t { 0 }, world::kSceneNodeFlag_Static },
         std::pair {
           world::kSceneNodeFlag_Visible, world::kSceneNodeFlag_Visible },
       }) {
    const auto root = TempPath(std::to_string(index++));
    auto bytes = MakeInspectionScene().builder.Bytes();
    auto node = DescriptorDefects::NodeAt(bytes, 0U);
    node.node_flags = values;
    node.inherited_flags = inherited;
    DescriptorDefects::SetNode(bytes, 0U, node);
    WriteSceneBytesRoot(root, std::move(bytes));
    EXPECT_THROW(ValidateRoot(root), std::runtime_error);
    EXPECT_EQ(Report(root), 2);
    const auto report = ReadReport();
    EXPECT_EQ(report.at("complete"), false);
    EXPECT_EQ(report.at("scenes").front().at("complete"), false);
    EXPECT_THAT(report.at("scenes").front().at("diagnostic").get<std::string>(),
      testing::HasSubstr("flag source"));
  }
}

NOLINT_TEST_F(
  InspectorRootValidationTest, ReportsNativeIdentityAndEverySourceMode)
{
  using oxygen::data::schema::ShadowSource;
  using oxygen::data::schema::VisibilitySource;
  struct FlagsCase {
    uint32_t values;
    uint32_t inherited;
    VisibilitySource visible;
    ShadowSource shadows;
  };
  constexpr auto cases = std::array {
    FlagsCase {
      .values = 0U,
      .inherited = 0U,
      .visible = VisibilitySource::kHidden,
      .shadows = ShadowSource::kOff,
    },
    FlagsCase {
      .values = world::kSceneNodeFlags_Inheritable,
      .inherited = 0U,
      .visible = VisibilitySource::kShown,
      .shadows = ShadowSource::kOn,
    },
    FlagsCase {
      .values = 0U,
      .inherited = world::kSceneNodeFlags_Inheritable,
      .visible = VisibilitySource::kInherit,
      .shadows = ShadowSource::kInherit,
    },
  };
  for (const auto& test : cases) {
    const auto visible = static_cast<int>(std::to_underlying(test.visible));
    const auto shadows = static_cast<int>(std::to_underlying(test.shadows));
    SCOPED_TRACE(visible);
    const auto root = TempPath(std::to_string(visible));
    const auto scene = MakeInspectionScene({
      .values = test.values,
      .inherited = test.inherited,
    });
    WriteSceneRoot(root, scene);
    ASSERT_EQ(Report(root), 0);
    const auto report = ReadReport();
    EXPECT_EQ(report.at("complete"), true);
    ASSERT_EQ(report.at("scenes").size(), 1U);
    const auto& row = report.at("scenes").front();
    EXPECT_EQ(row.at("descriptor_version"), world::kSceneAssetVersion);
    EXPECT_EQ(row.at("asset_key"),
      nostd::to_string(oxygen::data::AssetKey::FromVirtualPath(
        "/Content/Scenes/Inspection.oscene")));
    ASSERT_EQ(row.at("nodes").size(), 1U);
    const auto& node = row.at("nodes").front();
    EXPECT_EQ(node.at("index"), 0U);
    EXPECT_EQ(node.at("parent"), nullptr);
    EXPECT_EQ(node.at("name"), "Root");
    const auto id = nostd::to_string(scene.builder.IdOf(scene.root));
    EXPECT_EQ(node.at("node_id"), id);
    EXPECT_EQ(node.at("root"), id);
    EXPECT_THAT(node.at("node_path"), testing::IsEmpty());
    EXPECT_EQ(
      Value(node, { .kind = "node-flags", .key = "/visible" }), visible);
    EXPECT_EQ(
      Value(node, { .kind = "node-flags", .key = "/casts_shadows" }), shadows);
    EXPECT_EQ(Value(node, { .kind = "node-flags", .key = "/receives_shadows" }),
      shadows);
  }
}

NOLINT_TEST_F(
  InspectorRootValidationTest, IncompleteSceneFailsWithScopedDiagnostic)
{
  WriteSceneRoot(TempDir(), MakeInspectionScene());
  const auto descriptor = TempPath("Descriptors/Scenes/Inspection.oscene");
  std::filesystem::resize_file(
    descriptor, std::filesystem::file_size(descriptor) - 1U);
  EXPECT_THROW(ValidateRoot(TempDir()), std::runtime_error);
  EXPECT_EQ(Report(TempDir()), 2);
  const auto report = ReadReport();
  const auto& scene = report.at("scenes").front();
  EXPECT_EQ(scene.at("complete"), false);
  EXPECT_EQ(scene.at("virtual_path"), "/Content/Scenes/Inspection.oscene");
  EXPECT_THAT(
    scene.at("diagnostic").get<std::string>(), testing::HasSubstr("size"));
}

//! The outermost scene's change wins two levels down, each value names the
//! occurrence that supplies it, and each instanced scene's roots hang under
//! its instance root. Street's nodes come first (Street, StreetHolder), then
//! Car's (Car, CarHolder), then Model's (Body, Wheel).
NOLINT_TEST_F(InspectorRootValidationTest, ComposesNestedInstances)
{
  const auto scenes = MakeNestedScenes();
  WriteNestedRoot(TempDir(), scenes);

  ASSERT_EQ(Report(TempDir()), 0);

  const auto report = ReadReport();
  EXPECT_EQ(report.at("complete"), true);
  const auto street = FindScene(report, kStreetPath);
  ASSERT_FALSE(street.is_null());
  EXPECT_EQ(street.at("complete"), true);
  const auto& occurrences = street.at("occurrences");
  ASSERT_EQ(occurrences.size(), 3U);
  EXPECT_EQ(occurrences.at(0).at("parent"), nullptr);
  EXPECT_EQ(occurrences.at(1).at("virtual_path"), kCarPath);
  EXPECT_EQ(occurrences.at(1).at("parent"), 0U);
  EXPECT_EQ(occurrences.at(1).at("instance_root"), 1U);
  EXPECT_EQ(occurrences.at(2).at("virtual_path"), kModelPath);
  EXPECT_EQ(occurrences.at(2).at("parent"), 1U);
  EXPECT_EQ(occurrences.at(2).at("instance_root"), 3U);
  const auto& nodes = street.at("nodes");
  ASSERT_EQ(nodes.size(), 6U);

  const auto wheel = FindNode(street, 2U, scenes.WheelId());
  ASSERT_FALSE(wheel.is_null());
  EXPECT_EQ(
    nodes.at(1).at("instance").at("source"), nostd::to_string(KeyOf(kCarPath)));
  EXPECT_EQ(nodes.at(3).at("instance").at("source"),
    nostd::to_string(KeyOf(kModelPath)));
  EXPECT_EQ(nodes.at(0).at("parent"), nullptr);
  EXPECT_EQ(nodes.at(2).at("parent"), 1U);
  EXPECT_EQ(nodes.at(3).at("parent"), 2U);
  EXPECT_EQ(nodes.at(4).at("parent"), 3U);
  EXPECT_EQ(wheel.at("parent"), 4U);
  EXPECT_EQ(wheel.at("defining_scene"), nostd::to_string(KeyOf(kModelPath)));
  EXPECT_EQ(wheel.at("root"), nodes.at(1).at("node_id"));
  EXPECT_EQ(wheel.at("node_path"),
    nlohmann::json::array({
      nostd::to_string(scenes.CarHolderId()),
      nostd::to_string(scenes.WheelId()),
    }));

  const auto x
    = Property(wheel, { .kind = "transform", .key = "/local_position/0" });
  ASSERT_FALSE(x.is_null());
  EXPECT_EQ(x.at("value"), kStreetX);
  EXPECT_EQ(x.at("supplier"), 0U);
  const auto range
    = Property(wheel, { .kind = "point-light", .key = "/range" });
  ASSERT_FALSE(range.is_null());
  EXPECT_EQ(range.at("value"), kCarRange);
  EXPECT_EQ(range.at("supplier"), 1U);
  const auto y
    = Property(wheel, { .kind = "transform", .key = "/local_position/1" });
  ASSERT_FALSE(y.is_null());
  EXPECT_EQ(y.at("value"), 0.0F);
  EXPECT_EQ(y.at("supplier"), 2U);
  const auto rotation = Value(nodes.at(4),
    { .kind = "transform", .key = "/local_rotation_euler_degrees" });
  ASSERT_EQ(rotation.size(), kBodyAngles.size());
  for (auto i = 0U; i < kBodyAngles.size(); ++i) {
    EXPECT_NEAR(rotation.at(i).get<float>(), kBodyAngles.at(i), 1e-3F);
  }
}

//! Each scene composes alone: Car's own change supplies its wheel's position,
//! and a scene without instances composes to itself.
NOLINT_TEST_F(InspectorRootValidationTest, ComposesEveryScene)
{
  const auto scenes = MakeNestedScenes();
  WriteNestedRoot(TempDir(), scenes);

  ASSERT_EQ(Report(TempDir()), 0);

  const auto report = ReadReport();
  EXPECT_EQ(report.at("scenes").size(), 4U);
  const auto car = FindScene(report, kCarPath);
  const auto model = FindScene(report, kModelPath);
  ASSERT_FALSE(car.is_null() || model.is_null());
  const auto wheel = FindNode(car, 1U, scenes.WheelId());
  ASSERT_FALSE(wheel.is_null());
  const auto x
    = Property(wheel, { .kind = "transform", .key = "/local_position/0" });
  ASSERT_FALSE(x.is_null());
  EXPECT_EQ(x.at("value"), kCarX);
  EXPECT_EQ(x.at("supplier"), 0U);
  EXPECT_EQ(model.at("occurrences").size(), 1U);
  const auto own = FindNode(model, 0U, scenes.WheelId());
  ASSERT_FALSE(own.is_null());
  EXPECT_EQ(
    Value(own, { .kind = "transform", .key = "/local_position/0" }), kModelX);
  EXPECT_THAT(own.at("node_path"), testing::IsEmpty());
  EXPECT_THAT(model.at("unresolved"), testing::IsEmpty());
  EXPECT_THAT(model.at("issues"), testing::IsEmpty());
}

//! A change that names no node is reported, addressed as its scene made it.
NOLINT_TEST_F(InspectorRootValidationTest, ReportsUnresolvedChanges)
{
  WriteNestedRoot(TempDir(), MakeNestedScenes());

  ASSERT_EQ(Report(TempDir(), kStreetPath), 0);

  const auto street = FindScene(ReadReport(), kStreetPath);
  ASSERT_FALSE(street.is_null());
  ASSERT_EQ(street.at("unresolved").size(), 1U);
  const auto& change = street.at("unresolved").front();
  EXPECT_EQ(change.at("supplier"), 0U);
  EXPECT_EQ(change.at("instance_root"), 1U);
  EXPECT_EQ(change.at("node_path"),
    nlohmann::json::array({ nostd::to_string(UnknownNode()) }));
  EXPECT_EQ(change.at("property").at("key"), "/local_position/1");
}

//! An instanced scene the root does not hold is an issue of its instance
//! root; the scene still composes.
NOLINT_TEST_F(InspectorRootValidationTest, ReportsMissingInstancedScene)
{
  WriteNestedRoot(TempDir(), MakeNestedScenes());

  ASSERT_EQ(Report(TempDir(), kLonelyPath), 0);

  const auto lonely = FindScene(ReadReport(), kLonelyPath);
  ASSERT_FALSE(lonely.is_null());
  EXPECT_EQ(lonely.at("complete"), true);
  EXPECT_EQ(lonely.at("nodes").size(), 2U);
  ASSERT_EQ(lonely.at("issues").size(), 1U);
  const auto& issue = lonely.at("issues").front();
  EXPECT_EQ(issue.at("code"), "scene.instance.missing");
  EXPECT_EQ(issue.at("node"), 1U);
  EXPECT_EQ(issue.at("scene"), nostd::to_string(KeyOf(kGonePath)));
}

//! A virtual path composes that scene alone; one the root does not hold
//! reports no scene.
NOLINT_TEST_F(InspectorRootValidationTest, VirtualPathSelectsOneScene)
{
  WriteNestedRoot(TempDir(), MakeNestedScenes());

  ASSERT_EQ(Report(TempDir(), kCarPath), 0);
  const auto report = ReadReport();
  ASSERT_EQ(report.at("scenes").size(), 1U);
  EXPECT_EQ(report.at("scenes").front().at("virtual_path"), kCarPath);

  ASSERT_EQ(Report(TempDir(), "/Content/Scenes/Unknown.oscene"), 0);
  EXPECT_THAT(ReadReport().at("scenes"), testing::IsEmpty());
}

} // namespace
