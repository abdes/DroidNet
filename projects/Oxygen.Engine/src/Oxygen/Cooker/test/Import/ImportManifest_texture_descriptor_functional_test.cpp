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

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace {

using oxygen::content::import::ImportDiagnostic;
using oxygen::cooker::test::DiagnosticSummary;
using oxygen::cooker::test::HasDiagnostic;

using oxygen::ColorSpace;
using oxygen::content::import::ImportManifest;
using oxygen::content::import::TextureIntent;
using oxygen::cooker::test::WriteText;
using oxygen::testing::ScopedTempDir;

NOLINT_TEST(ImportManifestTextureDescriptorTest,
  CarriesExplicitRootIdentitiesIntoNativeRequests)
{
  const ScopedTempDir temp;
  const auto root = temp.Path() / "root-identities";
  const auto top = oxygen::Uuid::Generate();
  const auto defaults = oxygen::Uuid::Generate();
  const auto job_key = oxygen::Uuid::Generate();
  const auto manifest_json = nlohmann::json {
    { "version", 1 },
    {
      "mounts",
      nlohmann::json::array({ { { "name", "Content" }, { "path", "." } } }),
    },
    { "output", "first" },
    { "source_key", top.ToString() },
    { "defaults", { { "source_key", defaults.ToString() } } },
    {
      "jobs",
      nlohmann::json::array({
        {
          { "type", "texture" },
          { "source", "first.png" },
          { "virtual_path", "/Cooked/Textures/First.otex" },
          { "id", "first" },
        },
        {
          { "type", "texture" },
          { "source", "second.png" },
          { "virtual_path", "/Cooked/Textures/Second.otex" },
          { "id", "second" },
          { "output", "second" },
          { "source_key", job_key.ToString() },
        },
      }),
    },
  };
  std::vector<ImportDiagnostic> errors;
  const auto manifest
    = ImportManifest::Parse(manifest_json.dump(), root, {}, errors);
  ASSERT_HAS_VALUE(manifest) << DiagnosticSummary(errors);
  const auto requests = manifest->BuildRequests(errors);
  ASSERT_EQ(requests.size(), 2U) << DiagnosticSummary(errors);
  EXPECT_EQ(requests.at(0).source_key, oxygen::data::SourceKey { defaults });
  EXPECT_EQ(requests.at(1).source_key, oxygen::data::SourceKey { job_key });
}

NOLINT_TEST(ImportManifestTextureDescriptorTest,
  BuildsTextureRequestWithClearDefaultsJobAndDescriptorTreatment)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "builds_texture_descriptor" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Textures" / "brick.texture.json";
  WriteText(descriptor_path,
    R"({
      "source": "images/brick_nrm.png",
      "intent": "normal",
      "mips": {
        "policy": "max",
        "max_mips": 5
      },
      "output": {
        "format": "bc7",
        "packing_policy": "tight"
      }
    })");

  const auto* const manifest_json = R"({
      "version": 1,
      "mounts": [{ "name": "Content", "path": "." }],
      "output": ".cooked",
      "defaults": {
        "texture": {
          "intent": "albedo",
          "color_space": "srgb",
          "packing_policy": "d3d12"
        }
      },
      "jobs": [
        {
          "type": "texture-descriptor",
          "source": "Textures/brick.texture.json",
          "virtual_path": "/Cooked/Textures/Brick.otex",
          "name": "brick-job",
          "color_space": "linear"
        }
      ]
    })";
  WriteText(manifest_path, manifest_json);

  auto errors = std::vector<ImportDiagnostic> {};
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  ASSERT_HAS_VALUE(manifest) << DiagnosticSummary(errors);
  ASSERT_EQ(manifest->jobs.size(), 1U);

  auto request_errors = std::vector<ImportDiagnostic> {};
  const auto request = manifest->jobs.at(0).BuildRequest(request_errors);
  ASSERT_HAS_VALUE(request) << DiagnosticSummary(request_errors);
  ASSERT_HAS_VALUE(request->cooked_root);
  EXPECT_EQ(request->source_path,
    (root / "Textures" / "images" / "brick_nrm.png").lexically_normal());
  EXPECT_EQ(request->job_name, std::optional<std::string> { "brick-job" });

  // Descriptor-local intent overrides shared defaults.
  EXPECT_EQ(request->options.texture_tuning.intent, TextureIntent::kNormalTS);
  // Job override remains authoritative when descriptor omits this field.
  EXPECT_EQ(
    request->options.texture_tuning.source_color_space, ColorSpace::kLinear);
  // Descriptor value overrides default packing policy for this job.
  EXPECT_EQ(request->options.texture_tuning.packing_policy_id, "tight");
  // The job's identity reaches the request.
  EXPECT_EQ(request->asset_virtual_path, "/Cooked/Textures/Brick.otex");
}

NOLINT_TEST(ImportManifestTextureDescriptorTest,
  RejectsTextureDescriptorJobWithDisallowedKeys)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "rejects_disallowed_key" / "import_manifest.json";
  WriteText(manifest_path,
    R"({
      "version": 1,
      "mounts": [{ "name": "Content", "path": "." }],
      "jobs": [
        {
          "type": "texture-descriptor",
          "source": "Textures/brick.texture.json",
          "unit_policy": "normalize"
        }
      ]
    })");

  auto errors = std::vector<ImportDiagnostic> {};
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  EXPECT_FALSE(manifest.has_value());
  EXPECT_THAT(errors, HasDiagnostic("manifest.schema_validation_failed"));
}

NOLINT_TEST(ImportManifestTextureDescriptorTest,
  RejectsDescriptorPayloadThatViolatesDescriptorSchema)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "rejects_bad_descriptor_payload" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Textures" / "bad.texture.json";
  WriteText(descriptor_path,
    R"({
      "source": "images/brick_nrm.png",
      "mips": {
        "policy": "max"
      }
    })");

  WriteText(manifest_path,
    R"({
      "version": 1,
      "mounts": [{ "name": "Content", "path": "." }],
      "jobs": [
        {
          "type": "texture-descriptor",
          "source": "Textures/bad.texture.json",
          "virtual_path": "/Cooked/Textures/Bad.otex"
        }
      ]
    })");

  auto errors = std::vector<ImportDiagnostic> {};
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  ASSERT_HAS_VALUE(manifest) << DiagnosticSummary(errors);
  ASSERT_EQ(manifest->jobs.size(), 1U);

  auto request_errors = std::vector<ImportDiagnostic> {};
  const auto request = manifest->jobs.at(0).BuildRequest(request_errors);
  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(request_errors,
    HasDiagnostic("texture.descriptor.schema_validation_failed"));
}

} // namespace
