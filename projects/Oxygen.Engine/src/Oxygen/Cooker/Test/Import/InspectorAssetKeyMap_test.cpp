//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Cooker/Test/Pak/PakTestSupport.h>
#include <Oxygen/Cooker/Tools/Inspector/AssetKeyMap.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::content::inspection::RunAssetKeyMap;

class InspectorAssetKeyMapTest
  : public oxygen::content::pak::test::TempDirFixture {
protected:
  auto Run(const json& request) -> int
  {
    {
      std::ofstream input(Path("request.json"));
      input << request.dump();
    }
    return RunAssetKeyMap(
      { Path("request.json").string(), Path("report.json").string() });
  }

  auto Read() -> json
  {
    std::ifstream report(Path("report.json"));
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
  ASSERT_EQ(Run({ { "schema", "oxygen.asset-key-request.v1" },
              { "virtual_paths", paths } }),
    0);
  const auto report = Read();
  const auto schema_path = std::filesystem::path(__FILE__).parent_path()
    / "../../Import/Schemas/oxygen.asset-key-map.schema.json";
  std::ifstream schema(schema_path);
  nlohmann::json_schema::json_validator validator;
  validator.set_root_schema(json::parse(schema));
  EXPECT_NO_THROW(validator.validate(report));
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
  const auto valid = json { { "schema", "oxygen.asset-key-request.v1" },
    { "virtual_paths", { "/Content/Materials/A.omat" } } };
  ASSERT_EQ(Run(valid), 0);
  const auto original = Read();
  for (const auto& path :
    { "relative/A.omat", "/Content/../A.omat", "/Content/./A.omat",
      "/Content//A.omat", "/Content\\A.omat", "/Content/..", "/Content/." }) {
    SCOPED_TRACE(path);
    auto invalid = valid;
    invalid["virtual_paths"] = json::array({ path });
    EXPECT_EQ(Run(invalid), 2);
    EXPECT_EQ(Read(), original);
  }
  auto duplicate = valid;
  duplicate["virtual_paths"].push_back("/Content/Materials/A.omat");
  EXPECT_EQ(Run(duplicate), 2);
  auto unknown = valid;
  unknown["schema"] = "oxygen.asset-key-request.v2";
  EXPECT_EQ(Run(unknown), 2);
  EXPECT_EQ(Read(), original);
}

NOLINT_TEST_F(InspectorAssetKeyMapTest, EmptyBatchIsValid)
{
  ASSERT_EQ(Run({ { "schema", "oxygen.asset-key-request.v1" },
              { "virtual_paths", json::array() } }),
    0);
  EXPECT_TRUE(Read().at("assets").empty());
}

NOLINT_TEST_F(InspectorAssetKeyMapTest, MissingInputFailsWithoutCreatingReport)
{
  EXPECT_EQ(RunAssetKeyMap(
              { Path("missing.json").string(), Path("report.json").string() }),
    2);
  EXPECT_FALSE(std::filesystem::exists(Path("report.json")));
}

} // namespace
