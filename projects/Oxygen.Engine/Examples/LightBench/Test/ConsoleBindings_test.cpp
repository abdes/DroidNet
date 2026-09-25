//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <optional>
#include <stdexcept>
#include <string>

#include "LightBench/LightBenchConsoleBindings.h"

#include <Oxygen/Console/Console.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::examples::light_bench::testing {

NOLINT_TEST(LightBenchConsole, ValidatesArgumentsDispatchesAndRetiresCommands)
{
  console::Console console;
  std::optional<LightBenchPreset> pending;
  unsigned resets = 0;
  const auto context = console::CommandContext {
    .source = console::CommandSource::kAutomation,
    .shipping_build = true,
  };
  {
    LightBenchConsoleBindings bindings(console,
      {
        .select = [&](LightBenchPreset preset) { pending = preset; },
        .reset = [&] { ++resets; },
        .inspect = [] { return "owner snapshot"; },
      });
    EXPECT_EQ(console.Complete("lightbench.").size(), 3U);
    EXPECT_NE(console.Execute("lightbench.presets", context)
                .output.find("outdoor-daylight"),
      std::string::npos);
    EXPECT_EQ(console.Execute("lightbench.preset invalid", context).status,
      console::ExecutionStatus::kInvalidArguments);
    EXPECT_FALSE(pending);
    const auto queued = console.Execute("lightbench.preset indoor", context);
    EXPECT_EQ(queued.status, console::ExecutionStatus::kOk);
    EXPECT_TRUE(queued.output.starts_with("queued"));
    EXPECT_EQ(pending, LightBenchPreset::kIndoor);
    EXPECT_EQ(
      console.Execute("lightbench.preset", context).output, "owner snapshot");
    EXPECT_EQ(console.Execute("lightbench.reset extra", context).status,
      console::ExecutionStatus::kInvalidArguments);
    EXPECT_EQ(resets, 0U);
    EXPECT_EQ(console.Execute("lightbench.reset", context).status,
      console::ExecutionStatus::kOk);
    EXPECT_EQ(resets, 1U);
    // Applying a queued preset and resetting the real scene are covered by
    // console_preset_and_settings in the in-app Test Engine suite.
  }
  EXPECT_TRUE(console.Complete("lightbench.").empty());
  EXPECT_EQ(console.Execute("lightbench.reset", context).status,
    console::ExecutionStatus::kNotFound);
}

NOLINT_TEST(LightBenchConsole, FailedRegistrationRollsBackOnlyItsOwnCommands)
{
  console::Console console;
  const auto existing = console.RegisterCommand({
    .name = "lightbench.reset",
    .help = "Existing owner",
    .flags = console::CommandFlags::kNone,
    .handler = [](const auto&, const auto&) -> console::ExecutionResult {
      return { .output = "existing" };
    },
  });
  ASSERT_TRUE(existing.IsValid());
  const auto construct = [&] {
    LightBenchConsoleBindings binding(console,
      {
        .select = [](LightBenchPreset) { },
        .reset = [] { },
        .inspect = [] { return "ready"; },
      });
  };
  EXPECT_THROW(construct(), std::runtime_error);
  EXPECT_EQ(console.Complete("lightbench.").size(), 1U);
  EXPECT_EQ(console.Execute("lightbench.reset").output, "existing");
  EXPECT_EQ(console.Execute("lightbench.presets").status,
    console::ExecutionStatus::kNotFound);
}

} // namespace oxygen::examples::light_bench::testing
