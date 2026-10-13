//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include <Oxygen/Core/Meta/Scripting/ScriptCompileMode.h>
#include <Oxygen/Scripting/Analysis/LuauCompiler.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::scripting::testing {

namespace {

  using core::meta::scripting::ScriptCompileMode;

  constexpr std::string_view kSourceName = "Content/Scripts/Test.lua";

  auto Compile(const std::string_view source,
    const ScriptCompileMode mode = ScriptCompileMode::kDebug)
    -> LuauCompileOutput
  {
    return CompileLuau(LuauCompileInput {
      .source_name = kSourceName,
      .source = std::as_bytes(std::span(source)),
      .mode = mode,
    });
  }

} // namespace

NOLINT_TEST(LuauCompilerTest, CompilesValidSource)
{
  for (const auto mode :
    { ScriptCompileMode::kDebug, ScriptCompileMode::kOptimized }) {
    const auto output = Compile("local x = 1\nreturn x + 1\n", mode);

    EXPECT_TRUE(output.Succeeded());
    EXPECT_TRUE(output.diagnostics.empty());
  }
}

NOLINT_TEST(LuauCompilerTest, ReportsEverySyntaxErrorWithItsSpan)
{
  const auto output = Compile("local x = \nlocal y = )\n");

  EXPECT_FALSE(output.Succeeded());
  ASSERT_FALSE(output.diagnostics.empty());
  for (const auto& diagnostic : output.diagnostics) {
    EXPECT_EQ(diagnostic.code, DiagnosticCode::kSyntaxError);
    const auto* const span = diagnostic.PrimarySpan();
    ASSERT_NE(span, nullptr);
    EXPECT_EQ(span->source_name, kSourceName);
    EXPECT_GE(span->begin.line, 1U);
  }
}

NOLINT_TEST(LuauCompilerTest, ReportsCompileErrorWithItsSpan)
{
  // Luau limits a function to 200 local variables; the 201st cannot be
  // allocated, which the parser accepts and the compiler rejects.
  constexpr uint32_t kLocalCount = 201;
  auto source = std::string {};
  for (uint32_t i = 1; i <= kLocalCount; ++i) {
    source += fmt::format("local v{} = {}\n", i, i);
  }

  const auto output = Compile(source);

  EXPECT_FALSE(output.Succeeded());
  ASSERT_EQ(output.diagnostics.size(), 1U);
  const auto& diagnostic = output.diagnostics.front();
  EXPECT_EQ(diagnostic.code, DiagnosticCode::kCompileError);
  EXPECT_FALSE(diagnostic.message.empty());
  const auto* const span = diagnostic.PrimarySpan();
  ASSERT_NE(span, nullptr);
  EXPECT_EQ(span->source_name, kSourceName);
  EXPECT_GE(span->begin.line, 1U);
}

NOLINT_TEST(LuauCompilerTest, ReportsEmptySource)
{
  const auto output = Compile("");

  EXPECT_FALSE(output.Succeeded());
  ASSERT_EQ(output.diagnostics.size(), 1U);
  EXPECT_EQ(output.diagnostics.front().code, DiagnosticCode::kEmptySource);
  const auto* const span = output.diagnostics.front().PrimarySpan();
  ASSERT_NE(span, nullptr);
  EXPECT_EQ(span->source_name, kSourceName);
}

} // namespace oxygen::scripting::testing
