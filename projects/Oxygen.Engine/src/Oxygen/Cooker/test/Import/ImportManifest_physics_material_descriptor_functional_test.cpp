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

NOLINT_TEST(ImportManifestPhysicsMaterialDescriptorTest,
  BuildsPhysicsMaterialDescriptorRequestWithDefaultsAndDescriptorOverrides)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "builds_request" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Physics" / "ground.material.json";
  WriteText(descriptor_path,
    R"({
      "name": "ground",
      "static_friction": 0.95,
      "dynamic_friction": 0.70,
      "restitution": 0.05,
      "density": 1500.0,
      "combine_mode_friction": "max",
      "combine_mode_restitution": "average"
    })");

  const auto manifest_json = oxygen::cooker::test::ManifestText(R"({
      "mounts": [{ "name": "Content", "path": "." }],
      "output": ".cooked",
      "defaults": {
        "physics_material_descriptor": {
          "name": "default-material-name"
        }
      },
      "jobs": [
        {
          "type": "physics-material-descriptor",
          "source": "Physics/ground.material.json",
          "virtual_path": "/Cooked/Physics/Materials/ground.opmat",
          "name": "ground_material_job"
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
  EXPECT_EQ(
    request->job_name, std::optional<std::string> { "ground_material_job" });
  EXPECT_EQ(
    request->asset_virtual_path, "/Cooked/Physics/Materials/ground.opmat");
  ASSERT_HAS_VALUE(request->physics_material_descriptor);

  const auto normalized
    = json::parse(request->physics_material_descriptor->descriptor_json);
  EXPECT_EQ(normalized.at("name").get<std::string>(), "ground");
  EXPECT_EQ(normalized.at("density").get<double>(), 1500.0);
}

NOLINT_TEST(ImportManifestPhysicsMaterialDescriptorTest,
  CollectsAndPropagatesPhysicsMaterialDescriptorDependencies)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "collects_dependencies" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Physics" / "ground.material.json";
  WriteText(descriptor_path,
    R"({
      "name": "ground",
      "static_friction": 0.95,
      "dynamic_friction": 0.70,
      "restitution": 0.05,
      "density": 1500.0
    })");

  WriteText(manifest_path, oxygen::cooker::test::ManifestText(R"({
      "mounts": [{ "name": "Content", "path": "." }],
      "output": "physics-material-cooked",
      "jobs": [
        {
          "id": "buffers.shared",
          "type": "buffer-container",
          "source": "Buffers/shared.buffers.json"
        },
        {
          "id": "physics.material.ground",
          "type": "physics-material-descriptor",
          "source": "Physics/ground.material.json",
          "virtual_path": "/Cooked/Physics/Materials/ground.opmat",
          "depends_on": ["buffers.shared"]
        }
      ]
    })"));

  auto errors = std::vector<ImportDiagnostic> {};
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  ASSERT_HAS_VALUE(manifest) << DiagnosticSummary(errors);
  ASSERT_EQ(manifest->jobs.size(), 2U);
  EXPECT_EQ(manifest->jobs.at(1).id, "physics.material.ground");
  ASSERT_EQ(manifest->jobs.at(1).depends_on.size(), 1U);
  EXPECT_EQ(manifest->jobs.at(1).depends_on.at(0), "buffers.shared");

  auto request_errors = std::vector<ImportDiagnostic> {};
  const auto request = manifest->jobs.at(1).BuildRequest(request_errors);
  ASSERT_HAS_VALUE(request) << DiagnosticSummary(request_errors);
  ASSERT_HAS_VALUE(request->orchestration);
  EXPECT_EQ(request->orchestration->job_id, "physics.material.ground");
  ASSERT_EQ(request->orchestration->depends_on.size(), 1U);
  EXPECT_EQ(request->orchestration->depends_on.at(0), "buffers.shared");
}

NOLINT_TEST(ImportManifestPhysicsMaterialDescriptorTest,
  RejectsPhysicsMaterialDescriptorJobWithDisallowedKeys)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "rejects_disallowed_key" / "import_manifest.json";
  WriteText(manifest_path, oxygen::cooker::test::ManifestText(R"({
      "mounts": [{ "name": "Content", "path": "." }],
      "jobs": [
        {
          "type": "physics-material-descriptor",
          "source": "Physics/ground.material.json",
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

NOLINT_TEST(ImportManifestPhysicsMaterialDescriptorTest,
  RejectsDescriptorPayloadThatViolatesDescriptorSchema)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "rejects_bad_descriptor_payload" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Physics" / "bad.material.json";
  WriteText(descriptor_path,
    R"({
      "name": "bad",
      "static_friction": 0.8,
      "unexpected": true
    })");

  WriteText(manifest_path, oxygen::cooker::test::ManifestText(R"({
      "mounts": [{ "name": "Content", "path": "." }],
      "output": "physics-material-cooked",
      "jobs": [
        {
          "type": "physics-material-descriptor",
          "source": "Physics/bad.material.json",
          "virtual_path": "/Cooked/Physics/Materials/bad.opmat"
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
        kPhysicsMaterialSchemaValidationFailed));
}

NOLINT_TEST(ImportManifestPhysicsMaterialDescriptorTest,
  RejectsLegacyPhysicsResourceDescriptorJobType)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "rejects_legacy_job_type" / "import_manifest.json";
  WriteText(manifest_path, oxygen::cooker::test::ManifestText(R"({
      "mounts": [{ "name": "Content", "path": "." }],
      "jobs": [
        {
          "type": "physics-resource-descriptor",
          "source": "Physics/legacy.resource.json"
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

} // namespace
