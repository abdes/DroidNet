//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>
#include <stdexcept>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>

using oxygen::clap::Cli;
using oxygen::clap::CliBuilder;
using oxygen::clap::Command;
using oxygen::clap::CommandBuilder;
using oxygen::clap::Option;

using testing::_;
using testing::ElementsAre;
using testing::Eq;
using testing::SizeIs;

namespace {

auto MakeBefore_1() -> std::shared_ptr<Option>
{
  return Option::Positional("BEFORE_1")
    .About("first positional before rest")
    .WithValue<std::string>()
    .Build();
}

auto MakeBefore_2() -> std::shared_ptr<Option>
{
  return Option::Positional("BEFORE_2")
    .About("second positional before rest")
    .WithValue<std::string>()
    .Build();
}

auto MakeRest() -> std::shared_ptr<Option>
{
  return Option::Rest()
    .About("remaining positional arguments")
    .WithValue<std::string>()
    .Build();
}

auto MakeAfter_1() -> std::shared_ptr<Option>
{
  return Option::Positional("AFTER_1")
    .About("first positional after rest")
    .WithValue<std::string>()
    .Build();
}

NOLINT_TEST(
  PositionalArgumentsTest, RestPositional_AllArgumentsAssignedToRest_Succeeds)
{
  constexpr auto argc = 5;
  std::array<const char*, argc> argv { { "/usr/bin/test-program.exe", "r_1",
    "r_2", "r_3", "r_4" } };

  const std::shared_ptr<Command> default_command {
    CommandBuilder(Command::DEFAULT).WithPositionalArguments(MakeRest()).Build()
  };

  const std::unique_ptr<Cli> cli
    = CliBuilder().ProgramName("positional_args").WithCommand(default_command);
  const auto& matches = cli->Parse(argc, argv.data()).ovm;

  const auto& v_rest = matches.ValuesOf(Option::key_rest_);
  EXPECT_THAT(v_rest.size(), Eq(4));
  EXPECT_THAT(v_rest.at(0).GetAs<std::string>(), Eq("r_1"));
  EXPECT_THAT(v_rest.at(1).GetAs<std::string>(), Eq("r_2"));
  EXPECT_THAT(v_rest.at(2).GetAs<std::string>(), Eq("r_3"));
  EXPECT_THAT(v_rest.at(3).GetAs<std::string>(), Eq("r_4"));
}

NOLINT_TEST(PositionalArgumentsTest,
  MultiplePositionals_BeforeAndRest_AssignsCorrectly_Succeeds)
{
  constexpr auto argc = 5;
  std::array<const char*, argc> argv { { "/usr/bin/test-program.exe", "b_1",
    "b_2", "r_1", "r_2" } };

  const std::shared_ptr<Command> default_command { CommandBuilder(
    Command::DEFAULT)
      .WithPositionalArguments(MakeBefore_1(), MakeBefore_2(), MakeRest())
      .Build() };

  const std::unique_ptr<Cli> cli
    = CliBuilder().ProgramName("positional_args").WithCommand(default_command);
  const auto& matches = cli->Parse(argc, argv.data()).ovm;

  const auto& v_before_1 = matches.ValuesOf("BEFORE_1");
  EXPECT_THAT(v_before_1.size(), Eq(1));
  EXPECT_THAT(v_before_1.at(0).GetAs<std::string>(), Eq("b_1"));

  const auto& v_before_2 = matches.ValuesOf("BEFORE_2");
  EXPECT_THAT(v_before_2.size(), Eq(1));
  EXPECT_THAT(v_before_2.at(0).GetAs<std::string>(), Eq("b_2"));

  const auto& v_rest = matches.ValuesOf(Option::key_rest_);
  EXPECT_THAT(v_rest.size(), Eq(2));
  EXPECT_THAT(v_rest.at(0).GetAs<std::string>(), Eq("r_1"));
  EXPECT_THAT(v_rest.at(1).GetAs<std::string>(), Eq("r_2"));
}

NOLINT_TEST(
  PositionalArgumentsTest, RestPositional_WithTrailingPositional_Throws)
{
  constexpr auto argc = 5;
  std::array<const char*, argc> argv { { "/usr/bin/test-program.exe", "r_1",
    "r_2", "r_3", "a_1" } };

  const std::shared_ptr<Command> default_command { CommandBuilder(
    Command::DEFAULT)
      .WithPositionalArguments(MakeRest(), MakeAfter_1())
      .Build() };

  const std::unique_ptr<Cli> cli
    = CliBuilder().ProgramName("positional_args").WithCommand(default_command);
  NOLINT_EXPECT_THROW(cli->Parse(argc, argv.data()), std::runtime_error);
}

NOLINT_TEST(
  PositionalArgumentsTest, MultiplePositionals_BeforeRestAndAfterRest_Throws)
{
  constexpr auto argc = 5;
  std::array<const char*, argc> argv { { "/usr/bin/test-program.exe", "b_1",
    "r_1", "r_2", "a_1" } };

  const std::shared_ptr<Command> default_command { CommandBuilder(
    Command::DEFAULT)
      .WithPositionalArguments(MakeBefore_1(), MakeRest(), MakeAfter_1())
      .Build() };

  const std::unique_ptr<Cli> cli
    = CliBuilder().ProgramName("positional_args").WithCommand(default_command);
  NOLINT_EXPECT_THROW(cli->Parse(argc, argv.data()), std::runtime_error);
}

NOLINT_TEST(PositionalArgumentsTest, NoPositionals_ArgumentsProvided_Throws)
{
  constexpr auto argc = 2;
  std::array<const char*, argc> argv { { "/usr/bin/test-program.exe", "pos" } };

  const std::shared_ptr<Command> default_command {
    CommandBuilder(Command::DEFAULT).Build()
  };

  const std::unique_ptr<Cli> cli
    = CliBuilder().ProgramName("positional_args").WithCommand(default_command);

  NOLINT_EXPECT_THROW(cli->Parse(argc, argv.data()), std::runtime_error);
}

//! Verifies that parsing with only the program name and no positionals
//! succeeds.
NOLINT_TEST(PositionalArgumentsTest, NoArguments_NoPositionals_Succeeds)
{
  constexpr auto argc = 1;
  std::array<const char*, argc> argv { { "/usr/bin/test-program.exe" } };

  const std::shared_ptr<Command> default_command {
    CommandBuilder(Command::DEFAULT).Build()
  };

  const std::unique_ptr<Cli> cli
    = CliBuilder().ProgramName("positional_args").WithCommand(default_command);
  EXPECT_NO_THROW(cli->Parse(argc, argv.data()));
}

//! Verifies that too few arguments for required positionals throws.
NOLINT_TEST(
  PositionalArgumentsTest, MultipleRequiredPositionals_TooFewArguments_Throws)
{
  constexpr auto argc = 2;
  std::array<const char*, argc> argv { { "/usr/bin/test-program.exe",
    "only_one" } };

  const std::shared_ptr<Command> default_command { CommandBuilder(
    Command::DEFAULT)
      .WithPositionalArguments(MakeBefore_1(), MakeBefore_2())
      .Build() };

  const std::unique_ptr<Cli> cli
    = CliBuilder().ProgramName("positional_args").WithCommand(default_command);
  NOLINT_EXPECT_THROW(cli->Parse(argc, argv.data()), std::runtime_error);
}

//! Verifies that an empty rest positional is handled correctly.
NOLINT_TEST(
  PositionalArgumentsTest, RestPositional_NoArguments_DoesNotStoreRest)
{
  constexpr auto argc = 1;
  std::array<const char*, argc> argv { { "/usr/bin/test-program.exe" } };

  const std::shared_ptr<Command> default_command {
    CommandBuilder(Command::DEFAULT).WithPositionalArguments(MakeRest()).Build()
  };

  const std::unique_ptr<Cli> cli
    = CliBuilder().ProgramName("positional_args").WithCommand(default_command);
  const auto& matches = cli->Parse(argc, argv.data()).ovm;
  EXPECT_FALSE(matches.HasOption(Option::key_rest_));
}

//! Verifies that a type conversion error in a positional argument throws.
NOLINT_TEST(
  PositionalArgumentsTest, SinglePositional_TypeConversionFails_Throws)
{
  constexpr auto argc = 2;
  std::array<const char*, argc> argv { { "/usr/bin/test-program.exe",
    "not_an_int" } };

  auto int_positional
    = Option::Positional("INT_ARG").About("int arg").WithValue<int>().Build();
  const std::shared_ptr<Command> default_command { CommandBuilder(
    Command::DEFAULT)
      .WithPositionalArguments(int_positional)
      .Build() };

  const std::unique_ptr<Cli> cli
    = CliBuilder().ProgramName("positional_args").WithCommand(default_command);
  NOLINT_EXPECT_THROW(cli->Parse(argc, argv.data()), std::runtime_error);
}

//! Verifies that multiple trailing positionals after rest is rejected (if not
//! allowed).
NOLINT_TEST(
  PositionalArgumentsTest, RestPositional_MultipleTrailingPositionals_Throws)
{
  constexpr auto argc = 4;
  std::array<const char*, argc> argv { { "/usr/bin/test-program.exe", "a", "b",
    "c" } };

  const std::shared_ptr<Command> default_command { CommandBuilder(
    Command::DEFAULT)
      .WithPositionalArguments(MakeRest(), MakeAfter_1())
      .Build() };

  const std::unique_ptr<Cli> cli
    = CliBuilder().ProgramName("positional_args").WithCommand(default_command);
  NOLINT_EXPECT_THROW(cli->Parse(argc, argv.data()), std::runtime_error);
}

} // namespace
