//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/Inspector/ScriptMetadata.cpp

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Tools/Common/ToolApp.h>
#include <Oxygen/Cooker/Tools/Inspector/ScriptMetadata.h>
#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Cooker/testing/DescriptorDefects.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/ScriptBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {

namespace scripting = oxygen::data::pak::scripting;
using oxygen::content::inspection::ScriptMetadataOptions;

//! `RunScriptMetadataReport` as the tool runs it: the error, if any, goes to
//! stderr, and the result is the process exit code.
auto RunScriptMetadataReport(const ScriptMetadataOptions& options) -> int
{
  return oxygen::content::toolkit::ExitCodeOf(std::cerr, [&] {
    return oxygen::content::inspection::RunScriptMetadataReport(options);
  });
}

using oxygen::cooker::testing::DescriptorDefects;
using oxygen::cooker::testing::LooseAsset;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::Materialize;
using oxygen::cooker::testing::ScriptBuilder;
using oxygen::data::AssetKey;
using oxygen::data::AssetType;
using oxygen::data::ScriptParam;

constexpr auto kScriptLeaf = "Scripts/Turret.oscript";
constexpr auto kScriptPath = "/Content/Scripts/Turret.oscript";

//! A script declaring one parameter of each kind the report distinguishes.
auto ScriptAsset(const uint8_t version = scripting::kScriptAssetVersion)
  -> LooseAsset
{
  auto script = ScriptBuilder { "Turret" };
  script.Parameters({
    { "speed", ScriptParam { 2.5F } },
    { "count", ScriptParam { int32_t { 3 } } },
    { "armed", ScriptParam { true } },
    { "mode", ScriptParam { std::string("walk") } },
    { "offset", ScriptParam { oxygen::Vec3 { 1.0F, 2.0F, 3.0F } } },
    {
      "fire",
      ScriptParam { oxygen::data::ScriptAssetParam {
        .key = AssetKey {},
        .asset_type = AssetType::kInputAction,
      } },
    },
    { "target", ScriptParam { oxygen::data::ScriptNodeParam {} } },
  });
  auto descriptor = script.Bytes();
  if (version != scripting::kScriptAssetVersion) {
    DescriptorDefects::SetVersion(descriptor, version);
  }
  return LooseAsset {
    .type = AssetType::kScript,
    .leaf = kScriptLeaf,
    .descriptor = std::move(descriptor),
    .references = script.References(),
  };
}

class InspectorScriptMetadataTest : public oxygen::testing::TempDirTest {
protected:
  auto WriteRoot(const uint8_t version = scripting::kScriptAssetVersion) -> void
  {
    auto library = LooseCookedRoot { "Content" };
    library.Add(ScriptAsset(version));
    Materialize(library.Files(), TempDir());
  }

  //! A report beside the cooked root, which is `TempDir()`.
  [[nodiscard]] auto ReportPath(const std::string_view name) const
    -> std::filesystem::path
  {
    return reports_.Path() / name;
  }

  auto Report(std::string virtual_path = {}) -> int
  {
    return RunScriptMetadataReport({
      .cooked_root = TempDir().string(),
      .output = ReportPath("scripts.json").string(),
      .virtual_path = std::move(virtual_path),
    });
  }

  auto ReadReport() -> nlohmann::json
  {
    std::ifstream input(ReportPath("scripts.json"));
    auto report = nlohmann::json::parse(input);
    static const auto schema = oxygen::cooker::test::LoadSchema(
      "Tools/Inspector/Schemas/oxygen.cooked-scripts.schema.json");
    EXPECT_THAT(
      oxygen::cooker::test::ValidateJson(schema, report), ::testing::IsEmpty());
    EXPECT_EQ(report.at("schema_version"),
      oxygen::content::inspection::kScriptMetadataSchemaVersion);
    return report;
  }

private:
  oxygen::testing::ScopedTempDir reports_;
};

NOLINT_TEST_F(InspectorScriptMetadataTest, ReportsDeclarationsInOrder)
{
  WriteRoot();
  ASSERT_EQ(Report(), 0);
  const auto report = ReadReport();
  ASSERT_EQ(report.at("scripts").size(), 1U);
  const auto& script = report.at("scripts").front();
  EXPECT_EQ(script.at("virtual_path"), kScriptPath);
  EXPECT_EQ(script.at("asset_key"),
    oxygen::data::to_string(AssetKey::FromVirtualPath(kScriptPath)));
  const auto expected = nlohmann::json::parse(R"([
    { "name": "speed", "type": "float", "default": 2.5 },
    { "name": "count", "type": "int32", "default": 3 },
    { "name": "armed", "type": "bool", "default": true },
    { "name": "mode", "type": "string", "default": "walk" },
    { "name": "offset", "type": "vec3", "default": [1.0, 2.0, 3.0] },
    { "name": "fire", "type": "asset", "asset_type": 5 },
    { "name": "target", "type": "node" }
  ])");
  static_assert(static_cast<uint8_t>(AssetType::kInputAction) == 5U);
  EXPECT_EQ(script.at("parameters"), expected);
}

NOLINT_TEST_F(InspectorScriptMetadataTest, FiltersByNativeVirtualPath)
{
  WriteRoot();
  ASSERT_EQ(Report("/Content/Scripts/Absent.oscript"), 0);
  EXPECT_TRUE(ReadReport().at("scripts").empty());
  ASSERT_EQ(Report(kScriptPath), 0);
  EXPECT_EQ(ReadReport().at("scripts").size(), 1U);
}

//! A script that cannot be read fails the run, and its row says why while the
//! other rows stay usable.
NOLINT_TEST_F(InspectorScriptMetadataTest, ReportsARetiredScriptAsAFailedRow)
{
  auto library = LooseCookedRoot { "Content" };
  library.Add(
    ScriptAsset(static_cast<uint8_t>(scripting::kScriptAssetVersion - 1U)));
  auto other = ScriptAsset();
  other.leaf = "Scripts/Other.oscript";
  library.Add(std::move(other));
  Materialize(library.Files(), TempDir());

  EXPECT_EQ(Report(), 2);

  const auto report = ReadReport();
  EXPECT_EQ(report.at("complete"), false);
  ASSERT_EQ(report.at("scripts").size(), 2U);
  for (const auto& row : report.at("scripts")) {
    const auto retired = row.at("virtual_path") == kScriptPath;
    EXPECT_EQ(row.at("complete"), !retired);
    EXPECT_EQ(row.contains("diagnostic"), retired);
    EXPECT_EQ(row.at("parameters").empty(), retired);
  }
}

} // namespace
