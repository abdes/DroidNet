//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <gtest/gtest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Profiling/ProfileScope.h>

namespace oxygen::profiling {
namespace {

  TEST(ProfileScope, VarsFormatsNumbersAndStrings)
  {
    const auto vars = Vars(
      Var("id", 7), Var("name", std::string_view("Main")), Var("signed", -3));

    ASSERT_EQ(vars.size(), 3U);
    ASSERT_NE(vars[0].key, nullptr);
    EXPECT_EQ(std::string_view(vars[0].key.get()), "id");
    EXPECT_EQ(vars[0].value, "7");
    EXPECT_EQ(vars[1].value, "Main");
    EXPECT_EQ(vars[2].value, "-3");
  }

  TEST(ProfileScope, FormatScopeNameWithoutVariablesUsesBaseLabel)
  {
    const ProfileScopeDesc desc {
      .label = "Renderer.View",
    };

    EXPECT_EQ(FormatScopeName(desc), "Renderer.View");
  }

  TEST(ProfileScope, FormatScopeNameWithVariablesPreservesOrderAndEscapes)
  {
    const ProfileScopeDesc desc {
      .label = "Renderer.View",
      .variables
      = Vars(Var("id", 3), Var("name", std::string_view("Main,View]"))),
      .granularity = ProfileGranularity::kTelemetry,
      .category = ProfileCategory::kPass,
    };

    EXPECT_EQ(
      FormatScopeName(desc), R"(Renderer.View[id=3,name=Main\,View\]])");
  }

  TEST(ProfileScope, DefaultProfileColorUsesCategoryDefaults)
  {
    EXPECT_TRUE(DefaultProfileColor(ProfileCategory::kPass).IsSpecified());
    EXPECT_EQ(DefaultProfileColor(ProfileCategory::kGeneral).argb, 0U);
  }

} // namespace
} // namespace oxygen::profiling
