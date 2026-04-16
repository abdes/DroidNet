//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <source_location>
#include <span>

#include <Oxygen/Profiling/CpuProfileScope.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Tracy/Cpu.h>

namespace {

using oxygen::profiling::CpuProfileScope;
using oxygen::profiling::ProfileCategory;
using oxygen::profiling::Var;
using oxygen::profiling::Vars;

TEST(TracyCpuCache, ReusesSourceLocationForRepeatedStableName)
{
  const auto baseline = oxygen::tracy::cpu::CachedSourceLocationCountForTesting();
  auto storage = std::array<std::byte, 16> {};

  for (int i = 0; i < 64; ++i) {
    ASSERT_TRUE(oxygen::tracy::cpu::BeginZone(
      std::span { storage }, std::source_location::current(), "StableScope", 0));
    oxygen::tracy::cpu::EndZone(std::span { storage });
  }

  EXPECT_EQ(
    oxygen::tracy::cpu::CachedSourceLocationCountForTesting(), baseline + 1U);
}

TEST(TracyCpuCache, CpuProfileVariablesDoNotGrowSourceLocationCache)
{
  const auto baseline = oxygen::tracy::cpu::CachedSourceLocationCountForTesting();

  for (int i = 0; i < 64; ++i) {
    CpuProfileScope scope("Profiling.VariableScope", ProfileCategory::kGeneral,
      Vars(Var("iteration", i)));
  }

  EXPECT_EQ(
    oxygen::tracy::cpu::CachedSourceLocationCountForTesting(), baseline + 1U);
}

} // namespace
