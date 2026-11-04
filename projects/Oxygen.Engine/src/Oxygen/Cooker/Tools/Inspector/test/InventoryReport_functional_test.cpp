//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/Inspector/InventoryReport.cpp

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Tools/Common/ToolApp.h>
#include <Oxygen/Cooker/Tools/Inspector/InventoryReport.h>
#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {

namespace inspection = oxygen::content::inspection;

class InspectorInventoryReportTest : public oxygen::testing::TempDirTest {
protected:
  //! A root holding one material, in `TempDir()`.
  auto WriteRoot() -> void
  {
    auto library = oxygen::cooker::testing::LooseCookedRoot { "Content" };
    static_cast<void>(library.Material(
      "Shared.omat", oxygen::cooker::testing::MaterialBuilder { "Shared" }));
    oxygen::cooker::testing::Materialize(library.Files(), TempDir());
  }

  auto Report() -> int
  {
    return oxygen::content::toolkit::ExitCodeOf(std::cerr, [&] {
      return inspection::RunInventoryReport({
        .cooked_root = TempDir().string(),
        .output = ReportPath().string(),
      });
    });
  }

  //! The written report, checked against the schema the SDK installs.
  auto ReadReport() -> nlohmann::json
  {
    auto input = std::ifstream(ReportPath());
    auto report = nlohmann::json::parse(input);
    static const auto schema = oxygen::cooker::test::LoadSchema(
      "Tools/Inspector/Schemas/oxygen.cooked-inventory.schema.json");
    EXPECT_THAT(
      oxygen::cooker::test::ValidateJson(schema, report), ::testing::IsEmpty());
    EXPECT_EQ(
      report.at("schema_version"), inspection::kInventoryReportSchemaVersion);
    return report;
  }

private:
  [[nodiscard]] auto ReportPath() const -> std::filesystem::path
  {
    return reports_.Path() / "inventory.json";
  }

  oxygen::testing::ScopedTempDir reports_;
};

//! A sound root reports its files and assets, and no issue.
NOLINT_TEST_F(InspectorInventoryReportTest, ReportsASoundRoot)
{
  WriteRoot();

  ASSERT_EQ(Report(), 0);

  const auto report = ReadReport();
  EXPECT_THAT(report.at("issues"), ::testing::IsEmpty());
  EXPECT_THAT(report.at("files"), ::testing::Not(::testing::IsEmpty()));
  EXPECT_EQ(report.at("assets").size(), 1U);
}

//! A damaged file is an issue of the report, which keeps its contract.
NOLINT_TEST_F(InspectorInventoryReportTest, ReportsADamagedFileAsAnIssue)
{
  WriteRoot();
  ASSERT_EQ(Report(), 0);
  const auto relative_path = ReadReport()
                               .at("assets")
                               .at(0)
                               .at("descriptor_relative_path")
                               .get<std::string>();
  std::filesystem::resize_file(TempDir() / relative_path, 1U);

  ASSERT_EQ(Report(), 0);

  const auto issues = ReadReport().at("issues");
  ASSERT_EQ(issues.size(), 1U);
  EXPECT_EQ(issues.at(0).at("relative_path"), relative_path);
  EXPECT_EQ(issues.at(0).at("reason"), "size_mismatch");
}

//! What a writer has not committed is an issue of a root to publish, though a
//! mount accepts it.
NOLINT_TEST_F(InspectorInventoryReportTest, ReportsAWritersFileAsAnIssue)
{
  WriteRoot();
  const auto relative_path
    = std::string { "Materials/Wood.0123456789abcdef.omat" };
  std::filesystem::create_directories(TempDir() / "Materials");
  std::ofstream(TempDir() / relative_path) << "a writer's";

  ASSERT_EQ(Report(), 0);

  const auto issues = ReadReport().at("issues");
  ASSERT_EQ(issues.size(), 1U);
  EXPECT_EQ(issues.at(0).at("relative_path"), relative_path);
  EXPECT_EQ(issues.at(0).at("reason"), "uncommitted");
}

} // namespace
