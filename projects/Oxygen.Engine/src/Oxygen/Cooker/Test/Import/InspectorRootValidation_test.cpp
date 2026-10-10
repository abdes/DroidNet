//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Cooker/Loose/Validation.h>
#include <Oxygen/Cooker/Test/Pak/PakTestSupport.h>
#include <Oxygen/Cooker/Tools/Inspector/SceneMetadata.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace world = oxygen::data::pak::world;
using oxygen::content::inspection::RunSceneMetadataReport;
using oxygen::content::lc::ValidateRoot;

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
  descriptor.scene_strings.offset
    = sizeof(descriptor) + sizeof(world::NodeRecord);
  descriptor.scene_strings.size
    = static_cast<oxygen::data::pak::core::StringTableSizeT>(strings.size());
  auto node = world::NodeRecord {};
  node.node_id = oxygen::data::AssetKey::FromVirtualPath("/Content/Nodes/Root");
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
  const auto bytes = std::as_bytes(std::span(&descriptor, 1));
  WriteDescriptorRoot(root, "/Content/Materials/Inspection.omat",
    oxygen::data::AssetType::kMaterial, "Materials/Inspection.omat",
    { bytes.begin(), bytes.end() });
}

class InspectorRootValidationTest
  : public oxygen::content::pak::test::TempDirFixture {
protected:
  auto Report(const std::filesystem::path& root) -> int
  {
    return RunSceneMetadataReport(
      { .cooked_root = root.string(), .output = Path("scenes.json").string() });
  }

  auto ReadReport() -> nlohmann::json
  {
    std::ifstream input(Path("scenes.json"));
    auto report = nlohmann::json::parse(input);
    const auto schema_path = std::filesystem::path(__FILE__).parent_path()
      / "../../Tools/Inspector/Schemas/oxygen.cooked-scenes.schema.json";
    std::ifstream schema(schema_path);
    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(nlohmann::json::parse(schema));
    validator.validate(report);
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
  struct FlagsCase {
    uint32_t values;
    uint32_t inherited;
    const char* visible;
    const char* shadows;
  };
  constexpr auto cases = std::array {
    FlagsCase {
      .values = 0U,
      .inherited = 0U,
      .visible = "hidden",
      .shadows = "off",
    },
    FlagsCase {
      .values = world::kSceneNodeFlags_Inheritable,
      .inherited = 0U,
      .visible = "shown",
      .shadows = "on",
    },
    FlagsCase {
      .values = 0U,
      .inherited = world::kSceneNodeFlags_Inheritable,
      .visible = "inherit",
      .shadows = "inherit",
    },
  };
  for (const auto& test : cases) {
    SCOPED_TRACE(test.visible);
    const auto root = Path(test.visible);
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
    EXPECT_EQ(node.at("parent_index"), 0U);
    EXPECT_EQ(node.at("name"), "Root");
    EXPECT_EQ(node.at("node_id"),
      nostd::to_string(
        oxygen::data::AssetKey::FromVirtualPath("/Content/Nodes/Root")));
    EXPECT_EQ(node.at("flags").at("visible"), test.visible);
    EXPECT_EQ(node.at("flags").at("casts_shadows"), test.shadows);
    EXPECT_EQ(node.at("flags").at("receives_shadows"), test.shadows);
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

} // namespace
