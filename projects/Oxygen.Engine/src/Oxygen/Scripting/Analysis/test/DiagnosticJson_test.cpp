//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Scripting/Analysis/DiagnosticJson.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>
#include <Oxygen/Scripting/Analysis/test/ScriptDiagnostic_schema.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::scripting::testing {

namespace {

  using nlohmann::json;
  using nlohmann::json_schema::json_validator;

  constexpr std::string_view kSource = "Content/Scripts/Follow.lua";
  constexpr uint32_t kErrorLine = 18;
  constexpr uint32_t kErrorColumn = 5;
  constexpr uint32_t kErrorEndColumn = 23;
  constexpr uint32_t kBlockEndLine = 5;
  constexpr uint32_t kBlockEndColumn = 4;
  constexpr uint32_t kSlotIndex = 2;

  auto ErrorSpan() -> SourceSpan
  {
    return SourceSpan {
      .source_name = std::string(kSource),
      .begin = { .line = kErrorLine, .column = kErrorColumn },
      .end = { .line = kErrorLine, .column = kErrorEndColumn },
    };
  }

  auto FullDiagnostic() -> ScriptDiagnostic
  {
    return ScriptDiagnostic {
      .severity = DiagnosticSeverity::kError,
      .code = DiagnosticCode::kRuntimeError,
      .message = "attempt to index nil with 'set_position'",
      .labels = {
        DiagnosticLabel {
          .span = ErrorSpan(),
          .message = "this is nil",
          .primary = true,
        },
        DiagnosticLabel {
          .span = SourceSpan {
            .source_name = std::string(kSource),
            .begin = { .line = 3, .column = 1 },
            .end = { .line = kBlockEndLine, .column = kBlockEndColumn },
          },
          .message = {},
          .primary = false,
        },
      },
      .notes = { "called from update at Content/Scripts/Follow.lua:30" },
      .help = Suggestion {
        .message = "set Target in the node's Scripting section",
        .span = ErrorSpan(),
        .replacement = "self.params.target",
      },
      .slot = SlotContext {
        .node = "Camera Rig",
        .slot_index = kSlotIndex,
        .hook = "on_gameplay",
      },
    };
  }

  auto MinimalDiagnostic() -> ScriptDiagnostic
  {
    return ScriptDiagnostic {
      .severity = DiagnosticSeverity::kWarning,
      .code = DiagnosticCode::kNoCompiler,
      .message = "no compiler is registered for luau scripts",
      .labels = {},
      .notes = {},
      .help = std::nullopt,
      .slot = std::nullopt,
    };
  }

  auto MakeValidator() -> json_validator
  {
    auto validator = json_validator {};
    validator.set_root_schema(json::parse(kScriptDiagnosticSchema));
    return validator;
  }

  auto ValidationError(const std::string& text) -> std::string
  {
    try {
      MakeValidator().validate(json::parse(text));
    } catch (const std::exception& error) {
      return error.what();
    }
    return {};
  }

} // namespace

NOLINT_TEST(DiagnosticJsonTest, FullDiagnosticRoundTrips)
{
  const auto diagnostic = FullDiagnostic();

  const auto parsed = ParseDiagnosticJson(ToJson(diagnostic));

  ASSERT_TRUE(parsed.has_value()) << parsed.error();
  EXPECT_EQ(*parsed, diagnostic);
}

NOLINT_TEST(DiagnosticJsonTest, MinimalDiagnosticRoundTrips)
{
  const auto diagnostic = MinimalDiagnostic();

  const auto parsed = ParseDiagnosticJson(ToJson(diagnostic));

  ASSERT_TRUE(parsed.has_value()) << parsed.error();
  EXPECT_EQ(*parsed, diagnostic);
}

NOLINT_TEST(DiagnosticJsonTest, WrittenFormValidatesAgainstSchema)
{
  EXPECT_EQ(ValidationError(ToJson(FullDiagnostic())), "");
  EXPECT_EQ(ValidationError(ToJson(MinimalDiagnostic())), "");
}

NOLINT_TEST(DiagnosticJsonTest, WrittenFormNamesCodeAndFamily)
{
  const auto value = json::parse(ToJson(FullDiagnostic()));

  EXPECT_EQ(value.at("version"), kScriptDiagnosticJsonVersion);
  EXPECT_EQ(value.at("code"), "S0701");
  EXPECT_EQ(value.at("family"), "runtime");
  EXPECT_EQ(value.at("severity"), "error");
}

NOLINT_TEST(DiagnosticJsonTest, SchemaRejectsUnknownFields)
{
  auto value = json::parse(ToJson(MinimalDiagnostic()));
  value.emplace("extra", 1);

  EXPECT_NE(ValidationError(value.dump()), "");
}

NOLINT_TEST(DiagnosticJsonTest, RejectsUncatalogedCode)
{
  auto value = json::parse(ToJson(MinimalDiagnostic()));
  value.at("code") = "S0999";

  const auto parsed = ParseDiagnosticJson(value.dump());

  ASSERT_FALSE(parsed.has_value());
  EXPECT_NE(parsed.error().find("S0999"), std::string::npos);
}

NOLINT_TEST(DiagnosticJsonTest, RejectsOtherVersions)
{
  auto value = json::parse(ToJson(MinimalDiagnostic()));
  value.at("version") = 2;

  EXPECT_FALSE(ParseDiagnosticJson(value.dump()).has_value());
}

NOLINT_TEST(DiagnosticJsonTest, RejectsMalformedJson)
{
  EXPECT_FALSE(ParseDiagnosticJson("{").has_value());
  EXPECT_FALSE(ParseDiagnosticJson("{\"version\": 1}").has_value());
}

} // namespace oxygen::scripting::testing
