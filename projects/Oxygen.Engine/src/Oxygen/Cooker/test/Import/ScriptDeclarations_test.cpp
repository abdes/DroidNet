//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/ScriptSource.cpp

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/ScriptSource.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Scripting/Analysis/ScriptAnalyzer.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {
  using oxygen::cooker::test::DiagnosticSummary;
  using oxygen::cooker::test::HasErrorDiagnostic;

  auto Analyze(
    const std::string_view source, std::vector<ImportDiagnostic>& diagnostics)
    -> std::optional<std::vector<scripting::ScriptParamDeclaration>>
  {
    return AnalyzeScriptDeclarations(std::as_bytes(std::span(source)),
      "Scripts/orbit.luau", "input/orbit.luau", diagnostics);
  }

  NOLINT_TEST(ScriptDeclarationsTest, ReadsTheDeclarationsInOrder)
  {
    auto diagnostics = std::vector<ImportDiagnostic> {};

    const auto declarations = Analyze(R"lua(local Orbit = oxygen.script {
  params = {
    speed = oxygen.param.float { default = 1.5, min = 0, unit = "m/s" },
    orb = oxygen.param.asset { kind = "material" },
    target = oxygen.param.node {},
  },
}
return Orbit
)lua",
      diagnostics);

    ASSERT_HAS_VALUE(declarations) << DiagnosticSummary(diagnostics);
    EXPECT_TRUE(diagnostics.empty());
    ASSERT_EQ(declarations->size(), 3U);
    EXPECT_EQ(declarations->at(0).name, "speed");
    EXPECT_EQ(
      declarations->at(0).type, data::pak::scripting::ScriptParamType::kFloat);
    EXPECT_EQ(declarations->at(1).name, "orb");
    EXPECT_EQ(
      declarations->at(1).type, data::pak::scripting::ScriptParamType::kAsset);
    EXPECT_EQ(declarations->at(2).name, "target");
  }

  NOLINT_TEST(ScriptDeclarationsTest, AScriptWithoutDeclarationDeclaresNothing)
  {
    auto diagnostics = std::vector<ImportDiagnostic> {};

    const auto declarations = Analyze("return 'module'", diagnostics);

    ASSERT_HAS_VALUE(declarations) << DiagnosticSummary(diagnostics);
    EXPECT_TRUE(declarations->empty());
  }

  NOLINT_TEST(ScriptDeclarationsTest, RefusesADeclarationWithErrorsAndLocatesIt)
  {
    auto diagnostics = std::vector<ImportDiagnostic> {};

    const auto declarations = Analyze("local base = 1\n"
                                      "local S = oxygen.script {\n"
                                      "  params = { speed = oxygen.param.float "
                                      "{ default = base * 2 } },\n"
                                      "}\n"
                                      "return S\n",
      diagnostics);

    EXPECT_FALSE(declarations.has_value());
    const auto diagnostic = std::ranges::find_if(
      diagnostics, [](const ImportDiagnostic& d) -> bool {
        return d.code == "script.S0302";
      });
    ASSERT_NE(diagnostic, diagnostics.end()) << DiagnosticSummary(diagnostics);
    EXPECT_EQ(diagnostic->severity, ImportSeverity::kError);
    EXPECT_EQ(diagnostic->source_path, "input/orbit.luau");
    EXPECT_NE(
      diagnostic->message.find("Scripts/orbit.luau:3:"), std::string::npos);
  }

  NOLINT_TEST(ScriptDeclarationsTest, RefusesASourceThatDoesNotParse)
  {
    auto diagnostics = std::vector<ImportDiagnostic> {};

    const auto declarations = Analyze("return 22 +", diagnostics);

    EXPECT_FALSE(declarations.has_value());
    EXPECT_TRUE(HasErrorDiagnostic(diagnostics))
      << DiagnosticSummary(diagnostics);
  }

} // namespace
} // namespace oxygen::content::import::test
