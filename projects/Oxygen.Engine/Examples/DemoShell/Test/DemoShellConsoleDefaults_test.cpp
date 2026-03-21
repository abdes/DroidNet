//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Console/Console.h>
#include <Oxygen/Console/CVar.h>

#include "DemoShell/Internal/DemoShellConsoleDefaults.h"

namespace oxygen::examples::testing {

using oxygen::console::CVarDefinition;
using oxygen::console::CVarFlags;
using oxygen::console::Console;

NOLINT_TEST(DemoShellConsoleDefaults, EnablesGpuTimestampCaptureAndViewerWhenRegistered)
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

  const auto snapshot = console.FindCVar("rndr.gpu_timestamps.viewer");
  ASSERT_NE(snapshot, nullptr);
  ASSERT_TRUE(std::holds_alternative<bool>(snapshot->current_value));
  EXPECT_TRUE(std::get<bool>(snapshot->current_value));
}

NOLINT_TEST(DemoShellConsoleDefaults, IgnoresMissingGpuTimestampCVars)
{
  Console console {};

  internal::ApplyDemoShellConsoleDefaults(console);

  EXPECT_EQ(console.FindCVar("rndr.gpu_timestamps"), nullptr);
  EXPECT_EQ(console.FindCVar("rndr.gpu_timestamps.viewer"), nullptr);
}

} // namespace oxygen::examples::testing
