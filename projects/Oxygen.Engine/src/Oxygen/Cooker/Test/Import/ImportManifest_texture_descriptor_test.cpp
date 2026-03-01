//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/ImportManifest.h>

namespace {

using oxygen::ColorSpace;
using oxygen::content::import::ImportManifest;
using oxygen::content::import::TextureIntent;

auto MakeManifestPath(const std::string_view stem) -> std::filesystem::path
{
  auto dir = std::filesystem::temp_directory_path()
    / "oxygen_manifest_texture_descriptor";
  dir /= std::filesystem::path { std::string { stem } };
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir);
  return dir / "import_manifest.json";
}

auto WriteTextFile(
  const std::filesystem::path& path, const std::string_view text) -> void
{
  std::filesystem::create_directories(path.parent_path());
  auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(out.is_open());
  out << text;
}

NOLINT_TEST(ImportManifestTextureDescriptorTest,
  BuildsTextureRequestWithClearDefaultsJobAndDescriptorTreatment)
{
  const auto manifest_path = MakeManifestPath("builds_texture_descriptor");
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Textures" / "brick.texture.json";
  WriteTextFile(descriptor_path,
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
  WriteTextFile(manifest_path, manifest_json);

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs[0].BuildRequest(request_errors);
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
  const auto manifest_path = MakeManifestPath("rejects_disallowed_key");
  WriteTextFile(manifest_path,
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
  EXPECT_TRUE(errors.str().find("manifest schema validation failed")
    != std::string::npos);
}

NOLINT_TEST(ImportManifestTextureDescriptorTest,
  RejectsDescriptorPayloadThatViolatesDescriptorSchema)
{
  const auto manifest_path = MakeManifestPath("rejects_bad_descriptor_payload");
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Textures" / "bad.texture.json";
  WriteTextFile(descriptor_path,
    R"({
      "source": "images/brick_nrm.png",
      "mips": {
        "policy": "max"
      }
    })");

  WriteTextFile(manifest_path,
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
  const auto request = manifest->jobs[0].BuildRequest(request_errors);
  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(
    request_errors.str().find("texture.descriptor.schema_validation_failed")
    != std::string::npos);
}

} // namespace
