//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <sstream>
#include <string>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/InputImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/InputImportSettings.h>

namespace {

using oxygen::content::import::InputImportSettings;
using oxygen::content::import::internal::BuildInputImportRequest;

NOLINT_TEST(InputImportRequestBuilderTest, AcceptsPrimaryInputDocument)
{
  auto settings = InputImportSettings {};
  settings.source_path = "Content/Input/Player.input.json";
  std::ostringstream errors;

  const auto request = BuildInputImportRequest(settings, errors);

  ASSERT_TRUE(request.has_value()) << errors.str();
  EXPECT_TRUE(errors.str().empty());
  EXPECT_TRUE(request->options.input.has_value());
  EXPECT_FALSE(request->orchestration.has_value());
}

NOLINT_TEST(InputImportRequestBuilderTest, AcceptsStandaloneActionDocument)
{
  auto settings = InputImportSettings {};
  settings.source_path = "Content/Input/Move.input-action.json";
  std::ostringstream errors;

  const auto request = BuildInputImportRequest(settings, errors);

  ASSERT_TRUE(request.has_value()) << errors.str();
  EXPECT_TRUE(request->options.input.has_value());
}

NOLINT_TEST(InputImportRequestBuilderTest, RejectsMissingSourcePath)
{
  auto settings = InputImportSettings {};
  std::ostringstream errors;

  const auto request = BuildInputImportRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(
    errors.str().find("source_path is required") != std::string::npos);
}

NOLINT_TEST(InputImportRequestBuilderTest, RejectsUnsupportedSourceExtension)
{
  auto settings = InputImportSettings {};
  settings.source_path = "Content/Input/Player.json";
  std::ostringstream errors;

  const auto request = BuildInputImportRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("*.input.json") != std::string::npos);
}

NOLINT_TEST(InputImportRequestBuilderTest, CarriesManifestOrchestrationMetadata)
{
  auto settings = InputImportSettings {};
  settings.source_path = "Content/Input/Player.input.json";
  std::ostringstream errors;

  const auto request = BuildInputImportRequest(
    settings, "  core.input  ", { " deps.a ", "", "deps.b" }, errors);

  ASSERT_TRUE(request.has_value()) << errors.str();
  ASSERT_TRUE(request->orchestration.has_value());
  EXPECT_EQ(request->orchestration->job_id, "core.input");
  ASSERT_EQ(request->orchestration->depends_on.size(), 2U);
  EXPECT_EQ(request->orchestration->depends_on[0], "deps.a");
  EXPECT_EQ(request->orchestration->depends_on[1], "deps.b");
}

NOLINT_TEST(InputImportRequestBuilderTest, RejectsDependsOnWithoutJobId)
{
  auto settings = InputImportSettings {};
  settings.source_path = "Content/Input/Player.input.json";
  std::ostringstream errors;

  const auto request
    = BuildInputImportRequest(settings, "", { "core.input" }, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("depends_on requires a non-empty job_id")
    != std::string::npos);
}

} // namespace
