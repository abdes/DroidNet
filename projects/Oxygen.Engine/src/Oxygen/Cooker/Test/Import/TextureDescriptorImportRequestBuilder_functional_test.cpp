//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/TextureDescriptorImportRequestBuilder.cpp

#include <filesystem>
#include <optional>
#include <sstream>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/TextureDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/TextureDescriptorImportSettings.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::ColorSpace;
using oxygen::content::import::TextureDescriptorImportSettings;
using oxygen::content::import::TextureIntent;
using oxygen::content::import::internal::BuildTextureDescriptorRequest;
using oxygen::cooker::test::ScopedTempDir;
using oxygen::cooker::test::WriteText;

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
  PreservesJobIdentityWithoutRewritingSourceDescriptor)
{
  auto settings = TextureDescriptorImportSettings {};
  settings.descriptor_path = "texture.json";
  settings.texture.virtual_path = "/Content/Textures/Retained.otex";
  auto diagnostics = std::vector<oxygen::content::import::ImportDiagnostic> {};
  const auto prepared
    = settings.Prepare(R"({"source":"image.png"})", diagnostics);
  if (!prepared.has_value()) {
    ADD_FAILURE() << "Descriptor preparation unexpectedly failed";
    return;
  }
  EXPECT_EQ(prepared->virtual_path, settings.texture.virtual_path);
  EXPECT_TRUE(diagnostics.empty());
  EXPECT_TRUE(settings.Prepare(
    R"({"source":"image.png","virtual_path":"/Content/Textures/Retained.otex"})",
    diagnostics));
  EXPECT_FALSE(settings.Prepare(
    R"({"source":"image.png","virtual_path":"/Content/Textures/Different.otex"})",
    diagnostics));
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().code, "texture.descriptor.identity_conflict");
}

NOLINT_TEST(TextureDescriptorImportRequestBuilderTest,
  BuildsRequestFromValidDescriptorAndReusesTexturePath)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "valid_request";
  const auto descriptor_path = dir / "Textures" / "brick.texture.json";
  WriteText(descriptor_path,
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

  ASSERT_TRUE(request.has_value()) << errors.str();
  EXPECT_TRUE(errors.str().empty());
  EXPECT_EQ(
    request.value().texture_virtual_path, "/Content/Textures/brick.otex");
  ASSERT_TRUE(request->cooked_root.has_value())
    << "Expected cooked root to be present";
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
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "schema_violation";
  const auto descriptor_path = dir / "Textures" / "bad.texture.json";
  WriteText(descriptor_path,
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
  EXPECT_THAT(errors.str(),
    ::testing::HasSubstr("texture.descriptor.schema_validation_failed"));
}

NOLINT_TEST(
  TextureDescriptorImportRequestBuilderTest, RejectsMissingDescriptorFile)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "missing_file";
  const auto descriptor_path = dir / "Textures" / "missing.texture.json";
  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildTextureDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(
    errors.str(), ::testing::HasSubstr("failed to open texture descriptor"));
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
  ASSERT_TRUE(prepared.has_value()) << "Expected prepared to contain a value";
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
  ASSERT_TRUE(request.has_value()) << errors.str();
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
