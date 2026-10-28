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
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Cooker/Loose/Validation.h>
#include <Oxygen/Cooker/Tools/Common/ToolApp.h>
#include <Oxygen/Cooker/Tools/Inspector/SceneMetadata.h>
#include <Oxygen/Cooker/test/Pak/PakTestSupport.h>
#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneChangeValues.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/SceneRecordDefaults.h>
#include <Oxygen/Data/SceneRecordFields.h>
#include <Oxygen/Data/Schema/Generated.SceneChanges.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/test/Fixtures/SceneAssetBuilder.h>
#include <Oxygen/Testing/GTest.h>

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

using oxygen::content::lc::ValidateRoot;
using oxygen::data::ComponentType;
using oxygen::data::schema::OverrideId;
using oxygen::data::schema::SchemaValue;
using oxygen::data::testing::SceneAssetBuilder;

//! Writes a one-descriptor root without the cooker's descriptor validation, so
//! fixtures can also hold the retired or malformed descriptors the inspector
//! must reject.
auto WriteDescriptorRoot(const std::filesystem::path& root,
  const std::string_view virtual_path, const oxygen::data::AssetType type,
  const std::string_view relpath, std::vector<std::byte> bytes) -> void
{
  const auto assets = std::array {
    oxygen::content::pak::test::AssetSpec {
      .key = oxygen::data::AssetKey::FromVirtualPath(virtual_path),
      .asset_type = type,
      .descriptor_relpath = std::string(relpath),
      .virtual_path = std::string(virtual_path),
      .descriptor_size = bytes.size(),
      .descriptor_payload = std::move(bytes),
    },
  };
  if (!oxygen::content::pak::test::WriteLooseIndex(root, assets, {}, 1U)) {
    throw std::runtime_error("Could not write the inspection root");
  }
}

struct SceneFixture {
  uint8_t version = world::kSceneAssetVersion;
  uint32_t values = 0U;
  uint32_t inherited = 0U;
};

auto WriteSceneRoot(
  const std::filesystem::path& root, const SceneFixture& fixture) -> void
{
  auto descriptor = world::SceneAssetDesc {};
  descriptor.header.asset_type
    = static_cast<uint8_t>(oxygen::data::AssetType::kScene);
  descriptor.header.version = fixture.version;
  descriptor.nodes.offset = sizeof(descriptor);
  descriptor.nodes.count = 1U;
  descriptor.nodes.entry_size = sizeof(world::NodeRecord);
  constexpr auto strings = std::array { '\0', 'R', 'o', 'o', 't', '\0' };
  descriptor.header.strings.offset
    = sizeof(descriptor) + sizeof(world::NodeRecord);
  descriptor.header.strings.size
    = static_cast<oxygen::data::pak::core::StringTableSizeT>(strings.size());
  auto node = world::NodeRecord {};
  node.node_id
    = oxygen::data::NodeId::FromStableIdentity("/Content/Nodes/Root");
  node.scene_name_offset = 1U;
  node.node_flags = fixture.values;
  node.inherited_flags = fixture.inherited;
  auto environment = world::SceneEnvironmentBlockHeader {};
  environment.byte_size = sizeof(environment);
  auto bytes = std::vector<std::byte>(
    sizeof(descriptor) + sizeof(node) + strings.size() + sizeof(environment));
  std::memcpy(bytes.data(), &descriptor, sizeof(descriptor));
  std::memcpy(
    std::span(bytes).subspan(sizeof(descriptor)).data(), &node, sizeof(node));
  std::memcpy(
    std::span(bytes).subspan(sizeof(descriptor) + sizeof(node)).data(),
    strings.data(), strings.size());
  std::memcpy(std::span(bytes)
                .subspan(sizeof(descriptor) + sizeof(node) + strings.size())
                .data(),
    &environment, sizeof(environment));

  WriteDescriptorRoot(root, "/Content/Scenes/Inspection.oscene",
    oxygen::data::AssetType::kScene, "Scenes/Inspection.oscene",
    std::move(bytes));
}

auto WriteMaterialRoot(const std::filesystem::path& root, const uint8_t version)
  -> void
{
  auto descriptor = oxygen::data::pak::render::MaterialAssetDesc {};
  descriptor.header.asset_type
    = static_cast<uint8_t>(oxygen::data::AssetType::kMaterial);
  descriptor.header.version = version;
  WriteDescriptorRoot(root, "/Content/Materials/Inspection.omat",
    oxygen::data::AssetType::kMaterial, "Materials/Inspection.omat",
    oxygen::data::MakeDescriptorBytes(descriptor));
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

constexpr world::SceneNodeIndexT kHolder = 1U;
constexpr world::SceneNodeIndexT kWheel = 1U;
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

//! A change of `override_id` to `value` below the instance root `root`.
auto Change(const world::SceneNodeIndexT root, const uint32_t path,
  const OverrideId override_id, const SchemaValue& value)
  -> world::OverrideRecord
{
  const auto encoded = oxygen::data::schema::EncodeChange(override_id, value);
  if (!encoded.has_value()) {
    throw std::runtime_error("The test change does not encode");
  }
  return {
    .target = { .node_index = root, .node_path = path },
    .override_id = override_id,
    .value = *encoded,
  };
}

//! A scene whose root `root` has a child `holder` (node 1) that instances
//! `source`, with the node path table `paths`.
struct InstancingSpec {
  std::string_view root;
  std::string_view holder;
  oxygen::data::AssetKey source;
  std::vector<std::vector<oxygen::data::NodeId>> paths;
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

//! `Body` (rotated) with a child `Wheel` at x = 1 that holds a point light.
auto Model() -> SceneAssetBuilder
{
  auto model = SceneAssetBuilder {};
  model.AddNode("Body");
  model.AddNode("Wheel", 0U);
  const auto rotation = world::EulerYxzDegreesToQuaternion(kBodyAngles);
  std::ranges::copy(rotation, std::begin(model.Node(0U).rotation));
  model.Node(kWheel).translation[0] = kModelX;
  auto light = world::MakeDefaultRecord<world::PointLightRecord>();
  light.node_index = kWheel;
  model.AddComponents(ComponentType::kPointLight, std::vector { light });
  return model;
}

auto NodeIdOf(SceneAssetBuilder builder, const world::SceneNodeIndexT index)
  -> oxygen::data::NodeId
{
  return builder.Node(index).node_id;
}

//! The descriptor bytes of `builder`, with the empty environment block every
//! cooked scene ends with.
auto DescriptorBytes(const SceneAssetBuilder& builder) -> std::vector<std::byte>
{
  auto bytes = builder.Bytes();
  const auto environment = world::SceneEnvironmentBlockHeader {
    .byte_size = sizeof(world::SceneEnvironmentBlockHeader),
  };
  const auto tail = std::as_bytes(std::span(&environment, 1));
  bytes.insert(bytes.end(), tail.begin(), tail.end());
  return bytes;
}

auto SceneSpec(const std::string_view virtual_path,
  const SceneAssetBuilder& builder) -> oxygen::content::pak::test::AssetSpec
{
  auto bytes = DescriptorBytes(builder);
  const auto size = bytes.size();
  return {
    .key = KeyOf(virtual_path),
    .asset_type = oxygen::data::AssetType::kScene,
    .descriptor_relpath = std::string(virtual_path.substr(1)),
    .virtual_path = std::string(virtual_path),
    .descriptor_size = size,
    .descriptor_payload = std::move(bytes),
  };
}

//! Street instances Car, which instances Model. Car moves the wheel to x = 2
//! and sets its light's range; Street moves it to x = 3 and changes a node
//! Car does not have. Lonely instances a scene the root does not hold.
auto WriteNestedRoot(const std::filesystem::path& root) -> void
{
  const auto model = Model();
  const auto wheel = NodeIdOf(model, kWheel);
  auto car = Instancing({
    .root = "Car",
    .holder = "CarHolder",
    .source = KeyOf(kModelPath),
    .paths = { { wheel } },
  });
  car.AddComponents(ComponentType::kOverride,
    std::vector {
      Change(kHolder, 0U, OverrideId::kTransformPositionX, kCarX),
      Change(kHolder, 0U, OverrideId::kPointLightRange, kCarRange),
    });
  auto street = Instancing({
    .root = "Street",
    .holder = "StreetHolder",
    .source = KeyOf(kCarPath),
    .paths = {
      { NodeIdOf(car, kHolder), wheel },
      { UnknownNode() },
    },
  });
  street.AddComponents(ComponentType::kOverride,
    std::vector {
      Change(kHolder, 0U, OverrideId::kTransformPositionX, kStreetX),
      Change(kHolder, 1U, OverrideId::kTransformPositionY, 1.0F),
    });
  const auto lonely = Instancing({
    .root = "Lonely",
    .holder = "LonelyHolder",
    .source = KeyOf(kGonePath),
  });
  const auto assets = std::array {
    SceneSpec(kModelPath, model),
    SceneSpec(kCarPath, car),
    SceneSpec(kStreetPath, street),
    SceneSpec(kLonelyPath, lonely),
  };
  if (!oxygen::content::pak::test::WriteLooseIndex(root, assets, {}, 1U)) {
    throw std::runtime_error("Could not write the inspection root");
  }
}

class InspectorRootValidationTest
  : public oxygen::content::pak::test::TempDirFixture {
protected:
  auto Report(const std::filesystem::path& root,
    const std::string_view virtual_path = {}) -> int
  {
    return RunSceneMetadataReport({
      .cooked_root = root.string(),
      .output = Path("scenes.json").string(),
      .virtual_path = std::string(virtual_path),
    });
  }

  auto ReadReport() -> nlohmann::json
  {
    std::ifstream input(Path("scenes.json"));
    auto report = nlohmann::json::parse(input);
    static const auto schema = oxygen::cooker::test::LoadSchema(
      "Tools/Inspector/Schemas/oxygen.cooked-scenes.schema.json");
    EXPECT_THAT(
      oxygen::cooker::test::ValidateJson(schema, report), ::testing::IsEmpty());
    return report;
  }
};

NOLINT_TEST_F(InspectorRootValidationTest, AcceptsCurrentMaterialVersion)
{
  WriteMaterialRoot(Root(), oxygen::data::pak::render::kMaterialAssetVersion);
  EXPECT_NO_THROW(ValidateRoot(Root()));
}

NOLINT_TEST_F(InspectorRootValidationTest, RejectsRetiredMaterialVersion)
{
  WriteMaterialRoot(Root(), 2);
  EXPECT_THROW(ValidateRoot(Root()), std::runtime_error);
}

NOLINT_TEST_F(InspectorRootValidationTest, AcceptsCurrentSceneWithSourceModes)
{
  WriteSceneRoot(Root(),
    {
      .values = world::kSceneNodeFlag_Visible,
      .inherited = world::kSceneNodeFlag_CastsShadows
        | world::kSceneNodeFlag_ReceivesShadows,
    });
  EXPECT_NO_THROW(ValidateRoot(Root()));
}

NOLINT_TEST_F(InspectorRootValidationTest, RejectsRetiredSceneVersion)
{
  constexpr uint8_t kRetiredSceneVersion = 4U;
  WriteSceneRoot(Root(), { .version = kRetiredSceneVersion });
  EXPECT_THROW(ValidateRoot(Root()), std::runtime_error);
  EXPECT_EQ(Report(Root()), 2);
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
    const auto root = Path(std::to_string(index++));
    WriteSceneRoot(root, { .values = values, .inherited = inherited });
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
    const auto root = Path(std::to_string(visible));
    WriteSceneRoot(
      root, { .values = test.values, .inherited = test.inherited });
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
    const auto id = nostd::to_string(
      oxygen::data::NodeId::FromStableIdentity("/Content/Nodes/Root"));
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
  WriteSceneRoot(Root(), {});
  const auto descriptor = Path("Scenes/Inspection.oscene");
  std::filesystem::resize_file(
    descriptor, std::filesystem::file_size(descriptor) - 1U);
  EXPECT_THROW(ValidateRoot(Root()), std::runtime_error);
  EXPECT_EQ(Report(Root()), 2);
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
  WriteNestedRoot(Root());

  ASSERT_EQ(Report(Root()), 0);

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

  const auto model = Model();
  const auto car = Instancing({
    .root = "Car",
    .holder = "CarHolder",
    .source = KeyOf(kModelPath),
  });
  const auto wheel = FindNode(street, 2U, NodeIdOf(model, kWheel));
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
      nostd::to_string(NodeIdOf(car, kHolder)),
      nostd::to_string(NodeIdOf(model, kWheel)),
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
  WriteNestedRoot(Root());

  ASSERT_EQ(Report(Root()), 0);

  const auto report = ReadReport();
  EXPECT_EQ(report.at("scenes").size(), 4U);
  const auto car = FindScene(report, kCarPath);
  const auto model = FindScene(report, kModelPath);
  ASSERT_FALSE(car.is_null() || model.is_null());
  const auto wheel = FindNode(car, 1U, NodeIdOf(Model(), kWheel));
  ASSERT_FALSE(wheel.is_null());
  const auto x
    = Property(wheel, { .kind = "transform", .key = "/local_position/0" });
  ASSERT_FALSE(x.is_null());
  EXPECT_EQ(x.at("value"), kCarX);
  EXPECT_EQ(x.at("supplier"), 0U);
  EXPECT_EQ(model.at("occurrences").size(), 1U);
  const auto own = FindNode(model, 0U, NodeIdOf(Model(), kWheel));
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
  WriteNestedRoot(Root());

  ASSERT_EQ(Report(Root(), kStreetPath), 0);

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
  WriteNestedRoot(Root());

  ASSERT_EQ(Report(Root(), kLonelyPath), 0);

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
  WriteNestedRoot(Root());

  ASSERT_EQ(Report(Root(), kCarPath), 0);
  const auto report = ReadReport();
  ASSERT_EQ(report.at("scenes").size(), 1U);
  EXPECT_EQ(report.at("scenes").front().at("virtual_path"), kCarPath);

  ASSERT_EQ(Report(Root(), "/Content/Scenes/Unknown.oscene"), 0);
  EXPECT_THAT(ReadReport().at("scenes"), testing::IsEmpty());
}

} // namespace
