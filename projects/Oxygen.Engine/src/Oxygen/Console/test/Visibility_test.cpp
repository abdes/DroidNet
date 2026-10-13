//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <optional>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Console/CVar.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::commands::ExecutionStatus;
using oxygen::console::Console;
using oxygen::console::CVarFlags;

NOLINT_TEST(ConsoleVisibility, FiltersHiddenCVarsFromPublicListings)
{
  oxygen::commands::CommandRegistry command_registry;
  Console console { oxygen::observer_ptr { &command_registry } };
  ASSERT_TRUE(console
      .RegisterCVar({
        .name = "sys.visible",
        .help = "Visible",
        .default_value = int64_t { 1 },
        .flags = CVarFlags::kNone,
        .min_value = std::nullopt,
        .max_value = std::nullopt,
      })
      .IsValid());
  ASSERT_TRUE(console
      .RegisterCVar({
        .name = "sys.hidden",
        .help = "Hidden",
        .default_value = int64_t { 1 },
        .flags = CVarFlags::kHidden,
        .min_value = std::nullopt,
        .max_value = std::nullopt,
      })
      .IsValid());

  // Completion should not find hidden
  const auto completions = console.Complete("sys.");
  bool found_hidden = false;
  for (const auto& c : completions) {
    if (c.token == "sys.hidden") {
      found_hidden = true;
    }
  }
  EXPECT_FALSE(found_hidden);

  // List commands should not show hidden
  const auto list_result = console.Execute("list cvars");
  EXPECT_EQ(list_result.status, ExecutionStatus::kOk);
  EXPECT_EQ(list_result.output.find("sys.hidden"), std::string::npos);
  EXPECT_NE(list_result.output.find("sys.visible"), std::string::npos);

  // Help should not find hidden
  EXPECT_EQ(
    console.Execute("help sys.hidden").status, ExecutionStatus::kNotFound);

  // But execution should still work if you know the name
  EXPECT_EQ(console.Execute("sys.hidden 0").status, ExecutionStatus::kOk);
  const auto snapshot = console.FindCVar("sys.hidden");
  ASSERT_NE(snapshot, nullptr);
  EXPECT_EQ(std::get<int64_t>(snapshot->current_value), 0);
}

} // namespace
