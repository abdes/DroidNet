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
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Import/EngineContent.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/StableId.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {

using oxygen::content::import::ImportDiagnostic;
using oxygen::cooker::test::DiagnosticSummary;
using oxygen::cooker::test::HasDiagnostic;

using nlohmann::json;
using oxygen::content::import::GetEngineShapes;
using oxygen::content::import::ImportManifest;
using oxygen::content::import::ImportManifestLoadOptions;
using oxygen::content::import::MountTableOwner;
using oxygen::content::import::WriteEngineContentSources;
using oxygen::cooker::test::LoadJson;
using oxygen::cooker::test::LoadSchema;
using oxygen::cooker::test::ValidateJson;
using oxygen::testing::TempDirTest;

class EngineContentTest : public TempDirTest { };

//! The geometry descriptor schema, loaded once.
auto GeometrySchema() -> const json&
{
  static const auto schema
    = LoadSchema("Import/Schemas/oxygen.geometry-descriptor.schema.json");
  return schema;
}

//! Loads the descriptor a job's mount path (`/Engine/<folder>/<file>`) names.
auto LoadSource(const std::filesystem::path& directory, const json& job) -> json
{
  constexpr auto kPrefix = std::string_view { "/Engine/" };
  const auto source = job.at("source").get<std::string>();
  EXPECT_THAT(source, testing::StartsWith(std::string(kPrefix)));
  return LoadJson(directory / source.substr(kPrefix.size()));
}

//! Checks one geometry job: its descriptor is schema-valid, procedural from the
//! shape's generator, and uses the engine default material.
auto CheckShapeJob(const std::filesystem::path& directory, const json& job)
  -> void
{
  const auto descriptor = LoadSource(directory, job);
  EXPECT_THAT(ValidateJson(GeometrySchema(), descriptor), testing::IsEmpty());
  const auto& lod = descriptor.at("lods").at(0);
  EXPECT_EQ(lod.at("procedural").at("generator"), job.at("name"));
  // The default material is in the geometry's mount, so it is named relative
  // to the geometry.
  const auto identity = job.at("virtual_path").get<std::string>();
  const auto material_ref
    = lod.at("submeshes").at(0).at("material_ref").get<std::string>();
  EXPECT_EQ(material_ref,
    oxygen::content::RelativeReference(
      identity, oxygen::data::MaterialAsset::kDefaultVirtualPath));
  EXPECT_EQ(oxygen::content::ResolveReference(identity, material_ref),
    std::string(oxygen::data::MaterialAsset::kDefaultVirtualPath));
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

  // Act
  const auto manifests = WriteEngineContentSources(TempDir());

  // Assert
  const auto manifest = LoadJson(manifests.materials);
  EXPECT_FALSE(manifest.contains("output"));
  EXPECT_EQ(manifest.at("mounts"),
    (json::array({ { { "name", "Engine" }, { "path", "." } } })));
  ASSERT_EQ(manifest.at("jobs").size(), 1U);
  const auto& job = manifest.at("jobs").at(0);
  EXPECT_EQ(job.at("type"), "material-descriptor");
  EXPECT_EQ(job.at("name"), "Default");
  EXPECT_EQ(job.at("virtual_path"), "/Engine/Generated/Materials/Default.omat");
  const auto descriptor = LoadSource(TempDir(), job);
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
  const auto manifests = WriteEngineContentSources(TempDir());

  // Assert
  const auto jobs = LoadJson(manifests.geometry).at("jobs");
  ASSERT_EQ(jobs.size(), GetEngineShapes().size());
  auto folders = std::vector<std::string> {};
  for (const auto& job : jobs) {
    TRACE_GCHECK_F(CheckShapeJob(TempDir(), job), "shape job");
    folders.push_back(
      std::filesystem::path(job.at("virtual_path").get<std::string>())
        .parent_path()
        .generic_string());
  }
  EXPECT_EQ(std::ranges::count(folders, "/Engine/Internal"), 1);
  EXPECT_EQ(std::ranges::count(folders, "/Engine/Generated/BasicShapes"),
    static_cast<std::ptrdiff_t>(GetEngineShapes().size() - 1));
}

//! The manifests declare the `Engine` mount, which only the engine content
//! build may do, and the directory's mounts file stops every file above it.
NOLINT_TEST_F(EngineContentTest, ManifestsLoadOnlyAsTheEngineContentBuild)
{
  // Arrange
  const auto manifests = WriteEngineContentSources(TempDir());
  const auto mounts_file = LoadJson(TempDir() / ".mounts.json");

  // Act
  auto engine_errors = std::vector<ImportDiagnostic> {};
  const auto engine = ImportManifest::Load(manifests.geometry,
    ImportManifestLoadOptions { .owner = MountTableOwner::kEngineContent },
    engine_errors);
  auto project_errors = std::vector<ImportDiagnostic> {};
  const auto project
    = ImportManifest::Load(manifests.geometry, {}, project_errors);

  // Assert
  EXPECT_EQ(mounts_file, (json { { "version", 1 }, { "root", true } }));
  EXPECT_TRUE(engine.has_value()) << DiagnosticSummary(engine_errors);
  EXPECT_FALSE(project.has_value());
  EXPECT_THAT(project_errors, HasDiagnostic("mount.name_invalid"));
}

} // namespace
