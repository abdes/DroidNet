//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/EngineContent.cpp

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/EngineContent.h>
#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Cooker/test/Support/TempDir.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/StableId.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::content::import::GetEngineShapes;
using oxygen::content::import::WriteEngineContentSources;
using oxygen::cooker::test::LoadJson;
using oxygen::cooker::test::LoadSchema;
using oxygen::cooker::test::TempDirTest;
using oxygen::cooker::test::ValidateJson;

class EngineContentTest : public TempDirTest { };

//! The geometry descriptor schema, loaded once.
auto GeometrySchema() -> const json&
{
  static const auto schema
    = LoadSchema("Import/Schemas/oxygen.geometry-descriptor.schema.json");
  return schema;
}

//! Checks one geometry job: its descriptor is schema-valid, procedural from the
//! shape's generator, and uses the engine default material.
auto CheckShapeJob(const json& job) -> void
{
  const auto descriptor = LoadJson(job.at("source").get<std::string>());
  EXPECT_THAT(ValidateJson(GeometrySchema(), descriptor), testing::IsEmpty());
  const auto& lod = descriptor.at("lods").at(0);
  EXPECT_EQ(lod.at("procedural").at("generator"), job.at("name"));
  EXPECT_EQ(lod.at("submeshes").at(0).at("material_ref").get<std::string>(),
    oxygen::data::MaterialAsset::kDefaultVirtualPath);
  EXPECT_EQ(job.at("layout"), (json { { "virtual_mount_root", "/Engine" } }));
  EXPECT_THAT(job.at("virtual_path").get<std::string>(),
    testing::AllOf(testing::StartsWith("/Engine/"),
      testing::EndsWith("/" + job.at("name").get<std::string>() + ".ogeo")));
  // The generator bounds the mesh when it is cooked.
  EXPECT_FALSE(descriptor.contains("bounds"));
  EXPECT_FALSE(lod.contains("bounds"));
  EXPECT_EQ(lod.at("submeshes").at(0).at("surface_id").get<std::string>(),
    to_string(oxygen::data::ProceduralSurfaceId()));
}

//! The default material descriptor is schema-valid and carries the live
//! default material's values under its engine path.
NOLINT_TEST_F(EngineContentTest, DefaultMaterialMatchesTheLiveMaterial)
{
  // Arrange
  const auto cooked_root = TempPath("cooked");

  // Act
  const auto manifests = WriteEngineContentSources(TempDir(), cooked_root);

  // Assert
  const auto manifest = LoadJson(manifests.materials);
  EXPECT_EQ(manifest.at("output"), cooked_root.generic_string());
  ASSERT_EQ(manifest.at("jobs").size(), 1U);
  const auto& job = manifest.at("jobs").at(0);
  EXPECT_EQ(job.at("type"), "material-descriptor");
  EXPECT_EQ(job.at("name"), "Default");
  EXPECT_EQ(job.at("virtual_path"), "/Engine/Generated/Materials/Default.omat");
  EXPECT_EQ(job.at("layout"), (json { { "virtual_mount_root", "/Engine" } }));
  const auto descriptor = LoadJson(job.at("source").get<std::string>());
  EXPECT_THAT(
    ValidateJson(
      LoadSchema("Import/Schemas/oxygen.material-descriptor.schema.json"),
      descriptor),
    testing::IsEmpty());
  const auto live = oxygen::data::MaterialAsset::CreateDefault();
  const auto& parameters = descriptor.at("parameters");
  EXPECT_FLOAT_EQ(
    parameters.at("roughness").get<float>(), live->GetRoughness());
  EXPECT_FLOAT_EQ(
    parameters.at("metalness").get<float>(), live->GetMetalness());
  EXPECT_EQ(parameters.at("double_sided").get<bool>(), live->IsDoubleSided());
}

//! Every engine shape gets a schema-valid procedural descriptor in its folder,
//! referencing the default material by its engine path.
NOLINT_TEST_F(EngineContentTest, EveryShapeCooksUnderItsEnginePath)
{
  // Act
  const auto manifests
    = WriteEngineContentSources(TempDir(), TempPath("cooked"));

  // Assert
  const auto jobs = LoadJson(manifests.geometry).at("jobs");
  ASSERT_EQ(jobs.size(), GetEngineShapes().size());
  auto folders = std::vector<std::string> {};
  for (const auto& job : jobs) {
    TRACE_GCHECK_F(CheckShapeJob(job), "shape job");
    folders.push_back(
      std::filesystem::path(job.at("virtual_path").get<std::string>())
        .parent_path()
        .generic_string());
  }
  EXPECT_EQ(std::ranges::count(folders, "/Engine/Internal"), 1);
  EXPECT_EQ(std::ranges::count(folders, "/Engine/Generated/BasicShapes"),
    static_cast<std::ptrdiff_t>(GetEngineShapes().size() - 1));
}

} // namespace
