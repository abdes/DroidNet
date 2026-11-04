//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/TextureDescriptorImportRequestBuilder.cpp

#include <memory>
#include <string>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequestCodes.h>
#include <Oxygen/Cooker/Import/TextureDescriptorImportSettings.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace {

using oxygen::content::import::TextureDescriptorImportSettings;
using oxygen::cooker::test::ContentMountTable;
using oxygen::testing::ScopedTempDir;

NOLINT_TEST(TextureDescriptorPreparationTest, KeepsNativeSchemaDiagnostics)
{
  const ScopedTempDir temp;
  TextureDescriptorImportSettings settings;
  settings.descriptor_path = "texture.json";
  settings.mounts = ContentMountTable(temp.Path());
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
  const ScopedTempDir temp;
  TextureDescriptorImportSettings settings;
  settings.descriptor_path = "texture.json";
  settings.mounts = ContentMountTable(temp.Path());
  std::vector<oxygen::content::import::ImportDiagnostic> diagnostics;
  EXPECT_FALSE(
    settings.Prepare(R"({"source":"image.png"} trailing)", diagnostics));
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().code, "texture.descriptor.invalid_json");
}

NOLINT_TEST(TextureDescriptorPreparationTest,
  ValidatesInheritedRecipeBeforeDestinationSelection)
{
  const ScopedTempDir temp;
  TextureDescriptorImportSettings settings;
  settings.descriptor_path = "texture.json";
  settings.mounts = ContentMountTable(temp.Path());
  settings.texture.preset = "unknown-native-preset";
  std::vector<oxygen::content::import::ImportDiagnostic> diagnostics;
  EXPECT_FALSE(settings.Prepare(R"({"source":"image.png"})", diagnostics));
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().code,
    oxygen::content::import::request_codes::kOptionInvalid);
  EXPECT_EQ(diagnostics.front().source_path, "texture.json");
  EXPECT_THAT(diagnostics.front().message, ::testing::HasSubstr("preset"));
}

//! The one rule across fields a descriptor can break is located in it.
NOLINT_TEST(TextureDescriptorImportSettingsPreparationTest,
  LocatesTheBc7RuleInTheDescriptor)
{
  const ScopedTempDir temp;
  TextureDescriptorImportSettings settings;
  settings.descriptor_path = "texture.json";
  settings.mounts = ContentMountTable(temp.Path());
  std::vector<oxygen::content::import::ImportDiagnostic> diagnostics;
  EXPECT_FALSE(settings.Prepare(
    R"({"source":"image.png","output":{"format":"rgba8","bc7_quality":"high"}})",
    diagnostics));
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().code,
    oxygen::content::import::request_codes::kBc7QualityNeedsBc7Format);
  EXPECT_EQ(diagnostics.front().json_pointer, "/output/bc7_quality");
}

} // namespace
