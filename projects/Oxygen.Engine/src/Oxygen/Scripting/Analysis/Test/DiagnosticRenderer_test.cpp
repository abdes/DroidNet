//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <Oxygen/Scripting/Analysis/DiagnosticRenderer.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::scripting::testing {

namespace {

  constexpr std::string_view kOrbitName = "Content/Scripts/Orbit.lua";
  constexpr std::string_view kOrbitSource = "local Orbit = oxygen.script {\n"
                                            "  params = {\n"
                                            "    speed = oxygen.param.float { "
                                            "default = base * 2 },\n"
                                            "  },\n"
                                            "}\n"
                                            "return Orbit\n";

  constexpr std::string_view kFollowName = "Content/Scripts/Follow.lua";
  constexpr uint32_t kFollowErrorLine = 18;
  constexpr uint32_t kFollowErrorColumn = 5;
  constexpr uint32_t kFollowErrorEndColumn = 23;

  constexpr std::string_view kBrokenName = "Content/Scripts/Broken.lua";
  constexpr std::string_view kBrokenSource = "local function step(dt)\n"
                                             "  local x = dt * 2\n"
                                             "  return x\n";

  constexpr std::string_view kSpinName = "Content/Scripts/Spin.lua";
  constexpr std::string_view kSpinSource = "local Spin = {}\n"
                                           "function Spin.on_gameplay() end\n"
                                           "return 42\n";

  auto Span(const std::string_view source, const SourcePosition begin,
    const SourcePosition end) -> SourceSpan
  {
    return SourceSpan {
      .source_name = std::string(source),
      .begin = begin,
      .end = end,
    };
  }

  auto FollowSource() -> std::string
  {
    auto text = std::string {};
    for (uint32_t line = 1; line < kFollowErrorLine; ++line) {
      text += "-- setup\n";
    }
    text += "    self.params.target:set_position(p)\n";
    return text;
  }

  auto RuntimeDiagnostic() -> ScriptDiagnostic
  {
    return ScriptDiagnostic {
      .severity = DiagnosticSeverity::kError,
      .code = DiagnosticCode::kRuntimeError,
      .message = "attempt to index nil with 'set_position'",
      .labels = {
        DiagnosticLabel {
          .span = Span(kFollowName,
            { .line = kFollowErrorLine, .column = kFollowErrorColumn },
            { .line = kFollowErrorLine, .column = kFollowErrorEndColumn }),
          .message = "this is nil",
          .primary = true,
        },
      },
      .notes = { "parameter `target` is not set on this slot" },
      .help = Suggestion {
        .message = "set Target in the node's Scripting section",
        .span = std::nullopt,
        .replacement = std::nullopt,
      },
      .slot = SlotContext {
        .node = "Camera Rig",
        .slot_index = 0,
        .hook = "on_gameplay",
      },
    };
  }

  class DiagnosticRendererTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
      sources_.Add(std::string(kOrbitName), std::string(kOrbitSource));
      sources_.Add(std::string(kFollowName), FollowSource());
      sources_.Add(std::string(kBrokenName), std::string(kBrokenSource));
      sources_.Add(std::string(kSpinName), std::string(kSpinSource));
    }

    [[nodiscard]] auto Sources() const -> const SourceTextCache&
    {
      return sources_;
    }

  private:
    SourceTextCache sources_;
  };

} // namespace

NOLINT_TEST_F(DiagnosticRendererTest, RendersDeclarationDiagnostic)
{
  const auto diagnostic = ScriptDiagnostic {
    .severity = DiagnosticSeverity::kError,
    .code = DiagnosticCode::kValueNotLiteral,
    .message = "parameter default must be a literal value",
    .labels = {
      DiagnosticLabel {
        .span = Span(kOrbitName, { .line = 3, .column = 44 },
          { .line = 3, .column = 52 }),
        .message = "computed here",
        .primary = true,
      },
    },
    .notes = { "declarations are read without running the script" },
    .help = Suggestion {
      .message = "write the value, for example `default = 3.2`",
      .span = std::nullopt,
      .replacement = std::nullopt,
    },
    .slot = std::nullopt,
  };

  EXPECT_EQ(RenderText(diagnostic, Sources()),
    "error[S0302]: parameter default must be a literal value\n"
    " --> Content/Scripts/Orbit.lua:3:44\n"
    "  |\n"
    "3 |     speed = oxygen.param.float { default = base * 2 },\n"
    "  |                                            ^^^^^^^^ computed here\n"
    "  |\n"
    "  = note: declarations are read without running the script\n"
    "  = help: write the value, for example `default = 3.2`\n");
}

NOLINT_TEST_F(DiagnosticRendererTest, RendersRuntimeDiagnosticWithSlot)
{
  EXPECT_EQ(RenderText(RuntimeDiagnostic(), Sources()),
    "error[S0701]: attempt to index nil with 'set_position'\n"
    "  --> Content/Scripts/Follow.lua:18:5\n"
    "   |\n"
    "18 |     self.params.target:set_position(p)\n"
    "   |     ^^^^^^^^^^^^^^^^^^ this is nil\n"
    "   |\n"
    "   = note: in on_gameplay, node \"Camera Rig\", slot 1\n"
    "   = note: parameter `target` is not set on this slot\n"
    "   = help: set Target in the node's Scripting section\n");
}

NOLINT_TEST_F(DiagnosticRendererTest, MissingSourceRendersHeaderLocationNotes)
{
  const auto no_sources = SourceTextCache {};

  EXPECT_EQ(RenderText(RuntimeDiagnostic(), no_sources),
    "error[S0701]: attempt to index nil with 'set_position'\n"
    "  --> Content/Scripts/Follow.lua:18:5\n"
    "   = note: in on_gameplay, node \"Camera Rig\", slot 1\n"
    "   = note: parameter `target` is not set on this slot\n"
    "   = help: set Target in the node's Scripting section\n");
}

NOLINT_TEST_F(DiagnosticRendererTest, RendersMultiLineSpanInMargin)
{
  const auto diagnostic = ScriptDiagnostic {
    .severity = DiagnosticSeverity::kError,
    .code = DiagnosticCode::kSyntaxError,
    .message = "expected 'end' (to close 'function' at line 1), got <eof>",
    .labels = {
      DiagnosticLabel {
        .span = Span(kBrokenName, { .line = 4, .column = 1 },
          { .line = 4, .column = 1 }),
        .message = "expected `end` here",
        .primary = true,
      },
      DiagnosticLabel {
        .span = Span(kBrokenName, { .line = 1, .column = 1 },
          { .line = 3, .column = 11 }),
        .message = "function opened here",
        .primary = false,
      },
    },
    .notes = {},
    .help = std::nullopt,
    .slot = std::nullopt,
  };

  EXPECT_EQ(RenderText(diagnostic, Sources()),
    "error[S0101]: expected 'end' (to close 'function' at line 1), got <eof>\n"
    " --> Content/Scripts/Broken.lua:4:1\n"
    "  |\n"
    "1 | / local function step(dt)\n"
    "2 | |   local x = dt * 2\n"
    "3 | |   return x\n"
    "  | |__________- function opened here\n"
    "4 |\n"
    "  |   ^ expected `end` here\n");
}

NOLINT_TEST_F(DiagnosticRendererTest, RendersBindingDiagnostic)
{
  const auto diagnostic = ScriptDiagnostic {
    .severity = DiagnosticSeverity::kError,
    .code = DiagnosticCode::kNotAScript,
    .message = "the script does not return its `oxygen.script` table",
    .labels = {
      DiagnosticLabel {
        .span = Span(kSpinName, { .line = 3, .column = 1 },
          { .line = 3, .column = 10 }),
        .message = "returns a number",
        .primary = true,
      },
    },
    .notes = {},
    .help = Suggestion {
      .message = "return the table created by `oxygen.script`",
      .span = std::nullopt,
      .replacement = std::nullopt,
    },
    .slot = SlotContext {
      .node = "Spinner",
      .slot_index = 1,
      .hook = "load",
    },
  };

  EXPECT_EQ(RenderText(diagnostic, Sources()),
    "error[S0601]: the script does not return its `oxygen.script` table\n"
    " --> Content/Scripts/Spin.lua:3:1\n"
    "  |\n"
    "3 | return 42\n"
    "  | ^^^^^^^^^ returns a number\n"
    "  |\n"
    "  = note: in load, node \"Spinner\", slot 2\n"
    "  = help: return the table created by `oxygen.script`\n");
}

NOLINT_TEST_F(DiagnosticRendererTest, RendersCompileDiagnosticWithoutSpan)
{
  const auto diagnostic = ScriptDiagnostic {
    .severity = DiagnosticSeverity::kError,
    .code = DiagnosticCode::kNoCompiler,
    .message = "no compiler is registered for luau scripts",
    .labels = {},
    .notes = { "the source of Content/Scripts/Orbit.lua cannot run" },
    .help = std::nullopt,
    .slot = std::nullopt,
  };

  EXPECT_EQ(RenderText(diagnostic, Sources()),
    "error[S0202]: no compiler is registered for luau scripts\n"
    "  = note: the source of Content/Scripts/Orbit.lua cannot run\n");
}

NOLINT_TEST(SourceTextCacheTest, ReturnsLinesWithoutBreaks)
{
  auto sources = SourceTextCache {};
  sources.Add("a.lua", "one\r\ntwo\nthree");

  EXPECT_EQ(sources.Line("a.lua", 1), "one");
  EXPECT_EQ(sources.Line("a.lua", 2), "two");
  EXPECT_EQ(sources.Line("a.lua", 3), "three");
  EXPECT_FALSE(sources.Line("a.lua", 0).has_value());
  EXPECT_FALSE(sources.Line("a.lua", 4).has_value());
  EXPECT_FALSE(sources.Line("b.lua", 1).has_value());
}

} // namespace oxygen::scripting::testing
