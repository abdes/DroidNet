//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <type_traits>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Meta/Scripting/ScriptCompileMode.h>

namespace {

using oxygen::core::meta::scripting::ScriptCompileMode;
using oxygen::core::meta::scripting::to_string;

NOLINT_TEST(ScriptCompileModeTest, StringMappingIsStable)
{
  EXPECT_EQ(to_string(ScriptCompileMode::kDebug), "debug");
  EXPECT_EQ(to_string(ScriptCompileMode::kOptimized), "optimized");
}

NOLINT_TEST(ScriptCompileModeTest, UnknownValueUsesFallbackString)
{
  constexpr auto kUnknown = static_cast<ScriptCompileMode>(0xFFU);
  EXPECT_EQ(to_string(kUnknown), "__Unknown__");
}

NOLINT_TEST(ScriptCompileModeTest, EnumIsByteSized)
{
  EXPECT_EQ(sizeof(std::underlying_type_t<ScriptCompileMode>), sizeof(uint8_t));
}

} // namespace
