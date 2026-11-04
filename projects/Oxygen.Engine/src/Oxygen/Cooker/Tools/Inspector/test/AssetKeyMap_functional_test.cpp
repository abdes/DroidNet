//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/Inspector/AssetKeyMap.cpp

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Cooker/Tools/Common/ToolApp.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/Tools/Inspector/AssetKeyMap.h>
#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {

using nlohmann::json;
using oxygen::content::inspection::AssetKeyMapOptions;

constexpr int kRefused
  = static_cast<int>(oxygen::content::toolkit::ExitCode::kUsage);

//! `RunAssetKeyMap` as the tool runs it: the error, if any, goes to stderr, and
//! the result is the process exit code.
auto RunAssetKeyMap(const AssetKeyMapOptions& options) -> int
{
  return oxygen::content::toolkit::ExitCodeOf(std::cerr,
    [&] { return oxygen::content::inspection::RunAssetKeyMap(options); });
}

class InspectorAssetKeyMapTest : public oxygen::testing::TempDirTest {
protected:
  auto Run(const json& request) -> int
  {
    {
      std::ofstream input(TempPath("request.json"));
      input << request.dump();
    }
    return RunAssetKeyMap({
      .input = TempPath("request.json").string(),
      .output = TempPath("report.json").string(),
    });
  }

  auto Read() -> json
  {
    std::ifstream report(TempPath("report.json"));
    return json::parse(report);
  }
};

NOLINT_TEST_F(
  InspectorAssetKeyMapTest, BatchMatchesNativeIdentityAndReportSchema)
{
  auto paths = json::array();
  for (auto index = 0; index < 1000; ++index) {
    paths.push_back("/Content/Materials/M" + std::to_string(index) + ".omat");
  }
  paths.push_back("/Game/Physics/Materials/Rubber.opmat");
  ASSERT_EQ(
    Run({ { "schema_version",
            oxygen::content::inspection::kAssetKeyRequestSchemaVersion },
      { "virtual_paths", paths } }),
    0);
  const auto report = Read();
  EXPECT_THAT(oxygen::cooker::test::ValidateJson(
                oxygen::cooker::test::LoadSchema(
                  "Import/Schemas/oxygen.asset-key-map.schema.json"),
                report),
    ::testing::IsEmpty());
  EXPECT_EQ(report.at("schema_version"),
    oxygen::content::inspection::kAssetKeyMapSchemaVersion);
  ASSERT_EQ(report.at("assets").size(), paths.size());
  for (size_t index = 0; index < paths.size(); ++index) {
    const auto path = paths.at(index).get<std::string>();
    EXPECT_EQ(report.at("assets").at(index).at("virtual_path"), path);
    EXPECT_EQ(report.at("assets").at(index).at("asset_key"),
      nostd::to_string(oxygen::data::AssetKey::FromVirtualPath(path)));
  }
  EXPECT_EQ(report.at("assets").back().at("asset_key"),
    "5793612a-1c25-ca81-a7a2-8e696378559e");
}

NOLINT_TEST_F(InspectorAssetKeyMapTest, InvalidRequestsPreserveExistingReport)
{
  const auto valid = json {
    {
      "schema_version",
      oxygen::content::inspection::kAssetKeyRequestSchemaVersion,
    },
    { "virtual_paths", { "/Content/Materials/A.omat" } },
  };
  ASSERT_EQ(Run(valid), 0);
  const auto original = Read();
  for (const auto& path : {
         "relative/A.omat",
         "/Content/../A.omat",
         "/Content/./A.omat",
         "/Content//A.omat",
         "/Content\\A.omat",
         "/Content/..",
         "/Content/.",
       }) {
    SCOPED_TRACE(path);
    auto invalid = valid;
    invalid["virtual_paths"] = json::array({ path });
    EXPECT_EQ(Run(invalid), kRefused);
    EXPECT_EQ(Read(), original);
  }
  auto duplicate = valid;
  duplicate["virtual_paths"].push_back("/Content/Materials/A.omat");
  EXPECT_EQ(Run(duplicate), kRefused);
  auto unknown = valid;
  unknown["schema_version"]
    = oxygen::content::inspection::kAssetKeyRequestSchemaVersion + 1;
  EXPECT_EQ(Run(unknown), kRefused);
  EXPECT_EQ(Read(), original);
}

NOLINT_TEST_F(InspectorAssetKeyMapTest, EmptyBatchIsValid)
{
  ASSERT_EQ(
    Run({ { "schema_version",
            oxygen::content::inspection::kAssetKeyRequestSchemaVersion },
      { "virtual_paths", json::array() } }),
    0);
  EXPECT_TRUE(Read().at("assets").empty());
}

NOLINT_TEST_F(InspectorAssetKeyMapTest, MissingInputFailsWithoutCreatingReport)
{
  EXPECT_EQ(RunAssetKeyMap({ TempPath("missing.json").string(),
              TempPath("report.json").string() }),
    kRefused);
  EXPECT_FALSE(std::filesystem::exists(TempPath("report.json")));
}

} // namespace
