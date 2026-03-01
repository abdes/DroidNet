//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/TextureDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/TextureDescriptorImportSettings.h>

namespace {

using oxygen::ColorSpace;
using oxygen::content::import::TextureDescriptorImportSettings;
using oxygen::content::import::TextureIntent;
using oxygen::content::import::internal::BuildTextureDescriptorRequest;

auto MakeTempDir(const std::string_view stem) -> std::filesystem::path
{
  auto dir = std::filesystem::temp_directory_path() / "oxygen_texture_desc";
  dir /= std::filesystem::path { std::string { stem } };
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir);
  return dir;
}

auto WriteTextFile(
  const std::filesystem::path& path, const std::string_view text) -> void
{
  std::filesystem::create_directories(path.parent_path());
  auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(out.is_open());
  out << text;
}

auto MakeBaseSettings(const std::filesystem::path& descriptor_path)
  -> TextureDescriptorImportSettings
{
  auto settings = TextureDescriptorImportSettings {};
  settings.descriptor_path = descriptor_path.string();
  settings.texture.cooked_root
    = (descriptor_path.parent_path() / ".cooked").string();
  settings.texture.job_name = "base-job";
  settings.texture.color_space = "linear";
  settings.texture.with_content_hashing = false;
  return settings;
}

NOLINT_TEST(TextureDescriptorImportRequestBuilderTest,
  BuildsRequestFromValidDescriptorAndReusesTexturePath)
{
  const auto dir = MakeTempDir("valid_request");
  const auto descriptor_path = dir / "Textures" / "brick.texture.json";
  WriteTextFile(descriptor_path,
    R"({
      "source": "images/brick_albedo.png",
      "intent": "albedo",
      "decode": {
        "flip_y": true
      },
      "mips": {
        "policy": "full",
        "filter": "kaiser"
      },
      "output": {
        "format": "bc7_srgb",
        "packing_policy": "tight"
      }
    })");

  auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildTextureDescriptorRequest(settings, errors);

  ASSERT_TRUE(request.has_value()) << errors.str();
  EXPECT_TRUE(errors.str().empty());
  ASSERT_TRUE(request->cooked_root.has_value());
  EXPECT_TRUE(request->cooked_root->is_absolute());
  EXPECT_EQ(request->source_path,
    (descriptor_path.parent_path() / "images" / "brick_albedo.png")
      .lexically_normal());
  EXPECT_EQ(request->job_name, std::optional<std::string> { "base-job" });
  EXPECT_EQ(request->options.texture_tuning.intent, TextureIntent::kAlbedo);
  EXPECT_EQ(
    request->options.texture_tuning.source_color_space, ColorSpace::kLinear);
  EXPECT_EQ(request->options.texture_tuning.packing_policy_id, "tight");
}

NOLINT_TEST(TextureDescriptorImportRequestBuilderTest,
  RejectsDescriptorWithSchemaViolations)
{
  const auto dir = MakeTempDir("schema_violation");
  const auto descriptor_path = dir / "Textures" / "bad.texture.json";
  WriteTextFile(descriptor_path,
    R"({
      "source": "images/brick_albedo.png",
      "decode": {
        "unexpected": true
      }
    })");

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildTextureDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("texture.descriptor.schema_validation_failed")
    != std::string::npos);
}

NOLINT_TEST(
  TextureDescriptorImportRequestBuilderTest, RejectsMissingDescriptorFile)
{
  const auto dir = MakeTempDir("missing_file");
  const auto descriptor_path = dir / "Textures" / "missing.texture.json";
  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildTextureDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("failed to open texture descriptor")
    != std::string::npos);
}

} // namespace
