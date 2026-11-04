//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/InputImportRequestBuilder.cpp

#include <filesystem>
#include <string>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequestCodes.h>
#include <Oxygen/Cooker/Import/InputImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/InputImportSettings.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::ImportDiagnostic;
namespace request_codes = oxygen::content::import::request_codes;
using oxygen::cooker::test::DiagnosticSummary;
using oxygen::cooker::test::HasDiagnostic;

using oxygen::content::import::InputImportSettings;
using oxygen::content::import::internal::BuildInputImportRequest;

//! The request carries the source's identity, under which its products are
//! named.
NOLINT_TEST(InputImportRequestBuilderTest, AcceptsInputSource)
{
  auto settings = InputImportSettings {};
  settings.source_path = "Content/Input/Player.input.json";
  settings.virtual_path = "/Content/Input/Player.input";
  std::vector<ImportDiagnostic> errors;

  const auto request = BuildInputImportRequest(settings, errors);

  ASSERT_HAS_VALUE(request) << DiagnosticSummary(errors);
  EXPECT_TRUE(errors.empty());
  EXPECT_TRUE(request->input.has_value());
  EXPECT_EQ(request->asset_virtual_path, "/Content/Input/Player.input");
  EXPECT_FALSE(request->orchestration.has_value());
}

//! The standalone action format is gone: an action is declared by an input
//! source like any other.
NOLINT_TEST(InputImportRequestBuilderTest, RejectsStandaloneActionDocument)
{
  auto settings = InputImportSettings {};
  settings.source_path = "Content/Input/Move.input-action.json";
  std::vector<ImportDiagnostic> errors;

  const auto request = BuildInputImportRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
}

NOLINT_TEST(InputImportRequestBuilderTest, RejectsMissingSourcePath)
{
  auto settings = InputImportSettings {};
  std::vector<ImportDiagnostic> errors;

  const auto request = BuildInputImportRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors, HasDiagnostic(request_codes::kSourceMissing));
}

NOLINT_TEST(InputImportRequestBuilderTest, RejectsUnsupportedSourceExtension)
{
  auto settings = InputImportSettings {};
  settings.source_path = "Content/Input/Player.json";
  std::vector<ImportDiagnostic> errors;

  const auto request = BuildInputImportRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors, HasDiagnostic(request_codes::kInputSourceNameInvalid));
}

NOLINT_TEST(InputImportRequestBuilderTest, AcceptsAbsoluteCookedRoot)
{
  auto settings = InputImportSettings {};
  settings.source_path = "Content/Input/Player.input.json";
  // Only compared, never created on disk.
  const auto cooked_root = std::filesystem::temp_directory_path()
    / "oxygen-cooker-tests" / "unit" / "oxygen-input-cooked";
  settings.cooked_root = cooked_root.string();
  std::vector<ImportDiagnostic> errors;

  const auto request = BuildInputImportRequest(settings, errors);

  ASSERT_HAS_VALUE(request) << DiagnosticSummary(errors);
  ASSERT_HAS_VALUE(request->cooked_root);
  EXPECT_EQ(*request->cooked_root, cooked_root);
}

NOLINT_TEST(InputImportRequestBuilderTest, RejectsRelativeCookedRoot)
{
  auto settings = InputImportSettings {};
  settings.source_path = "Content/Input/Player.input.json";
  settings.cooked_root = ".cooked";
  std::vector<ImportDiagnostic> errors;

  const auto request = BuildInputImportRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors, HasDiagnostic(request_codes::kCookedRootNotAbsolute));
}

} // namespace
