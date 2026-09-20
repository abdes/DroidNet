//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <gtest/gtest.h>
#include <stdexcept>
#include <thread>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Profiling/CpuProfileScope.h>
#include <Oxygen/Profiling/CpuScopeObserver.h>
#include <Oxygen/Profiling/ProfileScope.h>

namespace oxygen::profiling {
namespace {

  class RecordingObserver final : public CpuScopeObserver {
  public:
    std::vector<std::string> events;
    auto OnScopeBegin(const CpuProfileScopeDesc& desc) noexcept -> bool override
    {
      if (desc.label == "ignored") {
        return false;
      }
      events.push_back(desc.label);
      return true;
    }
    auto OnScopeEnd() noexcept -> void override { events.emplace_back("end"); }
  };

  TEST(ProfileScope, ObserverPairsSelectedNestedScopesAndDetaches)
  {
    RecordingObserver observer;
    {
      ScopedCpuScopeObserver registration(observer);
      CpuProfileScope outer("outer");
      CpuProfileScope ignored("ignored");
      CpuProfileScope inner("inner");
    }
    CpuProfileScope detached("detached");
    EXPECT_EQ(observer.events,
      (std::vector<std::string> { "outer", "inner", "end", "end" }));
  }

  TEST(ProfileScope, ObserverRegistrationIsThreadLocal)
  {
    RecordingObserver observer;
    ScopedCpuScopeObserver registration(observer);
    std::thread worker([] { CpuProfileScope unrelated("other thread"); });
    worker.join();
    EXPECT_TRUE(observer.events.empty());
  }

  TEST(ProfileScope, ObserverRejectsNestedRegistrationWithoutLosingOwner)
  {
    RecordingObserver observer;
    RecordingObserver other;
    ScopedCpuScopeObserver registration(observer);
    EXPECT_THROW(ScopedCpuScopeObserver nested(other), std::logic_error);
    {
      CpuProfileScope scope("still registered");
    }
    EXPECT_EQ(observer.events,
      (std::vector<std::string> { "still registered", "end" }));
    EXPECT_TRUE(other.events.empty());
  }

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
