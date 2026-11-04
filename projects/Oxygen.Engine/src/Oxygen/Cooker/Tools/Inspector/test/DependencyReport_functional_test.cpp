//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/Inspector/DependencyReport.cpp

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Tools/Common/ToolApp.h>
#include <Oxygen/Cooker/Tools/Inspector/DependencyReport.h>
#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Cooker/testing/GeometryBuilder.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/PakWorkspace.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {

namespace data = oxygen::data;
using oxygen::content::inspection::DependencyReportOptions;

//! `RunDependencyReport` as the tool runs it: the error, if any, goes to
//! stderr, and the result is the process exit code.
auto RunDependencyReport(const DependencyReportOptions& options) -> int
{
  return oxygen::content::toolkit::ExitCodeOf(std::cerr,
    [&] { return oxygen::content::inspection::RunDependencyReport(options); });
}

class InspectorDependencyReportTest : public oxygen::testing::TempDirTest {
protected:
  //! A library whose geometry is painted with its shared material.
  [[nodiscard]] static auto MakeLibrary()
    -> oxygen::cooker::testing::LooseCookedRoot
  {
    auto library = oxygen::cooker::testing::LooseCookedRoot { "Content" };
    const auto shared = library.Material(
      "Shared.omat", oxygen::cooker::testing::MaterialBuilder { "Shared" });
    auto crate = oxygen::cooker::testing::GeometryBuilder { "Crate" };
    crate.Submesh(crate.ProceduralLod(),
      data::SurfaceId::FromStableIdentity("crate"), shared);
    library.Geometry("Crate.ogeo", crate);
    return library;
  }

  auto Report(const std::filesystem::path& source) -> nlohmann::json
  {
    const auto output = TempPath("dependencies.json");
    EXPECT_EQ(RunDependencyReport({
                .cooked_root = source.string(),
                .output = output.string(),
              }),
      0);
    auto stream = std::ifstream(output);
    auto report = nlohmann::json::parse(stream);
    static const auto schema = oxygen::cooker::test::LoadSchema(
      "Import/Schemas/oxygen.cooked-dependencies.schema.json");
    EXPECT_THAT(
      oxygen::cooker::test::ValidateJson(schema, report), ::testing::IsEmpty());
    EXPECT_EQ(report.at("schema_version"),
      oxygen::content::inspection::kDependencyReportSchemaVersion);
    return report;
  }
};

//! A report over a cooked file would break the root it describes.
NOLINT_TEST_F(InspectorDependencyReportTest, RefusesAReportInsideItsCookedRoot)
{
  const auto library = MakeLibrary();
  const auto loose = TempPath("loose");
  oxygen::cooker::testing::Materialize(library.Files(), loose);
  const auto output = loose / "dependencies.json";

  EXPECT_EQ(RunDependencyReport({
              .cooked_root = loose.string(),
              .output = output.string(),
            }),
    1);

  EXPECT_FALSE(std::filesystem::exists(output));
}

//! A pak reports the same direct dependencies as the loose root it packs.
NOLINT_TEST_F(InspectorDependencyReportTest, ReportsDependenciesFromAPak)
{
  // Arrange
  const auto library = MakeLibrary();
  const auto loose = TempPath("loose");
  oxygen::cooker::testing::Materialize(library.Files(), loose);
  auto workspace = oxygen::cooker::testing::PakWorkspace {};
  workspace.PackOrThrow(workspace.FullRequest({ workspace.Add(library) }));
  const auto pak_path = TempPath("content.pak");
  oxygen::cooker::testing::Materialize(
    workspace.Storage().Bytes(
      oxygen::cooker::testing::PakWorkspace::PakPath("out.pak")),
    pak_path);
  const auto expected = Report(loose);

  // Act
  const auto actual = Report(pak_path);

  // Assert
  ASSERT_EQ(actual.at("assets").size(), expected.at("assets").size());
  for (const auto& row : expected.at("assets")) {
    const auto match = std::ranges::find_if(
      actual.at("assets"), [&row](const nlohmann::json& candidate) -> bool {
        return candidate.at("asset_key") == row.at("asset_key");
      });
    ASSERT_NE(match, actual.at("assets").end());
    EXPECT_EQ(match->at("virtual_path"), row.at("virtual_path"));
    EXPECT_EQ(match->at("dependencies"), row.at("dependencies"));
    EXPECT_EQ(match->at("key_references"), row.at("key_references"));
  }
}

} // namespace
