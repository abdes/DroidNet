//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/Inspector/ScriptMetadata.cpp

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Tools/Common/ToolApp.h>
#include <Oxygen/Cooker/Tools/Inspector/ScriptMetadata.h>
#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Testing/GTest.h>
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

using oxygen::cooker::testing::LooseAsset;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::Materialize;
using oxygen::data::AssetKey;
using oxygen::data::AssetType;
using oxygen::data::ScriptParam;

constexpr auto kScriptLeaf = "Scripts/Turret.oscript";
constexpr auto kScriptPath = "/Content/Scripts/Turret.oscript";

auto Record(const std::string_view key, const ScriptParam& value,
  oxygen::data::DescriptorStringsBuilder& strings)
  -> scripting::ScriptParamRecord
{
  const auto record = oxygen::data::MakeScriptParamRecord(key, value, strings);
  if (!record.has_value()) {
    throw std::runtime_error("Could not build a script parameter record");
  }
  return *record;
}

//! A script declaring one parameter of each kind the report distinguishes.
auto ScriptAsset(const uint8_t version = scripting::kScriptAssetVersion)
  -> LooseAsset
{
  auto strings = oxygen::data::DescriptorStringsBuilder {};
  const auto declarations = std::array {
    Record("speed", 2.5F, strings),
    Record("count", int32_t { 3 }, strings),
    Record("armed", true, strings),
    Record("mode", std::string("walk"), strings),
    Record("offset", oxygen::Vec3 { 1.0F, 2.0F, 3.0F }, strings),
    Record("fire",
      oxygen::data::ScriptAssetParam {
        .key = AssetKey {},
        .asset_type = AssetType::kInputAction,
      },
      strings),
    Record("target", oxygen::data::ScriptNodeParam {}, strings),
  };
  auto descriptor = scripting::ScriptAssetDesc {};
  descriptor.header.asset_type = static_cast<uint8_t>(AssetType::kScript);
  descriptor.header.version = version;
  descriptor.params = {
    .offset = sizeof(descriptor),
    .count = static_cast<uint32_t>(declarations.size()),
    .entry_size = sizeof(scripting::ScriptParamRecord),
  };
  auto bytes = std::vector<std::byte> {};
  const auto header = std::as_bytes(std::span(&descriptor, 1));
  bytes.insert(bytes.end(), header.begin(), header.end());
  const auto records = std::as_bytes(std::span(declarations));
  bytes.insert(bytes.end(), records.begin(), records.end());
  oxygen::data::AppendDescriptorStrings(bytes, strings);
  auto references = oxygen::data::AssetReferences::Create({}, {});
  return LooseAsset {
    .type = AssetType::kScript,
    .leaf = kScriptLeaf,
    .descriptor = std::move(bytes),
    .references = std::move(references).value(),
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

  auto Report(std::string virtual_path = {}) -> int
  {
    return RunScriptMetadataReport({
      .cooked_root = TempDir().string(),
      .output = TempPath("scripts.json").string(),
      .virtual_path = std::move(virtual_path),
    });
  }

  auto ReadReport() -> nlohmann::json
  {
    std::ifstream input(TempPath("scripts.json"));
    auto report = nlohmann::json::parse(input);
    static const auto schema = oxygen::cooker::test::LoadSchema(
      "Tools/Inspector/Schemas/oxygen.cooked-scripts.schema.json");
    EXPECT_THAT(
      oxygen::cooker::test::ValidateJson(schema, report), ::testing::IsEmpty());
    return report;
  }
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

NOLINT_TEST_F(InspectorScriptMetadataTest, RejectsRetiredScriptVersion)
{
  WriteRoot(static_cast<uint8_t>(scripting::kScriptAssetVersion - 1U));
  EXPECT_EQ(Report(), 2);
  EXPECT_FALSE(std::filesystem::exists(TempPath("scripts.json")));
}

} // namespace
