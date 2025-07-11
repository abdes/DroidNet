//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Clap/Parser/Tokenizer.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using testing::Eq;
using testing::Ne;

namespace oxygen::clap::parser {

namespace {

  using ParamType = std::pair<std::string, std::vector<Token>>;

  class TokenizerTest : public testing::TestWithParam<ParamType> { };

  // NOLINTNEXTLINE
  TEST_P(TokenizerTest, ProduceExpectedTokens)
  {
    const auto& [argument, tokens] = GetParam();
    std::array<std::string, 1> args { argument };
    Tokenizer tokenizer { std::span<const std::string>(args) };

    std::for_each(tokens.cbegin(), tokens.cend(),
      [&tokenizer](const Token& expected_token) {
        const auto token = tokenizer.NextToken();
        EXPECT_THAT(token.first, Eq(expected_token.first));
        EXPECT_THAT(token.second, Eq(expected_token.second));
      });
  }

  // NOLINTNEXTLINE
  INSTANTIATE_TEST_SUITE_P(ValidArguments, TokenizerTest,
    // clang-format off
    ::testing::Values(
        std::make_pair<std::string, std::vector<Token>>("", {}),
        std::make_pair<std::string, std::vector<Token>>("-", {
            {TokenType::kLoneDash, "-"}
        }),
        std::make_pair<std::string, std::vector<Token>>("-f", {
            {TokenType::kShortOption, "f"}
        }),
        std::make_pair<std::string, std::vector<Token>>("-ffv1=2", {
            {TokenType::kShortOption, "f"},
            {TokenType::kShortOption, "f"},
            {TokenType::kShortOption, "v"},
            {TokenType::kShortOption, "1"},
            {TokenType::kShortOption, "="},
            {TokenType::kShortOption, "2"}
        }),
        std::make_pair<std::string, std::vector<Token>>("--f", {
            {TokenType::kLongOption, "f"}
        }),
        std::make_pair<std::string, std::vector<Token>>("--long", {
            {TokenType::kLongOption, "long"}
        }),
        std::make_pair<std::string, std::vector<Token>>("--long-option", {
            {TokenType::kLongOption, "long-option"}
        }),
        std::make_pair<std::string, std::vector<Token>>("--long--option-f", {
            {TokenType::kLongOption, "long--option-f"}
        }),
        std::make_pair<std::string, std::vector<Token>>("--opt=", {
            {TokenType::kLongOption, "opt"},
            {TokenType::kEqualSign, "="},
        }),
        std::make_pair<std::string, std::vector<Token>>("--opt=v", {
            {TokenType::kLongOption, "opt"},
            {TokenType::kEqualSign, "="},
            {TokenType::kValue, "v"},
        }),
        std::make_pair<std::string, std::vector<Token>>("--opt=value-1", {
            {TokenType::kLongOption, "opt"},
            {TokenType::kEqualSign, "="},
            {TokenType::kValue, "value-1"},
        }),
        std::make_pair<std::string, std::vector<Token>>("--opt=with spaces", {
            {TokenType::kLongOption, "opt"},
            {TokenType::kEqualSign, "="},
            {TokenType::kValue, "with spaces"},
        }),
        std::make_pair<std::string, std::vector<Token>>("--opt=v=x", {
            {TokenType::kLongOption, "opt"},
            {TokenType::kEqualSign, "="},
            {TokenType::kValue, "v=x"},
        }),
        std::make_pair<std::string, std::vector<Token>>("--opt=a,b,c", {
            {TokenType::kLongOption, "opt"},
            {TokenType::kEqualSign, "="},
            {TokenType::kValue, "a,b,c"},
        }),
        std::make_pair<std::string, std::vector<Token>>("=", {
            {TokenType::kValue, "="}
        }),
        std::make_pair<std::string, std::vector<Token>>("value", {
            {TokenType::kValue, "value"}
        }),
        std::make_pair<std::string, std::vector<Token>>("va--lue-1", {
            {TokenType::kValue, "va--lue-1"}
        }),
        std::make_pair<std::string, std::vector<Token>>("---", {
            {TokenType::kLongOption, "-"}
        }),
        std::make_pair<std::string, std::vector<Token>>("----", {
            {TokenType::kLongOption, "--"}
        }),
        std::make_pair<std::string, std::vector<Token>>("--", {
            {TokenType::kDashDash, "--"}
        })
    ));
  // clang-format on

  // NOLINTNEXTLINE
  TEST_F(TokenizerTest, NextTokenWithNoToken)
  {
    {
      const Tokenizer tokenizer { {} };

      ASSERT_THAT(tokenizer.NextToken().first, Eq(TokenType::kEndOfInput));
    }
    {
      std::array<std::string, 1> args { "hello" };
      const Tokenizer tokenizer { std::span<const std::string>(args) };

      ASSERT_THAT(tokenizer.NextToken().first, Ne(TokenType::kEndOfInput));
      ASSERT_THAT(tokenizer.NextToken().first, Eq(TokenType::kEndOfInput));
    }
  }

  // NOLINTNEXTLINE
  TEST(TokenizerExample, ComplexCommandLine)
  {
    //! [Tokenizer example]
    std::array<std::string, 8> args { "doit", "-flv", "--host",
      "192.168.10.2:8080", "--allowed_ips=10.0.0.0/8,172.16.0.1/16",
      "--allowed_ids", "one,two", "now" };
    const Tokenizer tokenizer { std::span<const std::string>(args) };

    std::map<TokenType, std::vector<std::string>> tokens;
    while (tokenizer.HasMoreTokens()) {
      const auto [token_type, token_value] = tokenizer.NextToken();
      tokens[token_type].push_back(token_value);
    }
    //! [Tokenizer example]
    EXPECT_THAT(tokens.at(TokenType::kValue).size(), Eq(5));
  }

} // namespace

} // namespace oxygen::clap::parser
