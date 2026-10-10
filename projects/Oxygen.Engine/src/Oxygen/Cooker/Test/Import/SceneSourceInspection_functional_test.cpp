//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/SceneSourceInspection.cpp

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stop_token>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/SceneSourceInspection.h>
#include <Oxygen/Cooker/Test/Support/JsonSchema.h>
#include <Oxygen/Cooker/Test/Support/ModelImportTestBase.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::content::import::ExportSceneSourceInspection;
using oxygen::content::import::InspectSceneSource;
using oxygen::content::import::SceneSourceInspection;

class SceneSourceInspectionTest
  : public oxygen::content::import::test::ModelImportTestBase {
protected:
  static auto ValidateReport(const SceneSourceInspection& report) -> void
  {
    static const auto schema = oxygen::cooker::test::LoadSchema(
      "Import/Schemas/oxygen.scene-source-inspection.schema.json");
    EXPECT_THAT(oxygen::cooker::test::ValidateJson(
                  schema, json::parse(ExportSceneSourceInspection(report))),
      ::testing::IsEmpty());
  }

  auto WriteExternalGltf(
    const std::filesystem::path& path, const std::string& uri) -> void
  {
    std::ifstream source(
      TestModelsDirFromFile() / "static_scalar_triangle.gltf");
    auto document = json::parse(source);
    document.at("buffers").at(0).at("uri") = uri;
    std::ofstream output(path);
    output << document;
  }

  auto ReadFbxFixture() -> std::string
  {
    std::ifstream source(
      TestModelsDirFromFile() / "static_scalar_triangle.fbx");
    std::ostringstream contents;
    contents << source.rdbuf();
    return contents.str();
  }
};

NOLINT_TEST_F(SceneSourceInspectionTest, StaticTexturesReportExternalImages)
{
  for (const auto* extension : { "gltf", "fbx" }) {
    SCOPED_TRACE(extension);
    const auto report = InspectSceneSource(TestModelsDirFromFile()
      / (std::string("static_textured_triangle.") + extension));
    EXPECT_TRUE(report.parsed);
    EXPECT_TRUE(report.supported) << ExportSceneSourceInspection(report);
    ASSERT_EQ(report.external_files.size(), 1U);
    EXPECT_EQ(report.external_files.front(), "static_textured_checker.png");
    ValidateReport(report);
  }
}

NOLINT_TEST_F(
  SceneSourceInspectionTest, DiscoversMissingExternalBufferWithoutCooking)
{
  const auto root = MakeTempDir("source_inspection_external_buffer");
  const auto source = root / "model.gltf";
  WriteExternalGltf(source, "../buffers/triangle%20data.bin");

  const auto result = InspectSceneSource(source);

  EXPECT_TRUE(result.parsed);
  EXPECT_TRUE(result.supported);
  EXPECT_EQ(result.external_files,
    std::vector<std::string> { "../buffers/triangle data.bin" });
  EXPECT_EQ(result.mesh_count, 1U);
  EXPECT_EQ(result.material_count, 1U);
  EXPECT_DOUBLE_EQ(result.source_unit_meters.value_or(0), 1.0);
  EXPECT_EQ(result.source_up, "+Y");
  EXPECT_EQ(result.source_left_handed, std::optional(false));
  EXPECT_FALSE(result.reverses_winding);
  EXPECT_FALSE(std::filesystem::exists(root / "container.index.bin"));
  EXPECT_EQ(std::distance(std::filesystem::directory_iterator(root),
              std::filesystem::directory_iterator()),
    1);
  ValidateReport(result);
}

NOLINT_TEST_F(
  SceneSourceInspectionTest, EmbeddedGltfAndGlbHaveNoExternalDependencies)
{
  for (const auto* filename :
    { "static_scalar_triangle.gltf", "Tabuleiro.glb" }) {
    SCOPED_TRACE(filename);
    const auto source = TestModelsDirFromFile() / filename;
    ASSERT_TRUE(std::filesystem::exists(source));
    const auto result = InspectSceneSource(source);
    EXPECT_TRUE(result.parsed);
    EXPECT_EQ(result.format, "gltf");
    EXPECT_TRUE(result.external_files.empty());
    ValidateReport(result);
  }
}

NOLINT_TEST_F(
  SceneSourceInspectionTest, RejectsNonlocalAndNullContainingBufferUris)
{
  const auto root = MakeTempDir("source_inspection_bad_buffer");
  const auto source = root / "model.gltf";
  for (const auto* uri :
    { "https://example.invalid/data.bin", "/absolute.bin", "a%00b.bin" }) {
    SCOPED_TRACE(uri);
    WriteExternalGltf(source, uri);
    const auto result = InspectSceneSource(source);
    EXPECT_TRUE(result.parsed);
    EXPECT_FALSE(result.supported);
    EXPECT_TRUE(result.external_files.empty());
    ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_EQ(
      result.diagnostics.back().code, "import.source.nonlocal_dependency");
    ValidateReport(result);
  }
}

NOLINT_TEST_F(SceneSourceInspectionTest, ReportsFbxUnitsAxesAndAppliedWinding)
{
  const auto root = MakeTempDir("source_inspection_fbx_coordinates");
  const auto source = root / "model.fbx";
  auto text = ReadFbxFixture();
  const auto units
    = text.find(R"("UnitScaleFactor", "double", "Number", "",100)");
  ASSERT_NE(units, std::string::npos);
  text.replace(units,
    std::string(R"("UnitScaleFactor", "double", "Number", "",100)").size(),
    R"("UnitScaleFactor", "double", "Number", "",1)");
  const auto front = text.find(R"("FrontAxisSign", "int", "Integer", "",1)");
  ASSERT_NE(front, std::string::npos);
  text.replace(front,
    std::string(R"("FrontAxisSign", "int", "Integer", "",1)").size(),
    R"("FrontAxisSign", "int", "Integer", "",-1)");
  {
    std::ofstream output(source);
    output << text;
  }

  const auto result = InspectSceneSource(source);
  EXPECT_TRUE(result.parsed);
  EXPECT_TRUE(result.supported);
  EXPECT_TRUE(result.external_files.empty());
  EXPECT_FLOAT_EQ(
    static_cast<float>(result.source_unit_meters.value_or(0)), 0.01F);
  EXPECT_EQ(result.source_right, "+X");
  EXPECT_EQ(result.source_up, "+Y");
  EXPECT_EQ(result.source_front, "-Z");
  EXPECT_EQ(result.source_left_handed, std::optional(true));
  EXPECT_TRUE(result.reverses_winding);
  ValidateReport(result);
}

NOLINT_TEST_F(
  SceneSourceInspectionTest, UnsupportedContentRetainsItsMetadataAndIssues)
{
  for (const auto* filename :
    { "light_overrides.gltf", "light_overrides.fbx" }) {
    SCOPED_TRACE(filename);
    const auto result = InspectSceneSource(TestModelsDirFromFile() / filename);
    EXPECT_TRUE(result.parsed);
    EXPECT_FALSE(result.supported);
    EXPECT_GT(result.node_count, 0U);
    EXPECT_FALSE(result.diagnostics.empty());
    ValidateReport(result);
  }
}

NOLINT_TEST_F(SceneSourceInspectionTest,
  CancelledAndUnrecognizedRequestsHaveValidFailureReports)
{
  std::stop_source cancellation;
  cancellation.request_stop();
  const auto cancelled = InspectSceneSource(
    TestModelsDirFromFile() / "static_scalar_triangle.gltf",
    cancellation.get_token());
  EXPECT_FALSE(cancelled.parsed);
  EXPECT_FALSE(cancelled.supported);
  ASSERT_FALSE(cancelled.diagnostics.empty());
  EXPECT_EQ(cancelled.diagnostics.front().code, "import.canceled");
  ValidateReport(cancelled);
  const auto unknown = InspectSceneSource("unknown.obj");
  EXPECT_FALSE(unknown.parsed);
  EXPECT_FALSE(unknown.supported);
  EXPECT_FALSE(unknown.diagnostics.empty());
  ValidateReport(unknown);
}

} // namespace
