//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string_view>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scripting/Compilers/LuauScriptCompiler.h>

namespace {

using oxygen::scripting::CompileMode;
using oxygen::scripting::LuauScriptCompiler;

auto ToBytes(const std::string_view text) -> std::vector<uint8_t>
{
  std::vector<uint8_t> bytes;
  bytes.reserve(text.size());
  for (const auto ch : text) {
    bytes.push_back(static_cast<uint8_t>(ch));
  }
  return bytes;
}

NOLINT_TEST(LuauScriptCompilerTest, CompileValidSourceProducesBytecode)
{
  LuauScriptCompiler compiler;
  const auto source = ToBytes("local x = 1\nreturn x\n");

  const auto result = compiler.Compile(source, CompileMode::kDebug);

  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.HasBytecode());
  EXPECT_TRUE(result.diagnostics.empty());
}

NOLINT_TEST(LuauScriptCompilerTest, CompileInvalidSourceReturnsDiagnostics)
{
  LuauScriptCompiler compiler;
  const auto source = ToBytes("local x =\n");

  const auto result = compiler.Compile(source, CompileMode::kDebug);

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.HasBytecode());
  EXPECT_FALSE(result.diagnostics.empty());
}

} // namespace
