//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Console/Parser.h>

namespace {

using oxygen::console::Parser;

NOLINT_TEST(ConsoleParser, HandlesQuotesEscapesAndWhitespace)
{
  const auto tokens = Parser::Tokenize(
    R"(cmd  "arg with space"  'single quoted' escaped\ value "a\"b"  )");

  constexpr size_t kExpectedTokens = 5;
  ASSERT_EQ(tokens.size(), kExpectedTokens);
  EXPECT_EQ(tokens[0], "cmd");
  EXPECT_EQ(tokens[1], "arg with space");
  EXPECT_EQ(tokens[2], "single quoted");
  EXPECT_EQ(tokens[3], "escaped value");
  EXPECT_EQ(tokens[4], "a\"b");
}

NOLINT_TEST(ConsoleParser, EmptyInputYieldsNoTokens)
{
  const auto tokens = Parser::Tokenize("   \t \r\n ");
  EXPECT_TRUE(tokens.empty());
}

NOLINT_TEST(ConsoleParser, PreservesWindowsPaths)
{
  const auto tokens = Parser::Tokenize(R"(exec C:\temp\console.cfg)");
  ASSERT_EQ(tokens.size(), 2);
  EXPECT_EQ(tokens[0], "exec");
  EXPECT_EQ(tokens[1], R"(C:\temp\console.cfg)");
}

NOLINT_TEST(ConsoleParser, HandlesUnmatchedQuotes)
{
  // Implementation choice: either keep the quote or terminate at end of string
  const auto tokens = Parser::Tokenize(R"(cmd "unmatched quote)");
  ASSERT_EQ(tokens.size(), 2);
  EXPECT_EQ(tokens[0], "cmd");
  EXPECT_EQ(tokens[1], "unmatched quote");
}

NOLINT_TEST(ConsoleParser, IgnoresComments)
{
  const auto tokens = Parser::Tokenize("cmd arg1 # this is a comment");
  ASSERT_EQ(tokens.size(), 2);
  EXPECT_EQ(tokens[0], "cmd");
  EXPECT_EQ(tokens[1], "arg1");
}

NOLINT_TEST(ConsoleParser, HandlesMultipleCommandsOnOneLine)
{
  const auto commands = Parser::SplitCommands("cmd1 arg1; cmd2 \"arg2;part\"");
  ASSERT_EQ(commands.size(), 2);
  EXPECT_EQ(commands[0], "cmd1 arg1");
  EXPECT_EQ(commands[1], "cmd2 \"arg2;part\"");
}

NOLINT_TEST(ConsoleParser, SplitCommandsTrimsWhitespace)
{
  const auto commands = Parser::SplitCommands("  cmd1  ;   cmd2   ");
  ASSERT_EQ(commands.size(), 2);
  EXPECT_EQ(commands[0], "cmd1");
  EXPECT_EQ(commands[1], "cmd2");
}

NOLINT_TEST(ConsoleParser, SplitCommandsHandlesEscapedSeparators)
{
  const auto commands = Parser::SplitCommands(R"(cmd1 arg1\;part; cmd2 arg2)");
  ASSERT_EQ(commands.size(), 2);
  EXPECT_EQ(commands[0], R"(cmd1 arg1\;part)");
  EXPECT_EQ(commands[1], "cmd2 arg2");
}

} // namespace
