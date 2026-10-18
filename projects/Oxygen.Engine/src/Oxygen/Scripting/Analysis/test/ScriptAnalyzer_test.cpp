//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Scripting/Analysis/ScriptAnalyzer.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::Vec3;
using oxygen::data::AssetType;
using oxygen::data::ScriptAssetParam;
using oxygen::data::ScriptNodeParam;
using oxygen::data::pak::scripting::ScriptParamType;
using oxygen::scripting::AnalysisLevel;
using oxygen::scripting::DiagnosticCode;
using oxygen::scripting::ScriptAnalysis;
using oxygen::scripting::ScriptAnalyzer;
using oxygen::scripting::SourcePosition;
using testing::ElementsAre;
using testing::IsEmpty;
using testing::SizeIs;

constexpr std::string_view kSourceName = "Content/Scripts/Test.lua";

class ScriptAnalyzerTest : public testing::Test {
protected:
  [[nodiscard]] auto Analyze(const std::string_view text) const
    -> ScriptAnalysis
  {
    return analyzer_.Analyze({
      .source_name = kSourceName,
      .source = std::as_bytes(std::span(text.data(), text.size())),
      .level = AnalysisLevel::kDeclarations,
    });
  }

  //! Analyzes a script that declares `params_body` inside `params = { }`.
  [[nodiscard]] auto AnalyzeParams(const std::string_view params_body) const
    -> ScriptAnalysis
  {
    const auto text = std::string("local Script = oxygen.script {\n"
                                  "  params = {\n")
      + std::string(params_body) + "\n  },\n}\n\nreturn Script\n";
    return Analyze(text);
  }

  [[nodiscard]] static auto Codes(const ScriptAnalysis& analysis)
    -> std::vector<DiagnosticCode>
  {
    auto codes = std::vector<DiagnosticCode> {};
    for (const auto& diagnostic : analysis.diagnostics) {
      codes.push_back(diagnostic.code);
    }
    return codes;
  }

private:
  ScriptAnalyzer analyzer_;
};

//! The recommended declaration extracts every parameter, in order, with its
//! type, default, asset kind and hints.
NOLINT_TEST_F(ScriptAnalyzerTest, ExtractsRecommendedDeclaration)
{
  // Arrange
  constexpr std::string_view kScript = R"lua(local Orbit = oxygen.script {
  params = {
    speed = oxygen.param.float { default = 1.6, min = 0, max = 10,
      unit = "m/s", description = "Orbit speed" },
    target = oxygen.param.node { description = "Node to orbit around" },
    orb_material = oxygen.param.asset { kind = "material" },
    toggle = oxygen.param.asset { kind = "input_action" },
    tint = oxygen.param.vec3 { default = { 1, 1, 1 }, color = true },
    mode = oxygen.param.string { default = "loop", options = { "loop", "once" } },
    count = oxygen.param.int { default = -3, step = 1 },
    enabled = oxygen.param.bool { default = true },
  },
}

function Orbit:on_start() end
function Orbit:on_gameplay(dt) end

return Orbit
)lua";

  // Act
  const auto analysis = Analyze(kScript);

  // Assert
  EXPECT_THAT(analysis.diagnostics, IsEmpty());
  EXPECT_TRUE(analysis.has_declaration);
  ASSERT_THAT(analysis.declarations, SizeIs(8));
  const auto& d = analysis.declarations;

  EXPECT_EQ(d.at(0).name, "speed");
  EXPECT_EQ(d.at(0).type, ScriptParamType::kFloat);
  EXPECT_FLOAT_EQ(std::get<float>(d.at(0).default_value), 1.6F);
  EXPECT_EQ(d.at(0).min, 0.0);
  EXPECT_EQ(d.at(0).max, 10.0);
  EXPECT_EQ(d.at(0).unit, "m/s");
  EXPECT_EQ(d.at(0).description, "Orbit speed");
  EXPECT_EQ(d.at(0).span.begin, (SourcePosition { .line = 3, .column = 5 }));

  EXPECT_EQ(d.at(1).type, ScriptParamType::kNode);
  EXPECT_EQ(
    std::get<ScriptNodeParam>(d.at(1).default_value), ScriptNodeParam {});

  EXPECT_EQ(d.at(2).type, ScriptParamType::kAsset);
  EXPECT_EQ(std::get<ScriptAssetParam>(d.at(2).default_value).asset_type,
    AssetType::kMaterial);
  EXPECT_TRUE(std::get<ScriptAssetParam>(d.at(2).default_value).key.IsNil());
  EXPECT_EQ(std::get<ScriptAssetParam>(d.at(3).default_value).asset_type,
    AssetType::kInputAction);

  EXPECT_EQ(std::get<Vec3>(d.at(4).default_value), Vec3(1.0F, 1.0F, 1.0F));
  EXPECT_TRUE(d.at(4).color);

  EXPECT_EQ(std::get<std::string>(d.at(5).default_value), "loop");
  EXPECT_THAT(d.at(5).options, ElementsAre("loop", "once"));

  EXPECT_EQ(std::get<int32_t>(d.at(6).default_value), -3);
  EXPECT_EQ(d.at(6).step, 1.0);

  EXPECT_TRUE(std::get<bool>(d.at(7).default_value));
}

//! A script without `oxygen.script` is a module: no declaration, no error.
NOLINT_TEST_F(ScriptAnalyzerTest, ModuleWithoutDeclarationIsValid)
{
  const auto analysis = Analyze("local M = {}\nfunction M.f() end\nreturn M\n");

  EXPECT_FALSE(analysis.has_declaration);
  EXPECT_THAT(analysis.declarations, IsEmpty());
  EXPECT_THAT(analysis.diagnostics, IsEmpty());
}

//! Parameters without a default take their type's zero value.
NOLINT_TEST_F(ScriptAnalyzerTest, MissingDefaultsUseZeroValues)
{
  const auto analysis = AnalyzeParams(R"lua(
    a = oxygen.param.bool {},
    b = oxygen.param.int {},
    c = oxygen.param.string {},
    d = oxygen.param.vec3 {},)lua");

  ASSERT_THAT(analysis.declarations, SizeIs(4));
  EXPECT_FALSE(std::get<bool>(analysis.declarations.at(0).default_value));
  EXPECT_EQ(std::get<int32_t>(analysis.declarations.at(1).default_value), 0);
  EXPECT_THAT(std::get<std::string>(analysis.declarations.at(2).default_value),
    IsEmpty());
  EXPECT_EQ(
    std::get<Vec3>(analysis.declarations.at(3).default_value), Vec3(0.0F));
}

//! A syntax error is reported with its 1-based location and stops analysis.
NOLINT_TEST_F(ScriptAnalyzerTest, SyntaxErrorIsReported)
{
  const auto analysis = Analyze("local x =\n");

  EXPECT_FALSE(analysis.has_declaration);
  EXPECT_TRUE(analysis.HasErrors());
  ASSERT_THAT(Codes(analysis), ElementsAre(DiagnosticCode::kSyntaxError));
  const auto* const span = analysis.diagnostics.front().PrimarySpan();
  ASSERT_NE(span, nullptr);
  EXPECT_EQ(span->source_name, kSourceName);
  EXPECT_GE(span->begin.line, 1U);
}

//! A computed field value points at the expression, as in the design's
//! example diagnostic.
NOLINT_TEST_F(ScriptAnalyzerTest, ComputedValueIsRejectedAtTheExpression)
{
  // Arrange
  constexpr std::string_view kLine
    = "    speed = oxygen.param.float { default = base * 2 },";
  const auto text = std::string("local base = 1\n"
                                "local Orbit = oxygen.script {\n"
                                "  params = {\n")
    + std::string(kLine) + "\n  },\n}\nreturn Orbit\n";

  // Act
  const auto analysis = Analyze(text);

  // Assert
  ASSERT_THAT(Codes(analysis), ElementsAre(DiagnosticCode::kValueNotLiteral));
  const auto& diagnostic = analysis.diagnostics.front();
  EXPECT_EQ(diagnostic.message, "parameter default must be a literal value");
  const auto* const span = diagnostic.PrimarySpan();
  ASSERT_NE(span, nullptr);
  const auto column = static_cast<uint32_t>(kLine.find("base")) + 1U;
  EXPECT_EQ(span->begin, (SourcePosition { .line = 4, .column = column }));
  EXPECT_EQ(span->end.column, column + 8U);
  ASSERT_TRUE(diagnostic.help.has_value());
  EXPECT_THAT(diagnostic.notes, SizeIs(1));
  EXPECT_THAT(analysis.declarations, IsEmpty());
  EXPECT_TRUE(analysis.has_declaration);
  EXPECT_EQ(oxygen::scripting::FormatSummary(diagnostic),
    "Content/Scripts/Test.lua:4:" + std::to_string(column)
      + ": error[S0302]: parameter default must be a literal value");
}

//! `oxygen.script` must initialize a top-level local.
NOLINT_TEST_F(ScriptAnalyzerTest, DeclarationOutsideTopLevelLocalIsRejected)
{
  const auto global = Analyze("Script = oxygen.script { params = {} }\n"
                              "return Script\n");
  const auto nested = Analyze("local function make()\n"
                              "  local S = oxygen.script { params = {} }\n"
                              "  return S\n"
                              "end\n"
                              "return make()\n");

  EXPECT_THAT(
    Codes(global), ElementsAre(DiagnosticCode::kDeclarationNotTopLevelLocal));
  EXPECT_THAT(
    Codes(nested), ElementsAre(DiagnosticCode::kDeclarationNotTopLevelLocal));
}

//! A second declaration is reported with a label on the first.
NOLINT_TEST_F(ScriptAnalyzerTest, DuplicateDeclarationIsRejected)
{
  const auto analysis = Analyze("local A = oxygen.script { params = {} }\n"
                                "local B = oxygen.script { params = {} }\n"
                                "return A\n");

  ASSERT_THAT(
    Codes(analysis), ElementsAre(DiagnosticCode::kDuplicateDeclaration));
  const auto& labels = analysis.diagnostics.front().labels;
  ASSERT_THAT(labels, SizeIs(2));
  EXPECT_EQ(labels.at(0).span.begin.line, 2U);
  EXPECT_FALSE(labels.at(1).primary);
  EXPECT_EQ(labels.at(1).span.begin.line, 1U);
}

//! The chunk must return the declared local.
NOLINT_TEST_F(ScriptAnalyzerTest, DeclarationNotReturnedIsRejected)
{
  const auto other = Analyze("local S = oxygen.script { params = {} }\n"
                             "return {}\n");
  const auto none = Analyze("local S = oxygen.script { params = {} }\n");

  EXPECT_THAT(
    Codes(other), ElementsAre(DiagnosticCode::kDeclarationNotReturned));
  EXPECT_THAT(
    Codes(none), ElementsAre(DiagnosticCode::kDeclarationNotReturned));
}

//! The declaration and its `params` must be table constructors.
NOLINT_TEST_F(ScriptAnalyzerTest, NonTableDeclarationIsRejected)
{
  const auto call = Analyze("local t = {}\n"
                            "local S = oxygen.script(t)\n"
                            "return S\n");
  const auto params = Analyze("local S = oxygen.script { params = 3 }\n"
                              "return S\n");

  EXPECT_THAT(Codes(call), ElementsAre(DiagnosticCode::kDeclarationNotTable));
  EXPECT_THAT(Codes(params), ElementsAre(DiagnosticCode::kDeclarationNotTable));
}

//! Unknown declaration and parameter fields are rejected.
NOLINT_TEST_F(ScriptAnalyzerTest, UnknownFieldsAreRejected)
{
  const auto script_field
    = Analyze("local S = oxygen.script { params = {}, name = \"x\" }\n"
              "return S\n");
  const auto param_field
    = AnalyzeParams("    speed = oxygen.param.float { maximum = 3 },");

  EXPECT_THAT(Codes(script_field), ElementsAre(DiagnosticCode::kUnknownField));
  EXPECT_THAT(Codes(param_field), ElementsAre(DiagnosticCode::kUnknownField));
}

//! Every parameter is an `oxygen.param.<type>` call of a known type.
NOLINT_TEST_F(ScriptAnalyzerTest, ParameterShapeIsChecked)
{
  const auto not_declared = AnalyzeParams("    speed = 3,");
  const auto unknown_type
    = AnalyzeParams("    speed = oxygen.param.double {},");

  EXPECT_THAT(
    Codes(not_declared), ElementsAre(DiagnosticCode::kParamNotDeclared));
  EXPECT_THAT(
    Codes(unknown_type), ElementsAre(DiagnosticCode::kUnknownParamType));
}

//! Repeated names are rejected; a name has no length limit.
NOLINT_TEST_F(ScriptAnalyzerTest, ParameterNamesAreChecked)
{
  const auto long_name = AnalyzeParams(
    "    " + std::string(200, 'a') + " = oxygen.param.bool {},");
  const auto duplicate = AnalyzeParams("    a = oxygen.param.bool {},\n"
                                       "    a = oxygen.param.int {},");

  EXPECT_THAT(Codes(long_name), IsEmpty());
  ASSERT_THAT(Codes(duplicate), ElementsAre(DiagnosticCode::kDuplicateParam));
  EXPECT_THAT(duplicate.diagnostics.front().labels, SizeIs(2));
}

//! Defaults must match the parameter type.
NOLINT_TEST_F(ScriptAnalyzerTest, DefaultTypeMismatchIsRejected)
{
  const auto fractional_int
    = AnalyzeParams("    n = oxygen.param.int { default = 3.5 },");
  const auto short_vector
    = AnalyzeParams("    v = oxygen.param.vec3 { default = { 1, 2 } },");
  const auto string_bool
    = AnalyzeParams("    b = oxygen.param.bool { default = \"yes\" },");

  EXPECT_THAT(
    Codes(fractional_int), ElementsAre(DiagnosticCode::kDefaultTypeMismatch));
  EXPECT_THAT(
    Codes(short_vector), ElementsAre(DiagnosticCode::kDefaultTypeMismatch));
  EXPECT_THAT(
    Codes(string_bool), ElementsAre(DiagnosticCode::kDefaultTypeMismatch));
}

//! Fields only apply to the types they describe; asset and node parameters
//! take no default.
NOLINT_TEST_F(ScriptAnalyzerTest, FieldsNotApplicableToTheTypeAreRejected)
{
  const auto range_on_string
    = AnalyzeParams("    s = oxygen.param.string { min = 1 },");
  const auto asset_default = AnalyzeParams(
    R"(    m = oxygen.param.asset { kind = "material", default = "x" },)");
  const auto node_default
    = AnalyzeParams("    n = oxygen.param.node { default = 1 },");

  EXPECT_THAT(
    Codes(range_on_string), ElementsAre(DiagnosticCode::kFieldNotApplicable));
  EXPECT_THAT(
    Codes(asset_default), ElementsAre(DiagnosticCode::kFieldNotApplicable));
  EXPECT_THAT(
    Codes(node_default), ElementsAre(DiagnosticCode::kFieldNotApplicable));
}

//! Asset parameters need a known kind.
NOLINT_TEST_F(ScriptAnalyzerTest, AssetKindIsRequired)
{
  const auto missing = AnalyzeParams("    m = oxygen.param.asset {},");
  const auto unknown
    = AnalyzeParams("    m = oxygen.param.asset { kind = \"texture\" },");

  EXPECT_THAT(Codes(missing), ElementsAre(DiagnosticCode::kInvalidAssetKind));
  EXPECT_THAT(Codes(unknown), ElementsAre(DiagnosticCode::kInvalidAssetKind));
}

//! String defaults and options have no length limit.
NOLINT_TEST_F(ScriptAnalyzerTest, LongStringsAreAccepted)
{
  const auto text = std::string(200, 'x');
  const auto long_default = AnalyzeParams(
    "    s = oxygen.param.string { default = \"" + text + "\" },");
  const auto long_option = AnalyzeParams(
    "    s = oxygen.param.string { options = { \"" + text + "\" } },");

  EXPECT_THAT(Codes(long_default), IsEmpty());
  EXPECT_THAT(Codes(long_option), IsEmpty());
}

//! Ranges, steps and options must agree with each other and the default.
NOLINT_TEST_F(ScriptAnalyzerTest, InconsistentRangesAreRejected)
{
  const auto inverted
    = AnalyzeParams("    f = oxygen.param.float { min = 2, max = 1 },");
  const auto out_of_range = AnalyzeParams(
    "    f = oxygen.param.float { default = 5, min = 0, max = 1 },");
  const auto bad_step
    = AnalyzeParams("    f = oxygen.param.float { step = 0 },");
  const auto not_an_option = AnalyzeParams(
    R"(    s = oxygen.param.string { default = "c", options = { "a" } },)");

  EXPECT_THAT(Codes(inverted), ElementsAre(DiagnosticCode::kInvalidRange));
  EXPECT_THAT(Codes(out_of_range), ElementsAre(DiagnosticCode::kInvalidRange));
  EXPECT_THAT(Codes(bad_step), ElementsAre(DiagnosticCode::kInvalidRange));
  EXPECT_THAT(Codes(not_an_option), ElementsAre(DiagnosticCode::kInvalidRange));
}

//! Hint fields must hold the right kind of literal.
NOLINT_TEST_F(ScriptAnalyzerTest, HintTypeMismatchIsRejected)
{
  const auto numeric_unit
    = AnalyzeParams("    f = oxygen.param.float { unit = 3 },");
  const auto string_color
    = AnalyzeParams("    c = oxygen.param.vec3 { color = \"yes\" },");

  EXPECT_THAT(
    Codes(numeric_unit), ElementsAre(DiagnosticCode::kFieldTypeMismatch));
  EXPECT_THAT(
    Codes(string_color), ElementsAre(DiagnosticCode::kFieldTypeMismatch));
}

//! Definitions in the shape the binding generator writes: an extern type, a
//! generic script type and the `oxygen` global.
constexpr std::string_view kDefinitions = R"luau(
declare extern type SceneNode with
  function get_name(self): string
  function set_local_position(self, position: vector): boolean
end

type OxygenScript<P> = {
  node: SceneNode,
  params: P,
  on_start: ((self: OxygenScript<P>) -> ())?,
  on_gameplay: ((self: OxygenScript<P>, dt: number) -> ())?,
  [string]: any,
}

declare oxygen: {
  script: <P>(declaration: { params: P }) -> OxygenScript<P>,
  param: {
    float: (options: { default: number?, min: number? }) -> number,
  },
  scene: {
    find_one: (path: string) -> SceneNode?,
  },
}
)luau";

class ScriptAnalyzerChecksTest : public ScriptAnalyzerTest {
protected:
  [[nodiscard]] auto Check(const std::string_view text,
    const AnalysisLevel level, const std::string_view definitions = {}) const
    -> ScriptAnalysis
  {
    return analyzer_.Analyze({
      .source_name = kSourceName,
      .source = std::as_bytes(std::span(text.data(), text.size())),
      .level = level,
      .type_definitions = definitions,
    });
  }

private:
  ScriptAnalyzer analyzer_;
};

constexpr std::string_view kTypedScript = R"lua(local Script = oxygen.script {
  params = {
    speed = oxygen.param.float { default = 1, min = 0 },
  },
}

function Script:on_start()
  self.angle = 0
end

function Script:on_gameplay(dt)
  self.angle += self.params.speed * dt
  self.node:set_local_position(vector.create(self.angle, 0, 0))
end

return Script
)lua";

//! A script that uses the API as declared checks clean, with `self.params`
//! typed by its declaration and script fields on `self` allowed.
NOLINT_TEST_F(ScriptAnalyzerChecksTest, ValidScriptChecksClean)
{
  const auto analysis
    = Check(kTypedScript, AnalysisLevel::kTypes, kDefinitions);

  EXPECT_THAT(analysis.diagnostics, IsEmpty());
  EXPECT_THAT(analysis.declarations, SizeIs(1));
}

//! The valid script also checks clean in strict mode.
NOLINT_TEST_F(ScriptAnalyzerChecksTest, ValidScriptChecksCleanWhenStrict)
{
  const auto text = std::string("--!strict\n") + std::string(kTypedScript);

  const auto analysis = Check(text, AnalysisLevel::kTypes, kDefinitions);

  EXPECT_THAT(analysis.diagnostics, IsEmpty());
}

//! A misspelled API member is a type error that names the member.
NOLINT_TEST_F(ScriptAnalyzerChecksTest, MisspelledApiMemberIsReported)
{
  constexpr std::string_view kScript
    = R"lua(local node = oxygen.scene.fnd_one("Player")
return node
)lua";

  const auto analysis = Check(kScript, AnalysisLevel::kTypes, kDefinitions);

  ASSERT_THAT(analysis.diagnostics, SizeIs(1));
  const auto& diagnostic = analysis.diagnostics.front();
  EXPECT_EQ(diagnostic.code, DiagnosticCode::kUnknownMember);
  EXPECT_NE(diagnostic.message.find("fnd_one"), std::string::npos);
  ASSERT_NE(diagnostic.PrimarySpan(), nullptr);
  EXPECT_EQ(diagnostic.PrimarySpan()->begin.line, 1U);
  EXPECT_TRUE(diagnostic.help.has_value());
}

//! In strict mode, parameter values carry their declared types into hook
//! bodies. Non-strict mode leaves unannotated hook parameters, `self`
//! included, untyped.
NOLINT_TEST_F(ScriptAnalyzerChecksTest, ParamTypesReachStrictHooks)
{
  constexpr std::string_view kScript = R"lua(--!strict
local Script = oxygen.script {
  params = {
    speed = oxygen.param.float { default = 1 },
  },
}

function Script:on_start()
  local label: string = self.params.speed
  print(label)
end

return Script
)lua";

  const auto analysis = Check(kScript, AnalysisLevel::kTypes, kDefinitions);

  EXPECT_THAT(Codes(analysis), ElementsAre(DiagnosticCode::kTypeMismatch));
}

//! Wrong argument types to an API function are reported.
NOLINT_TEST_F(ScriptAnalyzerChecksTest, WrongArgumentTypeIsReported)
{
  constexpr std::string_view kScript
    = R"lua(local node = oxygen.scene.find_one(42)
return node
)lua";

  const auto analysis = Check(kScript, AnalysisLevel::kTypes, kDefinitions);

  EXPECT_THAT(Codes(analysis), ElementsAre(DiagnosticCode::kTypeMismatch));
}

//! Linting reports Luau lint warnings as S04xx and knows `oxygen` without
//! definitions.
NOLINT_TEST_F(ScriptAnalyzerChecksTest, LintWarnsWithoutDefinitions)
{
  constexpr std::string_view kScript = R"lua(local function f()
  local unused = oxygen.time.delta_seconds()
end
return f
)lua";

  const auto analysis = Check(kScript, AnalysisLevel::kLint);

  ASSERT_THAT(Codes(analysis), ElementsAre(DiagnosticCode::kLintLocalUnused));
  EXPECT_EQ(analysis.diagnostics.front().severity,
    oxygen::scripting::DiagnosticSeverity::kWarning);
  EXPECT_EQ(to_string(analysis.diagnostics.front().code), "S0407");
}

//! Types without definitions fall back to linting: no type errors.
NOLINT_TEST_F(ScriptAnalyzerChecksTest, TypesWithoutDefinitionsOnlyLint)
{
  constexpr std::string_view kScript
    = R"lua(local node = oxygen.scene.fnd_one("Player")
return node
)lua";

  const auto analysis = Check(kScript, AnalysisLevel::kTypes);

  EXPECT_THAT(analysis.diagnostics, IsEmpty());
}

//! The declaration level does not lint or type-check.
NOLINT_TEST_F(ScriptAnalyzerChecksTest, DeclarationLevelSkipsChecks)
{
  constexpr std::string_view kScript
    = R"lua(local unused = oxygen.scene.fnd_one(1)
return 1
)lua";

  const auto analysis
    = Check(kScript, AnalysisLevel::kDeclarations, kDefinitions);

  EXPECT_THAT(analysis.diagnostics, IsEmpty());
}

//! Codes print as `S` and four digits.
NOLINT_TEST(ScriptDiagnosticTest, CodesPrintWithFourDigits)
{
  EXPECT_EQ(to_string(DiagnosticCode::kSyntaxError), "S0101");
  EXPECT_EQ(to_string(DiagnosticCode::kValueNotLiteral), "S0302");
}

} // namespace
