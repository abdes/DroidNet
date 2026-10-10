//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/InputImportRequestBuilder.cpp

#include <filesystem>
#include <sstream>
#include <string>

#include <Oxygen/Cooker/Import/InputImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/InputImportSettings.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::InputImportSettings;
using oxygen::content::import::internal::BuildInputImportRequest;

NOLINT_TEST(InputImportRequestBuilderTest, AcceptsPrimaryInputDocument)
{
  auto settings = InputImportSettings {};
  settings.source_path = "Content/Input/Player.input.json";
  std::ostringstream errors;

  const auto request = BuildInputImportRequest(settings, errors);

  ASSERT_HAS_VALUE(request) << errors.str();
  EXPECT_TRUE(errors.str().empty());
  EXPECT_TRUE(request->input.has_value());
  EXPECT_FALSE(request->orchestration.has_value());
}

NOLINT_TEST(InputImportRequestBuilderTest, AcceptsStandaloneActionDocument)
{
  auto settings = InputImportSettings {};
  settings.source_path = "Content/Input/Move.input-action.json";
  std::ostringstream errors;

  const auto request = BuildInputImportRequest(settings, errors);

  ASSERT_HAS_VALUE(request) << errors.str();
  EXPECT_TRUE(request->input.has_value());
}

NOLINT_TEST(InputImportRequestBuilderTest, RejectsMissingSourcePath)
{
  auto settings = InputImportSettings {};
  std::ostringstream errors;

  const auto request = BuildInputImportRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors.str(), ::testing::HasSubstr("source_path is required"));
}

NOLINT_TEST(InputImportRequestBuilderTest, RejectsUnsupportedSourceExtension)
{
  auto settings = InputImportSettings {};
  settings.source_path = "Content/Input/Player.json";
  std::ostringstream errors;

  const auto request = BuildInputImportRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors.str(), ::testing::HasSubstr("*.input.json"));
}

NOLINT_TEST(InputImportRequestBuilderTest, AcceptsAbsoluteCookedRoot)
{
  auto settings = InputImportSettings {};
  settings.source_path = "Content/Input/Player.input.json";
  // Only compared, never created on disk.
  const auto cooked_root = std::filesystem::temp_directory_path()
    / "oxygen-cooker-tests" / "unit" / "oxygen-input-cooked";
  settings.cooked_root = cooked_root.string();
  std::ostringstream errors;

  const auto request = BuildInputImportRequest(settings, errors);

  ASSERT_HAS_VALUE(request) << errors.str();
  ASSERT_HAS_VALUE(request->cooked_root);
  EXPECT_EQ(*request->cooked_root, cooked_root);
}

NOLINT_TEST(InputImportRequestBuilderTest, RejectsRelativeCookedRoot)
{
  auto settings = InputImportSettings {};
  settings.source_path = "Content/Input/Player.input.json";
  settings.cooked_root = ".cooked";
  std::ostringstream errors;

  const auto request = BuildInputImportRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(
    errors.str(), ::testing::HasSubstr("cooked root must be an absolute path"));
}

NOLINT_TEST(InputImportRequestBuilderTest, CarriesManifestOrchestrationMetadata)
{
  auto settings = InputImportSettings {};
  settings.source_path = "Content/Input/Player.input.json";
  std::ostringstream errors;

  const auto request = BuildInputImportRequest(
    settings, "  core.input  ", { " deps.a ", "", "deps.b" }, errors);

  ASSERT_HAS_VALUE(request) << errors.str();
  ASSERT_HAS_VALUE(request->orchestration);
  EXPECT_EQ(request->orchestration->job_id, "core.input");
  ASSERT_EQ(request->orchestration->depends_on.size(), 2U);
  EXPECT_EQ(request->orchestration->depends_on.at(0), "deps.a");
  EXPECT_EQ(request->orchestration->depends_on.at(1), "deps.b");
}

NOLINT_TEST(InputImportRequestBuilderTest, RejectsDependsOnWithoutJobId)
{
  auto settings = InputImportSettings {};
  settings.source_path = "Content/Input/Player.input.json";
  std::ostringstream errors;

  const auto request
    = BuildInputImportRequest(settings, "", { "core.input" }, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors.str(),
    ::testing::HasSubstr("depends_on requires a non-empty job_id"));
}

} // namespace
