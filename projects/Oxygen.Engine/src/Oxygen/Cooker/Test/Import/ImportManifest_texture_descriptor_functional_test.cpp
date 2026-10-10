//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/ImportManifest.cpp

#include <filesystem>
#include <optional>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::ColorSpace;
using oxygen::content::import::ImportManifest;
using oxygen::content::import::TextureIntent;
using oxygen::cooker::test::ScopedTempDir;
using oxygen::cooker::test::WriteText;

NOLINT_TEST(ImportManifestTextureDescriptorTest,
  CarriesExplicitRootIdentitiesIntoNativeRequests)
{
  const ScopedTempDir temp;
  const auto root = temp.Path() / "root-identities";
  const auto top = oxygen::Uuid::Generate();
  const auto defaults = oxygen::Uuid::Generate();
  const auto job_key = oxygen::Uuid::Generate();
  const auto manifest_json = nlohmann::json { { "version", 1 },
    { "output", (root / "first").generic_string() },
    { "source_key", top.ToString() },
    { "defaults", { { "source_key", defaults.ToString() } } },
    { "jobs",
      nlohmann::json::array({ { { "type", "texture" },
                                { "source", "first.png" }, { "id", "first" } },
        { { "type", "texture" }, { "source", "second.png" }, { "id", "second" },
          { "output", (root / "second").generic_string() },
          { "source_key", job_key.ToString() } } }) } };
  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Parse(manifest_json.dump(), root, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  const auto requests = manifest->BuildRequests(errors);
  ASSERT_EQ(requests.size(), 2U) << errors.str();
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

  const auto cooked_root = (root / ".cooked").generic_string();
  const auto manifest_json = std::string { R"({
      "version": 1,
      "output": ")" }
    + cooked_root + R"(",
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
          "name": "brick-job",
          "color_space": "linear"
        }
      ]
    })";
  WriteText(manifest_path, manifest_json);

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs.at(0).BuildRequest(request_errors);
  ASSERT_TRUE(request.has_value()) << request_errors.str();
  ASSERT_TRUE(request->cooked_root.has_value());
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
      "jobs": [
        {
          "type": "texture-descriptor",
          "source": "Textures/brick.texture.json",
          "unit_policy": "normalize"
        }
      ]
    })");

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  EXPECT_FALSE(manifest.has_value());
  EXPECT_THAT(
    errors.str(), ::testing::HasSubstr("manifest schema validation failed"));
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
      "jobs": [
        {
          "type": "texture-descriptor",
          "source": "Textures/bad.texture.json"
        }
      ]
    })");

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs.at(0).BuildRequest(request_errors);
  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(request_errors.str(),
    ::testing::HasSubstr("texture.descriptor.schema_validation_failed"));
}

} // namespace
