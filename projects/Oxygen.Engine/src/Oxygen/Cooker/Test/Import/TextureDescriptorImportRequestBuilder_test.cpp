//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/TextureDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/TextureDescriptorImportSettings.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/Testing/GTest.h>

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
      "virtual_path": "/Content/Textures/brick.otex",
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

  if (!request.has_value()) {
    FAIL() << errors.str();
  }
  EXPECT_TRUE(errors.str().empty());
  EXPECT_EQ(
    request.value().texture_virtual_path, "/Content/Textures/brick.otex");
  if (!request->cooked_root.has_value()) {
    FAIL();
  }
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

NOLINT_TEST(
  TextureDescriptorPreparationTest, ResolvesLayeredInputsWithoutAWriter)
{
  TextureDescriptorImportSettings settings;
  settings.descriptor_path = (std::filesystem::temp_directory_path()
    / "descriptor-preparation" / "texture.json")
                               .string();
  settings.texture.job_name = "Default";
  settings.texture.flip_y = true;
  std::vector<oxygen::content::import::ImportDiagnostic> diagnostics;
  const auto prepared = settings.Prepare(R"({
    "source":"images/base.png","name":"Layers","virtual_path":"/Content/Layers.otex",
    "sources":[{"file":"images/layer.png","layer":1,"mip":0,"slice":0}],
    "decode":{"flip_y":false},"mips":{"policy":"none"}
  })",
    diagnostics);
  if (!prepared.has_value()) {
    FAIL() << "Expected prepared to contain a value";
  }
  EXPECT_TRUE(diagnostics.empty());
  EXPECT_TRUE(prepared->cooked_root.empty());
  EXPECT_EQ(prepared->job_name, "Layers");
  EXPECT_EQ(prepared->virtual_path, "/Content/Layers.otex");
  EXPECT_FALSE(prepared->flip_y);
  EXPECT_EQ(prepared->mip_policy, "none");
  EXPECT_EQ(std::filesystem::path(prepared->source_path),
    (std::filesystem::path(settings.descriptor_path).parent_path()
      / "images/base.png")
      .lexically_normal());
  ASSERT_EQ(prepared->sources.size(), 1U);
  EXPECT_EQ(prepared->sources.at(0).layer, 1U);
  EXPECT_EQ(std::filesystem::path(prepared->sources.at(0).file),
    (std::filesystem::path(settings.descriptor_path).parent_path()
      / "images/layer.png")
      .lexically_normal());
}

NOLINT_TEST(TextureDescriptorPreparationTest, KeepsNativeSchemaDiagnostics)
{
  TextureDescriptorImportSettings settings;
  settings.descriptor_path = "texture.json";
  std::vector<oxygen::content::import::ImportDiagnostic> diagnostics;
  EXPECT_FALSE(settings.Prepare(
    R"({"source":"image.png","decode":{"unexpected":true}})", diagnostics));
  ASSERT_FALSE(diagnostics.empty());
  EXPECT_EQ(
    diagnostics.front().code, "texture.descriptor.schema_validation_failed");
  EXPECT_EQ(diagnostics.front().source_path, "texture.json");
}

NOLINT_TEST(
  TextureDescriptorPreparationTest, RejectsTrailingJsonInsteadOfIgnoringIt)
{
  TextureDescriptorImportSettings settings;
  settings.descriptor_path = "texture.json";
  std::vector<oxygen::content::import::ImportDiagnostic> diagnostics;
  EXPECT_FALSE(
    settings.Prepare(R"({"source":"image.png"} trailing)", diagnostics));
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().code, "texture.descriptor.invalid_json");
}

NOLINT_TEST(TextureDescriptorPreparationTest,
  ValidatesInheritedRecipeBeforeDestinationSelection)
{
  TextureDescriptorImportSettings settings;
  settings.descriptor_path = "texture.json";
  settings.texture.preset = "unknown-native-preset";
  std::vector<oxygen::content::import::ImportDiagnostic> diagnostics;
  EXPECT_FALSE(settings.Prepare(R"({"source":"image.png"})", diagnostics));
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().code, "texture.descriptor.recipe_invalid");
  EXPECT_TRUE(diagnostics.front().message.find("preset") != std::string::npos);
}

NOLINT_TEST(TextureDescriptorImportRequestBuilderTest,
  PreparesLayeredSourcesWithoutDestination)
{
  auto settings = oxygen::content::import::TextureImportSettings {};
  settings.source_path = "authoring/texture.png";
  settings.job_name = "Layered";
  settings.sources
    = { { .file = "layer.png", .layer = 1, .mip = 0, .slice = 0 } };
  std::ostringstream errors;
  const auto request = settings.Prepare(errors);
  if (!request.has_value()) {
    FAIL() << errors.str();
  }
  EXPECT_FALSE(request->cooked_root.has_value());
  ASSERT_EQ(request->additional_sources.size(), 1U);
  EXPECT_EQ(request->additional_sources.front().path,
    std::filesystem::path("authoring/layer.png"));
  EXPECT_EQ(request->additional_sources.front().subresource.array_layer, 1U);
  EXPECT_EQ(request->GetTextureDescriptorRelPath(),
    request->loose_cooked_layout.TextureDescriptorRelPath(
      "Layered", "Layered"));
}

} // namespace
