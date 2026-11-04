//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/ImportManifest.cpp

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace {

using oxygen::content::import::ImportDiagnostic;
using oxygen::cooker::test::DiagnosticSummary;
using oxygen::cooker::test::HasDiagnostic;

using nlohmann::json;
using oxygen::content::import::ImportManifest;
using oxygen::cooker::test::WriteText;
using oxygen::testing::ScopedTempDir;

NOLINT_TEST(ImportManifestMaterialDescriptorTest,
  BuildsMaterialDescriptorRequestWithDefaultsAndDescriptorOverrides)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "builds_material_descriptor" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Materials" / "wood.material.json";
  WriteText(descriptor_path,
    R"({
      "name": "WoodFloor",
      "domain": "opaque",
      "textures": {
        "base_color": {
          "virtual_path": "/Cooked/Textures/WoodFloor_Color.otex"
        }
      }
    })");

  const auto manifest_json = oxygen::cooker::test::ManifestText(R"({
      "mounts": [{ "name": "Content", "path": "." }],
      "output": ".cooked",
      "defaults": {
        "material_descriptor": {
          "name": "default-material-name"
        }
      },
      "jobs": [
        {
          "type": "material-descriptor",
          "source": "Materials/wood.material.json",
          "virtual_path": "/Cooked/Materials/WoodFloor.omat",
          "name": "wood-job"
        }
      ]
    })");
  WriteText(manifest_path, manifest_json);

  auto errors = std::vector<ImportDiagnostic> {};
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  ASSERT_HAS_VALUE(manifest) << DiagnosticSummary(errors);
  ASSERT_EQ(manifest->jobs.size(), 1U);

  auto request_errors = std::vector<ImportDiagnostic> {};
  const auto request = manifest->jobs.at(0).BuildRequest(request_errors);
  ASSERT_HAS_VALUE(request) << DiagnosticSummary(request_errors);
  ASSERT_HAS_VALUE(request->cooked_root);
  EXPECT_EQ(request->source_path, descriptor_path.lexically_normal());
  EXPECT_EQ(request->job_name, std::optional<std::string> { "wood-job" });
  EXPECT_EQ(request->asset_virtual_path, "/Cooked/Materials/WoodFloor.omat");
  ASSERT_HAS_VALUE(request->material_descriptor);

  const auto normalized
    = json::parse(request->material_descriptor->descriptor_json);
  EXPECT_EQ(normalized.at("name").get<std::string>(), "WoodFloor");
}

NOLINT_TEST(ImportManifestMaterialDescriptorTest,
  CollectsAndPropagatesMaterialDescriptorDependencies)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "collects_material_dependencies" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  const auto material_descriptor_path
    = root / "Materials" / "wood.material.json";
  WriteText(material_descriptor_path,
    R"({
      "name": "WoodFloor",
      "domain": "opaque"
    })");

  WriteText(manifest_path, oxygen::cooker::test::ManifestText(R"({
      "mounts": [{ "name": "Content", "path": "." }],
      "output": "material-cooked",
      "jobs": [
        {
          "id": "wood.color",
          "type": "texture-descriptor",
          "source": "woodfloor007_color.texture.json",
          "virtual_path": "/Cooked/Textures/WoodFloor_Color.otex"
        },
        {
          "id": "wood.material",
          "type": "material-descriptor",
          "source": "Materials/wood.material.json",
          "virtual_path": "/Cooked/Materials/WoodFloor.omat",
          "depends_on": ["wood.color"]
        }
      ]
    })"));

  auto errors = std::vector<ImportDiagnostic> {};
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  ASSERT_HAS_VALUE(manifest) << DiagnosticSummary(errors);
  ASSERT_EQ(manifest->jobs.size(), 2U);
  EXPECT_EQ(manifest->jobs.at(1).id, "wood.material");
  ASSERT_EQ(manifest->jobs.at(1).depends_on.size(), 1U);
  EXPECT_EQ(manifest->jobs.at(1).depends_on.at(0), "wood.color");

  auto request_errors = std::vector<ImportDiagnostic> {};
  const auto request = manifest->jobs.at(1).BuildRequest(request_errors);
  ASSERT_HAS_VALUE(request) << DiagnosticSummary(request_errors);
  ASSERT_HAS_VALUE(request->orchestration);
  EXPECT_EQ(request->orchestration->job_id, "wood.material");
  ASSERT_EQ(request->orchestration->depends_on.size(), 1U);
  EXPECT_EQ(request->orchestration->depends_on.at(0), "wood.color");
}

NOLINT_TEST(ImportManifestMaterialDescriptorTest,
  RejectsMaterialDescriptorJobWithDisallowedKeys)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "rejects_disallowed_key" / "import_manifest.json";
  WriteText(manifest_path, oxygen::cooker::test::ManifestText(R"({
      "mounts": [{ "name": "Content", "path": "." }],
      "jobs": [
        {
          "type": "material-descriptor",
          "source": "Materials/wood.material.json",
          "intent": "albedo"
        }
      ]
    })"));

  auto errors = std::vector<ImportDiagnostic> {};
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  EXPECT_FALSE(manifest.has_value());
  EXPECT_THAT(errors,
    HasDiagnostic(
      oxygen::content::import::manifest_codes::kSchemaValidationFailed));
}

NOLINT_TEST(ImportManifestMaterialDescriptorTest,
  RejectsDescriptorPayloadThatViolatesDescriptorSchema)
{
  const ScopedTempDir temp;
  const auto manifest_path = temp.Path()
    / "rejects_bad_material_descriptor_payload" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Materials" / "bad.material.json";
  WriteText(descriptor_path,
    R"({
      "name": "Bad",
      "textures": {
        "base_color": {
          "virtual_path": "/Cooked/Textures/WoodFloor_Color.otex",
          "bad_key": 1
        }
      }
    })");

  WriteText(manifest_path, oxygen::cooker::test::ManifestText(R"({
      "mounts": [{ "name": "Content", "path": "." }],
      "output": "material-cooked",
      "jobs": [
        {
          "type": "material-descriptor",
          "source": "Materials/bad.material.json",
          "virtual_path": "/Cooked/Materials/Bad.omat"
        }
      ]
    })"));

  auto errors = std::vector<ImportDiagnostic> {};
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  ASSERT_HAS_VALUE(manifest) << DiagnosticSummary(errors);
  ASSERT_EQ(manifest->jobs.size(), 1U);

  auto request_errors = std::vector<ImportDiagnostic> {};
  const auto request = manifest->jobs.at(0).BuildRequest(request_errors);
  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(request_errors,
    HasDiagnostic(oxygen::content::import::import_codes::
        kMaterialDescriptorSchemaValidationFailed));
}

//! The cooked layout is fixed: a manifest that still sets `layout`, at the top,
//! in its defaults or on a job, is refused by the schema as an unknown field.
NOLINT_TEST(ImportManifestMaterialDescriptorTest, RejectsLayoutField)
{
  // Arrange
  const ScopedTempDir temp;
  const auto path = temp.Path() / "fixed_cooked_root" / "import_manifest.json";
  WriteText(
    path.parent_path() / "shared.material.json", R"({"name":"Shared"})");
  const auto layout = json { { "resources_dir", "Resources" } };
  const auto job = json {
    { "type", "material-descriptor" },
    { "source", "shared.material.json" },
    { "virtual_path", "/Content/Materials/Shared.omat" },
  };
  const auto base = json {
    { "schema_version", oxygen::content::import::kImportManifestSchemaVersion },
    {
      "mounts",
      json::array({ { { "name", "Content" }, { "path", "." } } }),
    },
    { "output", ".cooked" },
    { "jobs", json::array({ job }) },
  };
  auto top = base;
  top["layout"] = layout;
  auto defaults = base;
  defaults["defaults"] = json { { "layout", layout } };
  auto per_job = base;
  per_job["jobs"][0]["layout"] = layout;

  for (const auto& document : { top, defaults, per_job }) {
    WriteText(path, document.dump());
    std::vector<ImportDiagnostic> errors;

    // Act
    const auto manifest = ImportManifest::Load(path, {}, errors);

    // Assert
    EXPECT_FALSE(manifest.has_value()) << document.dump();
    EXPECT_THAT(errors,
      HasDiagnostic(
        oxygen::content::import::manifest_codes::kSchemaValidationFailed));
    EXPECT_THAT(DiagnosticSummary(errors), ::testing::HasSubstr("layout"));
  }
}

//! Material jobs read textures from the job's context roots, beneath which the
//! batch-wide roots, such as the engine content pak, are placed.
NOLINT_TEST(ImportManifestMaterialDescriptorTest, CarriesCookedContextRoots)
{
  // Arrange
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "material_context_roots" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  WriteText(root / "Materials" / "tiles.material.json",
    R"({"name": "Tiles", "domain": "opaque"})");
  WriteText(manifest_path, oxygen::cooker::test::ManifestText(R"({
      "mounts": [{ "name": "Content", "path": "." }],
      "output": "out",
      "jobs": [
        {
          "type": "material-descriptor",
          "source": "Materials/tiles.material.json",
          "virtual_path": "/Cooked/Materials/Tiles.omat",
          "cooked_context_roots": ["Libraries/Textures"]
        }
      ]
    })"));
  auto errors = std::vector<ImportDiagnostic> {};
  auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  ASSERT_HAS_VALUE(manifest) << DiagnosticSummary(errors);
  const auto engine = temp.Path() / "engine.pak";

  // Act
  manifest->AddBaseCookedContextRoots(std::vector { engine });
  const auto request = manifest->jobs.at(0).BuildRequest(errors);

  // Assert
  ASSERT_HAS_VALUE(request) << DiagnosticSummary(errors);
  EXPECT_EQ(request->cooked_context_roots,
    (std::vector<std::filesystem::path> {
      engine, root / "Libraries/Textures" }));
}

//! A single-asset job names the asset it cooks; without an identity the
//! manifest is refused.
NOLINT_TEST(
  ImportManifestMaterialDescriptorTest, RejectsSingleAssetJobWithoutVirtualPath)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "missing_virtual_path" / "import_manifest.json";
  WriteText(manifest_path, oxygen::cooker::test::ManifestText(R"({
      "mounts": [{ "name": "Content", "path": "." }],
      "jobs": [
        {
          "type": "material-descriptor",
          "source": "Materials/wood.material.json"
        }
      ]
    })"));

  auto errors = std::vector<ImportDiagnostic> {};
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  EXPECT_FALSE(manifest.has_value());
  EXPECT_THAT(errors,
    HasDiagnostic(
      oxygen::content::import::manifest_codes::kVirtualPathMissing));
}

//! A model job is a container: it names the model's identity, under which its
//! products are named, and is refused without one.
NOLINT_TEST(ImportManifestMaterialDescriptorTest, ModelJobRequiresItsIdentity)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "model_virtual_path" / "import_manifest.json";
  for (const auto* type : { "gltf", "fbx" }) {
    auto job = json {
      { "type", type },
      { "source", "Ship.src" },
    };
    WriteText(manifest_path,
      json {
        {
          "schema_version",
          oxygen::content::import::kImportManifestSchemaVersion,
        },
        {
          "mounts",
          json::array({ { { "name", "Content" }, { "path", "." } } }),
        },
        { "jobs", json::array({ job }) },
      }
        .dump());
    auto refused_errors = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(
      ImportManifest::Load(manifest_path, {}, refused_errors).has_value())
      << type;
    EXPECT_THAT(refused_errors,
      HasDiagnostic(
        oxygen::content::import::manifest_codes::kVirtualPathMissing))
      << type;

    job["virtual_path"] = "/Cooked/Vehicles/Ship.src";
    WriteText(manifest_path,
      json {
        {
          "schema_version",
          oxygen::content::import::kImportManifestSchemaVersion,
        },
        {
          "mounts",
          json::array({ { { "name", "Content" }, { "path", "." } } }),
        },
        { "jobs", json::array({ job }) },
      }
        .dump());
    auto errors = std::vector<ImportDiagnostic> {};
    const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
    ASSERT_HAS_VALUE(manifest) << type << DiagnosticSummary(errors);
    EXPECT_EQ(manifest->jobs.at(0).virtual_path, "/Cooked/Vehicles/Ship.src");
  }
}

} // namespace
