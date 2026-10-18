//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <set>

#include <Oxygen/Scripting/Analysis/DiagnosticCatalog.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::scripting::testing {

namespace {

  // Every DiagnosticCode enumerator. A code added to the enum without a
  // catalog entry, or an entry without an enumerator, fails
  // CatalogMatchesCodes.
  constexpr auto kAllCodes = std::to_array<DiagnosticCode>({
    DiagnosticCode::kSyntaxError,
    DiagnosticCode::kCompileError,
    DiagnosticCode::kNoCompiler,
    DiagnosticCode::kCompileStopped,
    DiagnosticCode::kEmptySource,
    DiagnosticCode::kDeclarationNotTopLevelLocal,
    DiagnosticCode::kValueNotLiteral,
    DiagnosticCode::kDuplicateDeclaration,
    DiagnosticCode::kDeclarationNotReturned,
    DiagnosticCode::kDeclarationNotTable,
    DiagnosticCode::kUnknownField,
    DiagnosticCode::kParamNotDeclared,
    DiagnosticCode::kUnknownParamType,
    DiagnosticCode::kDuplicateParam,
    DiagnosticCode::kDefaultTypeMismatch,
    DiagnosticCode::kFieldNotApplicable,
    DiagnosticCode::kInvalidAssetKind,
    DiagnosticCode::kInvalidRange,
    DiagnosticCode::kFieldTypeMismatch,
    DiagnosticCode::kLintUnknown,
    DiagnosticCode::kLintUnknownGlobal,
    DiagnosticCode::kLintDeprecatedGlobal,
    DiagnosticCode::kLintGlobalUsedAsLocal,
    DiagnosticCode::kLintLocalShadow,
    DiagnosticCode::kLintSameLineStatement,
    DiagnosticCode::kLintMultiLineStatement,
    DiagnosticCode::kLintLocalUnused,
    DiagnosticCode::kLintFunctionUnused,
    DiagnosticCode::kLintImportUnused,
    DiagnosticCode::kLintBuiltinGlobalWrite,
    DiagnosticCode::kLintPlaceholderRead,
    DiagnosticCode::kLintUnreachableCode,
    DiagnosticCode::kLintUnknownType,
    DiagnosticCode::kLintForRange,
    DiagnosticCode::kLintUnbalancedAssignment,
    DiagnosticCode::kLintImplicitReturn,
    DiagnosticCode::kLintDuplicateLocal,
    DiagnosticCode::kLintFormatString,
    DiagnosticCode::kLintTableLiteral,
    DiagnosticCode::kLintUninitializedLocal,
    DiagnosticCode::kLintDuplicateFunction,
    DiagnosticCode::kLintDeprecatedApi,
    DiagnosticCode::kLintTableOperations,
    DiagnosticCode::kLintDuplicateCondition,
    DiagnosticCode::kLintMisleadingAndOr,
    DiagnosticCode::kLintCommentDirective,
    DiagnosticCode::kLintIntegerParsing,
    DiagnosticCode::kLintComparisonPrecedence,
    DiagnosticCode::kLintRedundantNativeAttribute,
    DiagnosticCode::kTypeMismatch,
    DiagnosticCode::kUnknownGlobal,
    DiagnosticCode::kUnknownMember,
    DiagnosticCode::kArgumentCountMismatch,
    DiagnosticCode::kNotCallable,
    DiagnosticCode::kOptionalValueAccess,
    DiagnosticCode::kDeprecatedApi,
    DiagnosticCode::kUnsupportedRequire,
    DiagnosticCode::kTypeError,
    DiagnosticCode::kTypeCheckIncomplete,
    DiagnosticCode::kNotAScript,
    DiagnosticCode::kLoadFailed,
    DiagnosticCode::kAttachFailed,
    DiagnosticCode::kOverrideRejected,
    DiagnosticCode::kRuntimeError,
  });

} // namespace

NOLINT_TEST(DiagnosticCatalogTest, CatalogMatchesCodes)
{
  const auto catalog = DiagnosticCatalog();
  ASSERT_EQ(catalog.size(), kAllCodes.size());
  for (const auto code : kAllCodes) {
    const auto* const info = Describe(code);
    ASSERT_NE(info, nullptr) << to_string(code);
    EXPECT_EQ(info->code, code);
    EXPECT_FALSE(info->title.empty()) << to_string(code);
    EXPECT_FALSE(info->explanation.empty()) << to_string(code);
  }
}

NOLINT_TEST(DiagnosticCatalogTest, CodesAreUniqueAndBelongToKnownFamilies)
{
  auto seen = std::set<uint16_t> {};
  for (const auto& info : DiagnosticCatalog()) {
    EXPECT_TRUE(seen.insert(static_cast<uint16_t>(info.code)).second)
      << to_string(info.code);
    const auto family = FamilyOf(info.code);
    EXPECT_GE(family, DiagnosticFamily::kSyntax) << to_string(info.code);
    EXPECT_LE(family, DiagnosticFamily::kRuntime) << to_string(info.code);
    EXPECT_NE(to_string(family), "__NotSupported__");
  }
}

NOLINT_TEST(DiagnosticCatalogTest, FamilyIsTheHundredsDigit)
{
  EXPECT_EQ(FamilyOf(DiagnosticCode::kSyntaxError), DiagnosticFamily::kSyntax);
  EXPECT_EQ(
    FamilyOf(DiagnosticCode::kCompileError), DiagnosticFamily::kCompile);
  EXPECT_EQ(FamilyOf(DiagnosticCode::kValueNotLiteral),
    DiagnosticFamily::kDeclarations);
  EXPECT_EQ(FamilyOf(DiagnosticCode::kNotAScript), DiagnosticFamily::kBinding);
  EXPECT_EQ(
    FamilyOf(DiagnosticCode::kRuntimeError), DiagnosticFamily::kRuntime);
}

NOLINT_TEST(DiagnosticCatalogTest, ParsesEveryCatalogedCode)
{
  for (const auto& info : DiagnosticCatalog()) {
    EXPECT_EQ(ParseDiagnosticCode(to_string(info.code)), info.code);
  }
  EXPECT_EQ(ParseDiagnosticCode("S0302"), DiagnosticCode::kValueNotLiteral);
}

NOLINT_TEST(DiagnosticCatalogTest, RejectsMalformedAndUncatalogedCodes)
{
  EXPECT_FALSE(ParseDiagnosticCode("").has_value());
  EXPECT_FALSE(ParseDiagnosticCode("S302").has_value());
  EXPECT_FALSE(ParseDiagnosticCode("X0302").has_value());
  EXPECT_FALSE(ParseDiagnosticCode("S03O2").has_value());
  EXPECT_FALSE(ParseDiagnosticCode("S0999").has_value());
  EXPECT_FALSE(ParseDiagnosticCode("S03020").has_value());
}

} // namespace oxygen::scripting::testing
