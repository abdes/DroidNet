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
#include <Oxygen/Cooker/Import/TextureDescriptorImportSettings.h>
#include <Oxygen/Cooker/test/Support/TempDir.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::TextureDescriptorImportSettings;
using oxygen::cooker::test::ContentMountTable;
using oxygen::cooker::test::ScopedTempDir;

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
  EXPECT_EQ(diagnostics.front().code, "texture.descriptor.recipe_invalid");
  EXPECT_THAT(diagnostics.front().message, ::testing::HasSubstr("preset"));
}

} // namespace
