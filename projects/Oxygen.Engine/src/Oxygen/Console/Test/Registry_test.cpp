//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Console/Console.h>

namespace {

using oxygen::console::CommandFlags;
using oxygen::console::CompletionKind;
using oxygen::console::Console;
using oxygen::console::CVarFlags;
using oxygen::console::ExecutionStatus;

NOLINT_TEST(ConsoleRegistry, ListSymbolsReturnsAllVisibleSymbols)
{
  Console console {};
  ASSERT_TRUE(console
      .RegisterCVar({
        .name = "sys.val",
        .help = "Val",
        .default_value = int64_t { 0 },
        .flags = CVarFlags::kNone,
        .min_value = std::nullopt,
        .max_value = std::nullopt,
      })
      .IsValid());

  ASSERT_TRUE(console
      .RegisterCommand({ .name = "sys.cmd",
        .help = "Cmd",
        .flags = CommandFlags::kNone,
        .handler =
          [](const auto&, const auto&) {
            return oxygen::console::ExecutionResult { .status
              = ExecutionStatus::kOk,
              .exit_code = 0,
              .output = {},
              .error = {} };
          } })
      .IsValid());

  const auto symbols = console.ListSymbols(false);
  ASSERT_GE(symbols.size(), 2);

  bool found_cvar = false;
  bool found_cmd = false;
  for (const auto& s : symbols) {
    if (s.token == "sys.val" && s.kind == CompletionKind::kCVar) {
      found_cvar = true;
    }
    if (s.token == "sys.cmd" && s.kind == CompletionKind::kCommand) {
      found_cmd = true;
    }
  }
  EXPECT_TRUE(found_cvar);
  EXPECT_TRUE(found_cmd);
}

NOLINT_TEST(ConsoleRegistry, CaptureExecutionRecords)
{
  Console console {};
  EXPECT_EQ(console.Execute("help").status, ExecutionStatus::kOk);
  EXPECT_EQ(console.Execute("unknown.cmd").status, ExecutionStatus::kNotFound);

  const auto& records = console.GetExecutionRecords();
  constexpr size_t kExpectedRecords = 2;
  ASSERT_EQ(records.size(), kExpectedRecords);
  EXPECT_EQ(records[0].line, "help");
  EXPECT_EQ(records[1].line, "unknown.cmd");

  console.ClearExecutionRecords();
  EXPECT_TRUE(console.GetExecutionRecords().empty());
}

NOLINT_TEST(ConsoleRegistry, PreventDuplicateRegistrations)
{
  Console console {};
  const auto h1 = console.RegisterCVar({
    .name = "test",
    .help = "Test",
    .default_value = int64_t { 0 },
    .flags = CVarFlags::kNone,
    .min_value = std::nullopt,
    .max_value = std::nullopt,
  });
  const auto h2 = console.RegisterCVar({
    .name = "test",
    .help = "Test duplicate",
    .default_value = int64_t { 1 },
    .flags = CVarFlags::kNone,
    .min_value = std::nullopt,
    .max_value = std::nullopt,
  });

  EXPECT_TRUE(h1.IsValid());
  EXPECT_FALSE(h2.IsValid());
}

} // namespace
