//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Console/CVar.h>
#include <Oxygen/Console/Console.h>

#include "DemoShell/Internal/DemoShellConsoleDefaults.h"

namespace oxygen::examples::testing {

using oxygen::console::Console;
using oxygen::console::CVarDefinition;
using oxygen::console::CVarFlags;
using oxygen::console::CVarValueOrigin;

NOLINT_TEST(
  DemoShellConsoleDefaults, EnablesGpuTimestampCaptureAndViewerWhenRegistered)
{
  Console console {};
  ASSERT_TRUE(console
      .RegisterCVar(CVarDefinition {
        .name = "rndr.gpu_timestamps",
        .help = "GPU timestamp profiling enabled",
        .default_value = false,
        .flags = CVarFlags::kNone,
        .min_value = std::nullopt,
        .max_value = std::nullopt,
      })
      .IsValid());
  ASSERT_TRUE(console
      .RegisterCVar(CVarDefinition {
        .name = "rndr.gpu_timestamps.viewer",
        .help = "GPU timeline viewer enabled",
        .default_value = false,
        .flags = CVarFlags::kNone,
        .min_value = std::nullopt,
        .max_value = std::nullopt,
      })
      .IsValid());

  internal::ApplyDemoShellConsoleDefaults(console);

  const auto profiler_snapshot = console.FindCVar("rndr.gpu_timestamps");
  ASSERT_NE(profiler_snapshot, nullptr);
  ASSERT_TRUE(std::holds_alternative<bool>(profiler_snapshot->current_value));
  EXPECT_TRUE(std::get<bool>(profiler_snapshot->current_value));
  EXPECT_EQ(profiler_snapshot->current_origin, CVarValueOrigin::kAppDefault);

  const auto snapshot = console.FindCVar("rndr.gpu_timestamps.viewer");
  ASSERT_NE(snapshot, nullptr);
  ASSERT_TRUE(std::holds_alternative<bool>(snapshot->current_value));
  EXPECT_TRUE(std::get<bool>(snapshot->current_value));
  EXPECT_EQ(snapshot->current_origin, CVarValueOrigin::kAppDefault);
}

NOLINT_TEST(DemoShellConsoleDefaults, IgnoresMissingGpuTimestampCVars)
{
  Console console {};

  internal::ApplyDemoShellConsoleDefaults(console);

  EXPECT_EQ(console.FindCVar("rndr.gpu_timestamps"), nullptr);
  EXPECT_EQ(console.FindCVar("rndr.gpu_timestamps.viewer"), nullptr);
}

NOLINT_TEST(DemoShellConsoleDefaults, DoesNotOverrideHigherPrecedenceValue)
{
  Console console {};
  ASSERT_TRUE(console
      .RegisterCVar(CVarDefinition {
        .name = "rndr.gpu_timestamps",
        .help = "GPU timestamp profiling enabled",
        .default_value = false,
        .flags = CVarFlags::kNone,
        .min_value = std::nullopt,
        .max_value = std::nullopt,
      })
      .IsValid());

  auto startup_plan = oxygen::console::ConsoleStartupPlan {};
  startup_plan.Set("rndr.gpu_timestamps", true);
  console.ApplyStartupPlan(startup_plan);
  ASSERT_EQ(console.Execute("rndr.gpu_timestamps false").status,
    oxygen::console::ExecutionStatus::kOk);

  internal::ApplyDemoShellConsoleDefaults(console);

  const auto snapshot = console.FindCVar("rndr.gpu_timestamps");
  ASSERT_NE(snapshot, nullptr);
  EXPECT_FALSE(std::get<bool>(snapshot->current_value));
  EXPECT_EQ(snapshot->current_origin, CVarValueOrigin::kRuntimeExplicit);
}

} // namespace oxygen::examples::testing
