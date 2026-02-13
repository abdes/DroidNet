//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Console/Console.h>

namespace {

using oxygen::console::Console;
using oxygen::console::CVarDefinition;
using oxygen::console::CVarFlags;
using oxygen::console::ExecutionStatus;

NOLINT_TEST(ConsoleCVar, HandlesAllSupportedTypes)
{
  Console console {};

  // Bool
  ASSERT_TRUE(console
      .RegisterCVar({
        .name = "b.test",
        .help = "Bool test",
        .default_value = false,
        .flags = CVarFlags::kNone,
        .min_value = std::nullopt,
        .max_value = std::nullopt,
      })
      .IsValid());

  // Int
  const int64_t initial_int = 42;
  ASSERT_TRUE(console
      .RegisterCVar({
        .name = "i.test",
        .help = "Int test",
        .default_value = initial_int,
        .flags = CVarFlags::kNone,
        .min_value = std::nullopt,
        .max_value = std::nullopt,
      })
      .IsValid());

  // Float
  const double initial_float = 1.5;
  ASSERT_TRUE(console
      .RegisterCVar({
        .name = "f.test",
        .help = "Float test",
        .default_value = initial_float,
        .flags = CVarFlags::kNone,
        .min_value = std::nullopt,
        .max_value = std::nullopt,
      })
      .IsValid());

  // String
  ASSERT_TRUE(console
      .RegisterCVar({
        .name = "s.test",
        .help = "String test",
        .default_value = std::string("init"),
        .flags = CVarFlags::kNone,
        .min_value = std::nullopt,
        .max_value = std::nullopt,
      })
      .IsValid());

  EXPECT_EQ(console.Execute("b.test true").status, ExecutionStatus::kOk);
  EXPECT_EQ(std::get<bool>(console.FindCVar("b.test")->current_value), true);

  const int64_t target_int = 100;
  EXPECT_EQ(console.Execute("i.test 100").status, ExecutionStatus::kOk);
  EXPECT_EQ(
    std::get<int64_t>(console.FindCVar("i.test")->current_value), target_int);

  const double target_float = 2.25;
  EXPECT_EQ(console.Execute("f.test 2.25").status, ExecutionStatus::kOk);
  EXPECT_EQ(
    std::get<double>(console.FindCVar("f.test")->current_value), target_float);

  EXPECT_EQ(
    console.Execute("s.test \"hello world\"").status, ExecutionStatus::kOk);
  EXPECT_EQ(std::get<std::string>(console.FindCVar("s.test")->current_value),
    "hello world");
}

NOLINT_TEST(ConsoleCVar, RejectsInvalidTypeAndClampsBounds)
{
  Console console {};

  const int64_t default_lights = 4;
  const double min_lights = 1.0;
  const double max_lights = 8.0;
  const auto int_handle = console.RegisterCVar(CVarDefinition {
    .name = "sys.max_lights",
    .help = "Maximum visible lights",
    .default_value = default_lights,
    .flags = CVarFlags::kNone,
    .min_value = min_lights,
    .max_value = max_lights,
  });
  ASSERT_TRUE(int_handle.IsValid());

  {
    const auto result = console.Execute("sys.max_lights 999");
    EXPECT_EQ(result.status, ExecutionStatus::kOk);
    const auto snapshot = console.FindCVar("sys.max_lights");
    ASSERT_NE(snapshot, nullptr);
    ASSERT_TRUE(std::holds_alternative<int64_t>(snapshot->current_value));
    EXPECT_EQ(std::get<int64_t>(snapshot->current_value),
      static_cast<int64_t>(max_lights));
  }

  {
    const auto result = console.Execute("sys.max_lights -5");
    EXPECT_EQ(result.status, ExecutionStatus::kOk);
    const auto snapshot = console.FindCVar("sys.max_lights");
    ASSERT_NE(snapshot, nullptr);
    ASSERT_TRUE(std::holds_alternative<int64_t>(snapshot->current_value));
    EXPECT_EQ(std::get<int64_t>(snapshot->current_value),
      static_cast<int64_t>(min_lights));
  }

  {
    const auto result = console.Execute("sys.max_lights nope");
    EXPECT_EQ(result.status, ExecutionStatus::kInvalidArguments);
  }
}

NOLINT_TEST(ConsoleCVar, ReadOnlyCVarsCannotBeModified)
{
  Console console {};
  ASSERT_TRUE(console
      .RegisterCVar({
        .name = "sys.version",
        .help = "Engine version",
        .default_value = std::string("1.0.0"),
        .flags = CVarFlags::kReadOnly,
        .min_value = std::nullopt,
        .max_value = std::nullopt,
      })
      .IsValid());

  const auto result = console.Execute("sys.version \"2.0.0\"");
  EXPECT_EQ(result.status, ExecutionStatus::kDenied);
  EXPECT_EQ(
    std::get<std::string>(console.FindCVar("sys.version")->current_value),
    "1.0.0");
}

NOLINT_TEST(ConsoleCVar, LatchedCVarsUpdateOnApply)
{
  Console console {};
  ASSERT_TRUE(console
      .RegisterCVar({
        .name = "r.latched",
        .help = "Latched test",
        .default_value = int64_t { 0 },
        .flags = CVarFlags::kLatched,
        .min_value = std::nullopt,
        .max_value = std::nullopt,
      })
      .IsValid());

  EXPECT_EQ(console.Execute("r.latched 1").status, ExecutionStatus::kOk);
  EXPECT_EQ(std::get<int64_t>(console.FindCVar("r.latched")->current_value), 0);
  ASSERT_TRUE(console.FindCVar("r.latched")->latched_value.has_value());
  EXPECT_EQ(
    std::get<int64_t>(*console.FindCVar("r.latched")->latched_value), 1);

  EXPECT_EQ(console.ApplyLatchedCVars(), 1U);
  EXPECT_EQ(std::get<int64_t>(console.FindCVar("r.latched")->current_value), 1);
}

} // namespace
