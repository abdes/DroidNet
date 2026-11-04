//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/PhysicsMaterialSource.cpp

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/PhysicsMaterialSource.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {
  using oxygen::cooker::test::HasDiagnosticCode;
  using oxygen::cooker::test::PhysicsMaterialDescriptor;

  constexpr auto kIdentity = "/Content/Physics/Ground.opmat";

  auto Prepare(
    const std::string& text, std::vector<ImportDiagnostic>& diagnostics)
  {
    return internal::PreparePhysicsMaterialDescriptor(
      text, "ground.opmat.json", diagnostics);
  }

  NOLINT_TEST(PhysicsMaterialPreparationTest, AcceptsAValidDocument)
  {
    auto document = PhysicsMaterialDescriptor { kIdentity };
    document.Set("/static_friction", 0.6).Set("/combine_mode_friction", "min");
    std::vector<ImportDiagnostic> diagnostics;

    const auto prepared = Prepare(document.Text(), diagnostics);

    ASSERT_HAS_VALUE(prepared);
    EXPECT_TRUE(diagnostics.empty());
    EXPECT_EQ(prepared->at("combine_mode_friction"), "min");
  }

  NOLINT_TEST(PhysicsMaterialPreparationTest, RefusesInvalidJson)
  {
    std::vector<ImportDiagnostic> diagnostics;

    const auto prepared = Prepare("{ \"name\": ", diagnostics);

    EXPECT_FALSE(prepared.has_value());
    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(
      diagnostics[0].code, import_codes::kPhysicsMaterialRequestInvalidJson);
    EXPECT_EQ(diagnostics[0].source_path, "ground.opmat.json");
  }

  NOLINT_TEST(PhysicsMaterialPreparationTest, RefusesAComment)
  {
    std::vector<ImportDiagnostic> diagnostics;

    const auto prepared
      = Prepare(R"({ /* note */ "name": "Ground" })", diagnostics);

    EXPECT_FALSE(prepared.has_value());
    EXPECT_TRUE(HasDiagnosticCode(
      diagnostics, import_codes::kPhysicsMaterialRequestInvalidJson));
  }

  NOLINT_TEST(PhysicsMaterialPreparationTest, RefusesANonObjectRoot)
  {
    std::vector<ImportDiagnostic> diagnostics;

    const auto prepared = Prepare("[1, 2]", diagnostics);

    EXPECT_FALSE(prepared.has_value());
    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(
      diagnostics[0].code, import_codes::kPhysicsMaterialRequestInvalidJson);
  }

  NOLINT_TEST(PhysicsMaterialPreparationTest, LocatesEverySchemaIssue)
  {
    auto document = PhysicsMaterialDescriptor { kIdentity };
    document.Set("/restitution", "bouncy").Set("/combine_mode_friction", "sum");
    std::vector<ImportDiagnostic> diagnostics;

    const auto prepared = Prepare(document.Text(), diagnostics);

    EXPECT_FALSE(prepared.has_value());
    auto pointers = std::vector<std::string> {};
    for (const auto& diagnostic : diagnostics) {
      EXPECT_EQ(
        diagnostic.code, import_codes::kPhysicsMaterialSchemaValidationFailed);
      EXPECT_TRUE(diagnostic.object_path.empty());
      pointers.push_back(diagnostic.json_pointer);
    }
    EXPECT_THAT(pointers,
      ::testing::UnorderedElementsAre(
        "/restitution", "/combine_mode_friction"));
  }

} // namespace
} // namespace oxygen::content::import::test
