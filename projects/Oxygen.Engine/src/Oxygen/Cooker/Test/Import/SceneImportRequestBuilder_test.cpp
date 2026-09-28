//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/SceneImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/MaterialSlotProvenance.h>
#include <Oxygen/Cooker/Import/SceneImportSettings.h>
#include <Oxygen/Cooker/Test/Pak/PakTestSupport.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::content::import::ImportFormat;
using oxygen::content::import::ImportManifest;
using oxygen::content::import::SceneImportSettings;
using oxygen::content::import::internal::BuildSceneRequest;
constexpr std::string_view kSourceIdentity
  = "01990000-0000-7000-8000-000000000001";

auto RetainedProvenance() -> json
{
  return {
    { "schema_version", 1 },
    { "source_identity", kSourceIdentity },
    { "geometries", json::array() },
  };
}

class SceneImportRequestBuilderTest
  : public oxygen::content::pak::test::TempDirFixture {
protected:
  auto Settings() -> SceneImportSettings
  {
    auto settings = SceneImportSettings {};
    settings.source_path = Path("model.gltf").string();
    settings.cooked_root = Path("cooked").string();
    settings.material_slot_source_identity = kSourceIdentity;
    return settings;
  }
};

NOLINT_TEST_F(
  SceneImportRequestBuilderTest, RequiresRetainedModelSourceIdentity)
{
  auto settings = Settings();
  settings.material_slot_source_identity.clear();
  std::ostringstream errors;
  EXPECT_FALSE(
    BuildSceneRequest(settings, ImportFormat::kGltf, errors).has_value());
  EXPECT_THAT(
    errors.str(), testing::HasSubstr("requires retained material-slot"));
}

NOLINT_TEST_F(SceneImportRequestBuilderTest, ForwardsCanonicalProvenance)
{
  auto settings = Settings();
  settings.material_slot_provenance_json = RetainedProvenance().dump();
  std::ostringstream errors;
  const auto request = BuildSceneRequest(settings, ImportFormat::kGltf, errors);
  if (!request.has_value()) {
    FAIL() << "Expected request to contain a value" << errors.str();
  }
  EXPECT_EQ(request->material_slot_provenance->SourceIdentity().ToString(),
    kSourceIdentity);
  EXPECT_EQ(json::parse(request->material_slot_provenance->Serialize()),
    RetainedProvenance());
}

NOLINT_TEST_F(SceneImportRequestBuilderTest, ReadsExplicitProvenanceFile)
{
  auto settings = Settings();
  settings.material_slot_provenance_path = Path("slots.json").string();
  {
    std::ofstream output(settings.material_slot_provenance_path);
    output << RetainedProvenance().dump();
    ASSERT_TRUE(output.good());
  }
  std::ostringstream errors;
  const auto request = BuildSceneRequest(settings, ImportFormat::kGltf, errors);
  if (!request.has_value()) {
    FAIL() << "Expected request to contain a value" << errors.str();
  }
  EXPECT_EQ(json::parse(request->material_slot_provenance->Serialize()),
    RetainedProvenance());
}

NOLINT_TEST_F(SceneImportRequestBuilderTest, RejectsCompetingProvenanceInputs)
{
  auto settings = Settings();
  settings.material_slot_provenance_json = RetainedProvenance().dump();
  settings.material_slot_provenance_path = Path("slots.json").string();
  std::ostringstream errors;
  EXPECT_FALSE(
    BuildSceneRequest(settings, ImportFormat::kGltf, errors).has_value());
  EXPECT_THAT(errors.str(), testing::HasSubstr("not both"));
}

NOLINT_TEST_F(SceneImportRequestBuilderTest, RejectsProvenanceFromAnotherSource)
{
  auto settings = Settings();
  auto provenance = RetainedProvenance();
  provenance.update(
    { { "source_identity", "01990000-0000-7000-8000-000000000002" } });
  settings.material_slot_provenance_json = provenance.dump();
  std::ostringstream errors;
  EXPECT_FALSE(
    BuildSceneRequest(settings, ImportFormat::kGltf, errors).has_value());
  EXPECT_THAT(errors.str(), testing::HasSubstr("another source identity"));
}

NOLINT_TEST_F(SceneImportRequestBuilderTest, RejectsMalformedProvenanceShape)
{
  auto settings = Settings();
  auto provenance = RetainedProvenance();
  provenance.update({ { "geometries", { { "unexpected", true } } } });
  settings.material_slot_provenance_json = provenance.dump();
  std::ostringstream errors;
  EXPECT_FALSE(
    BuildSceneRequest(settings, ImportFormat::kGltf, errors).has_value());
  EXPECT_THAT(
    errors.str(), testing::HasSubstr("invalid material-slot provenance"));
}

NOLINT_TEST_F(SceneImportRequestBuilderTest,
  ManifestForwardsRetainedProvenanceToBothFormats)
{
  const auto manifest_path = Path("import-manifest.json");
  {
    const auto document = json {
      { "version", 1 },
      { "output", Path("cooked").string() },
      {
        "defaults",
        {
          {
            "scene",
            {
              { "material_slot_source_identity", kSourceIdentity },
              { "material_slot_provenance", RetainedProvenance() },
            },
          },
        },
      },
      {
        "jobs",
        json::array({
          { { "type", "gltf" }, { "source", "model.gltf" } },
          { { "type", "fbx" }, { "source", "model.fbx" } },
        }),
      },
    };
    std::ofstream output(manifest_path);
    output << document.dump();
    ASSERT_TRUE(output.good());
  }
  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  if (!manifest.has_value()) {
    FAIL() << "Expected manifest to contain a value" << errors.str();
  }
  ASSERT_EQ(manifest->jobs.size(), 2U);
  for (const auto& job : manifest->jobs) {
    const auto request = job.BuildRequest(errors);
    if (!request.has_value()) {
      FAIL() << "Expected request to contain a value" << errors.str();
    }
    EXPECT_EQ(request->material_slot_provenance->SourceIdentity().ToString(),
      kSourceIdentity);
    EXPECT_EQ(json::parse(request->material_slot_provenance->Serialize()),
      RetainedProvenance());
  }
}

NOLINT_TEST_F(
  SceneImportRequestBuilderTest, ContentFlagsDoNotWaiveModelIdentity)
{
  auto settings = Settings();
  settings.import_geometry = false;
  settings.material_slot_source_identity.clear();
  std::ostringstream errors;
  EXPECT_FALSE(BuildSceneRequest(settings, ImportFormat::kGltf, errors));
  EXPECT_THAT(
    errors.str(), testing::HasSubstr("model import requires retained"));
}

} // namespace
