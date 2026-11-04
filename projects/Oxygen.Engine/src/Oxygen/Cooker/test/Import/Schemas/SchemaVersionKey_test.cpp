//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportSourceAnalysis.h>
#include <Oxygen/Cooker/Import/Internal/SceneSource.h>
#include <Oxygen/Cooker/Import/SceneSourceInspection.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace fs = std::filesystem;
using nlohmann::json;
namespace import = oxygen::content::import;

//! Schemas with no version of their own: fragments other schemas include,
//! descriptors a manifest job embeds, and formats nothing versions.
constexpr auto kUnversioned = std::array<std::string_view, 17> {
  "Bindless.schema.json",
  "TestSceneFactory.schema.json",
  "multiview-validation.schema.json",
  "oxygen.buffer-container.schema.json",
  "oxygen.collision-shape-descriptor.schema.json",
  "oxygen.geometry-descriptor.schema.json",
  "oxygen.input.schema.json",
  "oxygen.lod-policy.schema.json",
  "oxygen.material-descriptor.schema.json",
  "oxygen.name.schema.json",
  "oxygen.physics-material-descriptor.schema.json",
  "oxygen.physics-sidecar.schema.json",
  "oxygen.player-events.schema.json",
  "oxygen.reference.schema.json",
  "oxygen.scripting-sidecar.schema.json",
  "oxygen.texture-descriptor.schema.json",
  "oxygen.transform-component.schema.json",
};

auto IsListed(const std::string_view name) -> bool
{
  for (const auto listed : kUnversioned) {
    if (listed == name) {
      return true;
    }
  }
  return false;
}

auto ReadJson(const fs::path& path) -> json
{
  auto stream = std::ifstream { path };
  return json::parse(stream);
}

auto Const(const std::string_view file) -> json
{
  const auto path = fs::path { OXYGEN_ENGINE_SOURCE_ROOT }
    / "src/Oxygen/Cooker/Import/Schemas" / file;
  return ReadJson(path).at("properties").at("schema_version").at("const");
}

//! One rule over every schema the engine tree holds: a versioned format
//! states one required integer `schema_version`, and no other key names it.
NOLINT_TEST(SchemaVersionKeyTest, EverySchemaStatesOneRequiredIntegerKey)
{
  auto seen = 0;
  for (const auto* folder : { "src", "Examples", "cmake", "tools" }) {
    const auto root = fs::path { OXYGEN_ENGINE_SOURCE_ROOT } / folder;
    for (const auto& entry : fs::recursive_directory_iterator { root }) {
      const auto name = entry.path().filename().string();
      if (!name.ends_with(".schema.json")) {
        continue;
      }
      ++seen;
      SCOPED_TRACE(entry.path().string());
      const auto schema = ReadJson(entry.path());
      const auto properties = schema.value("properties", json::object());
      EXPECT_FALSE(properties.contains("schema"));
      EXPECT_FALSE(properties.contains("report_version"));
      // Only the source provenance record states a product version too.
      if (name != "oxygen-source.schema.json") {
        EXPECT_FALSE(properties.contains("version"));
      }
      if (IsListed(name)) {
        EXPECT_FALSE(properties.contains("schema_version"));
        continue;
      }
      ASSERT_TRUE(properties.contains("schema_version"));
      EXPECT_TRUE(properties.at("schema_version")
          .value("const", json {})
          .is_number_integer());
      const auto required = schema.value("required", json::array());
      EXPECT_NE(
        std::ranges::find(required, json("schema_version")), required.end());
    }
  }
  EXPECT_GT(seen, 0);
}

//! Each owner constant is the version its schema requires.
NOLINT_TEST(SchemaVersionKeyTest, OwnerConstantsMatchTheirSchemas)
{
  EXPECT_EQ(Const("oxygen.mounts.schema.json"), import::kMountsSchemaVersion);
  EXPECT_EQ(Const("oxygen.import-manifest.schema.json"),
    import::kImportManifestSchemaVersion);
  EXPECT_EQ(Const("oxygen.scene-descriptor.schema.json"),
    import::internal::kSceneDescriptorSchemaVersion);
  EXPECT_EQ(Const("oxygen.source-analysis.schema.json"),
    import::kSourceAnalysisSchemaVersion);
  EXPECT_EQ(Const("oxygen.scene-source-inspection.schema.json"),
    import::kSceneSourceInspectionSchemaVersion);
}

} // namespace
