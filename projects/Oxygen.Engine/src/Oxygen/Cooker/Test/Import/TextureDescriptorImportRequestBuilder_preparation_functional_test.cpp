//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/TextureDescriptorImportRequestBuilder.cpp

#include <string>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/TextureDescriptorImportSettings.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::TextureDescriptorImportSettings;

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
  EXPECT_THAT(diagnostics.front().message, ::testing::HasSubstr("preset"));
}

} // namespace
