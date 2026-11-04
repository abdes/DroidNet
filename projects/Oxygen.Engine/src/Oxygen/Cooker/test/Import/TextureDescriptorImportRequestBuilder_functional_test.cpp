//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/TextureDescriptorImportRequestBuilder.cpp

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequestCodes.h>
#include <Oxygen/Cooker/Import/TextureDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/TextureDescriptorImportSettings.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace {

using oxygen::content::import::ImportDiagnostic;
namespace request_codes = oxygen::content::import::request_codes;
using oxygen::cooker::test::DiagnosticSummary;
using oxygen::cooker::test::HasDiagnostic;

using oxygen::ColorSpace;
using oxygen::content::import::TextureDescriptorImportSettings;
using oxygen::content::import::TextureIntent;
using oxygen::content::import::internal::BuildTextureDescriptorRequest;
using oxygen::cooker::test::ContentAndOtherMountTable;
using oxygen::cooker::test::ContentMountTable;
using oxygen::cooker::test::DiagnosticSummary;
using oxygen::cooker::test::HasDiagnosticCode;
using oxygen::cooker::test::WriteText;
using oxygen::testing::ScopedTempDir;

auto MakeBaseSettings(const std::filesystem::path& descriptor_path)
  -> TextureDescriptorImportSettings
{
  auto settings = TextureDescriptorImportSettings {};
  settings.descriptor_path = descriptor_path.string();
  settings.mounts = ContentMountTable(descriptor_path.parent_path());
  settings.texture.cooked_root
    = (descriptor_path.parent_path() / ".cooked").string();
  settings.texture.job_name = "base-job";
  settings.texture.color_space = "linear";
  return settings;
}

NOLINT_TEST(TextureDescriptorImportRequestBuilderTest,
  PreservesJobIdentityWithoutRewritingSourceDescriptor)
{
  auto settings = TextureDescriptorImportSettings {};
  const ScopedTempDir temp;
  settings.descriptor_path = "texture.json";
  settings.mounts = ContentMountTable(temp.Path());
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
  // A descriptor never names its own identity: the schema refuses the property.
  EXPECT_FALSE(settings.Prepare(
    R"({"source":"image.png","virtual_path":"/Content/Textures/Retained.otex"})",
    diagnostics));
  EXPECT_TRUE(HasDiagnosticCode(diagnostics,
    oxygen::content::import::import_codes::
      kTextureDescriptorSchemaValidationFailed))
    << DiagnosticSummary(diagnostics);
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
  settings.texture.virtual_path = "/Content/Textures/brick.otex";
  auto errors = std::vector<ImportDiagnostic> {};

  const auto request = BuildTextureDescriptorRequest(settings, errors);

  ASSERT_HAS_VALUE(request) << DiagnosticSummary(errors);
  EXPECT_TRUE(errors.empty());
  EXPECT_EQ(request.value().asset_virtual_path, "/Content/Textures/brick.otex");
  ASSERT_HAS_VALUE(request->cooked_root)
    << "Expected cooked root to be present";
  EXPECT_TRUE(request->cooked_root->is_absolute());
  EXPECT_EQ(request->source_path,
    std::filesystem::weakly_canonical(
      descriptor_path.parent_path() / "images" / "brick_albedo.png"));
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
  auto errors = std::vector<ImportDiagnostic> {};

  const auto request = BuildTextureDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors,
    HasDiagnostic(oxygen::content::import::import_codes::
        kTextureDescriptorSchemaValidationFailed));
}

NOLINT_TEST(
  TextureDescriptorImportRequestBuilderTest, RejectsMissingDescriptorFile)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "missing_file";
  const auto descriptor_path = dir / "Textures" / "missing.texture.json";
  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::vector<ImportDiagnostic> {};

  const auto request = BuildTextureDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors, HasDiagnostic(request_codes::kDocumentReadFailed));
}

NOLINT_TEST(
  TextureDescriptorPreparationTest, ResolvesLayeredInputsWithoutAWriter)
{
  TextureDescriptorImportSettings settings;
  const ScopedTempDir temp;
  settings.descriptor_path
    = (temp.Path() / "descriptor-preparation" / "texture.json").string();
  settings.mounts = ContentMountTable(temp.Path());
  settings.texture.job_name = "Default";
  settings.texture.virtual_path = "/Content/Layers.otex";
  settings.texture.flip_y = true;
  std::vector<oxygen::content::import::ImportDiagnostic> diagnostics;
  const auto prepared = settings.Prepare(R"({
    "source":"images/base.png","name":"Layers",
    "sources":[{"file":"images/layer.png","layer":1,"mip":0,"slice":0}],
    "decode":{"flip_y":false},"mips":{"policy":"none"}
  })",
    diagnostics);
  ASSERT_HAS_VALUE(prepared) << "Expected prepared to contain a value";
  EXPECT_TRUE(diagnostics.empty());
  EXPECT_TRUE(prepared->cooked_root.empty());
  EXPECT_EQ(prepared->job_name, "Layers");
  EXPECT_EQ(prepared->virtual_path, "/Content/Layers.otex");
  EXPECT_FALSE(prepared->flip_y);
  EXPECT_EQ(prepared->mip_policy, "none");
  EXPECT_EQ(std::filesystem::path(prepared->source_path),
    std::filesystem::weakly_canonical(
      std::filesystem::path(settings.descriptor_path).parent_path()
      / "images/base.png"));
  ASSERT_EQ(prepared->sources.size(), 1U);
  EXPECT_EQ(prepared->sources.at(0).layer, 1U);
  EXPECT_EQ(std::filesystem::path(prepared->sources.at(0).file),
    std::filesystem::weakly_canonical(
      std::filesystem::path(settings.descriptor_path).parent_path()
      / "images/layer.png"));
}

//! A `source` in the descriptor's own mount is relative to the descriptor's
//! folder, however far up it climbs inside the mount.
NOLINT_TEST(
  TextureDescriptorPreparationTest, ResolvesAnOwnMountSourceRelatively)
{
  const ScopedTempDir temp;
  auto settings = TextureDescriptorImportSettings {};
  settings.descriptor_path
    = (temp.Path() / "Textures" / "brick.texture.json").string();
  settings.mounts = ContentMountTable(temp.Path());
  std::vector<oxygen::content::import::ImportDiagnostic> diagnostics;

  const auto prepared
    = settings.Prepare(R"({"source":"../Images/brick.png"})", diagnostics);

  ASSERT_HAS_VALUE(prepared) << DiagnosticSummary(diagnostics);
  EXPECT_TRUE(diagnostics.empty());
  EXPECT_EQ(std::filesystem::path(prepared->source_path),
    std::filesystem::weakly_canonical(temp.Path() / "Images" / "brick.png"));
}

//! A `source` that is an absolute mount path to the descriptor's own mount is
//! refused: inside its mount a reference is relative.
NOLINT_TEST(TextureDescriptorPreparationTest, RefusesAnAbsoluteOwnMountSource)
{
  const ScopedTempDir temp;
  auto settings = TextureDescriptorImportSettings {};
  settings.descriptor_path
    = (temp.Path() / "Textures" / "brick.texture.json").string();
  settings.mounts = ContentMountTable(temp.Path());
  std::vector<oxygen::content::import::ImportDiagnostic> diagnostics;

  const auto prepared = settings.Prepare(
    R"({"source":"/Content/Images/brick.png"})", diagnostics);

  EXPECT_FALSE(prepared.has_value());
  ASSERT_TRUE(HasDiagnosticCode(
    diagnostics, oxygen::content::import::import_codes::kReferenceInvalid))
    << DiagnosticSummary(diagnostics);
  EXPECT_THAT(DiagnosticSummary(diagnostics),
    ::testing::HasSubstr(
      std::string(oxygen::content::vpath_codes::kReferenceSelfAbsolute)));
}

//! A `source` that is an absolute mount path to another mount is located in
//! that mount, whatever folder the descriptor is in.
NOLINT_TEST(TextureDescriptorPreparationTest, ResolvesAnotherMountPathSource)
{
  const ScopedTempDir temp;
  auto settings = TextureDescriptorImportSettings {};
  settings.descriptor_path
    = (temp.Path() / "Content" / "Textures" / "brick.texture.json").string();
  settings.mounts = ContentAndOtherMountTable(temp.Path());
  std::vector<oxygen::content::import::ImportDiagnostic> diagnostics;

  const auto prepared
    = settings.Prepare(R"({"source":"/Other/brick.png"})", diagnostics);

  ASSERT_HAS_VALUE(prepared) << DiagnosticSummary(diagnostics);
  EXPECT_TRUE(diagnostics.empty());
  EXPECT_EQ(std::filesystem::path(prepared->source_path),
    std::filesystem::weakly_canonical(temp.Path() / "Other" / "brick.png"));
}

//! A relative `source` that climbs above the descriptor's mount is refused
//! with `import.reference_invalid` naming `vpath.reference_outside_mount`.
NOLINT_TEST(
  TextureDescriptorPreparationTest, RefusesARelativeSourceLeavingItsMount)
{
  const ScopedTempDir temp;
  auto settings = TextureDescriptorImportSettings {};
  settings.descriptor_path
    = (temp.Path() / "Content" / "Textures" / "brick.texture.json").string();
  settings.mounts = ContentAndOtherMountTable(temp.Path());
  std::vector<oxygen::content::import::ImportDiagnostic> diagnostics;

  const auto prepared
    = settings.Prepare(R"({"source":"../../Other/brick.png"})", diagnostics);

  EXPECT_FALSE(prepared.has_value());
  ASSERT_TRUE(HasDiagnosticCode(
    diagnostics, oxygen::content::import::import_codes::kReferenceInvalid))
    << DiagnosticSummary(diagnostics);
  EXPECT_THAT(DiagnosticSummary(diagnostics),
    ::testing::HasSubstr(
      std::string(oxygen::content::vpath_codes::kReferenceOutsideMount)));
}

NOLINT_TEST(TextureDescriptorImportRequestBuilderTest,
  PreparesLayeredSourcesWithoutDestination)
{
  auto settings = oxygen::content::import::TextureImportSettings {};
  settings.source_path = "authoring/texture.png";
  settings.virtual_path = "/Cooked/Textures/Layered.otex";
  settings.job_name = "Layered";
  settings.sources
    = { { .file = "layer.png", .layer = 1, .mip = 0, .slice = 0 } };
  std::vector<ImportDiagnostic> errors;
  const auto request = settings.Prepare(errors);
  ASSERT_HAS_VALUE(request) << DiagnosticSummary(errors);
  EXPECT_FALSE(request->cooked_root.has_value());
  ASSERT_EQ(request->additional_sources.size(), 1U);
  EXPECT_EQ(request->additional_sources.front().path,
    std::filesystem::path("authoring/layer.png"));
  EXPECT_EQ(request->additional_sources.front().subresource.array_layer, 1U);
  EXPECT_EQ(request->GetTextureDescriptorRelPath(), "Textures/Layered.otex");
}

} // namespace
