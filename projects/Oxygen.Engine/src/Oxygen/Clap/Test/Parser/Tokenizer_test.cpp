//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause).
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Clap/Parser/Tokenizer.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using testing::Eq;
using testing::Ne;

namespace asap::clap::parser {

namespace {

  using ParamType = std::pair<std::string, std::vector<Token>>;

  class TokenizerTest : public ::testing::TestWithParam<ParamType> { };

  // NOLINTNEXTLINE
  TEST_P(TokenizerTest, ProduceExpectedTokens)
  {
    const auto& [argument, tokens] = GetParam();
    Tokenizer tokenizer { { argument } };

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
            {TokenType::LoneDash, "-"}
        }),
        std::make_pair<std::string, std::vector<Token>>("-f", {
            {TokenType::ShortOption, "f"}
        }),
        std::make_pair<std::string, std::vector<Token>>("-ffv1=2", {
            {TokenType::ShortOption, "f"},
            {TokenType::ShortOption, "f"},
            {TokenType::ShortOption, "v"},
            {TokenType::ShortOption, "1"},
            {TokenType::ShortOption, "="},
            {TokenType::ShortOption, "2"}
        }),
        std::make_pair<std::string, std::vector<Token>>("--f", {
            {TokenType::LongOption, "f"}
        }),
        std::make_pair<std::string, std::vector<Token>>("--long", {
            {TokenType::LongOption, "long"}
        }),
        std::make_pair<std::string, std::vector<Token>>("--long-option", {
            {TokenType::LongOption, "long-option"}
        }),
        std::make_pair<std::string, std::vector<Token>>("--long--option-f", {
            {TokenType::LongOption, "long--option-f"}
        }),
        std::make_pair<std::string, std::vector<Token>>("--opt=", {
            {TokenType::LongOption, "opt"},
            {TokenType::EqualSign, "="},
        }),
        std::make_pair<std::string, std::vector<Token>>("--opt=v", {
            {TokenType::LongOption, "opt"},
            {TokenType::EqualSign, "="},
            {TokenType::Value, "v"},
        }),
        std::make_pair<std::string, std::vector<Token>>("--opt=value-1", {
            {TokenType::LongOption, "opt"},
            {TokenType::EqualSign, "="},
            {TokenType::Value, "value-1"},
        }),
        std::make_pair<std::string, std::vector<Token>>("--opt=with spaces", {
            {TokenType::LongOption, "opt"},
            {TokenType::EqualSign, "="},
            {TokenType::Value, "with spaces"},
        }),
        std::make_pair<std::string, std::vector<Token>>("--opt=v=x", {
            {TokenType::LongOption, "opt"},
            {TokenType::EqualSign, "="},
            {TokenType::Value, "v=x"},
        }),
        std::make_pair<std::string, std::vector<Token>>("--opt=a,b,c", {
            {TokenType::LongOption, "opt"},
            {TokenType::EqualSign, "="},
            {TokenType::Value, "a,b,c"},
        }),
        std::make_pair<std::string, std::vector<Token>>("=", {
            {TokenType::Value, "="}
        }),
        std::make_pair<std::string, std::vector<Token>>("value", {
            {TokenType::Value, "value"}
        }),
        std::make_pair<std::string, std::vector<Token>>("va--lue-1", {
            {TokenType::Value, "va--lue-1"}
        }),
        std::make_pair<std::string, std::vector<Token>>("---", {
            {TokenType::LongOption, "-"}
        }),
        std::make_pair<std::string, std::vector<Token>>("----", {
            {TokenType::LongOption, "--"}
        }),
        std::make_pair<std::string, std::vector<Token>>("--", {
            {TokenType::DashDash, "--"}
        })
    ));
  // clang-format on

  // NOLINTNEXTLINE
  TEST_F(TokenizerTest, NextTokenWithNoToken)
  {
    {
      const Tokenizer tokenizer { {} };

      ASSERT_THAT(tokenizer.NextToken().first, Eq(TokenType::EndOfInput));
    }
    {
      const Tokenizer tokenizer { { "hello" } };

      ASSERT_THAT(tokenizer.NextToken().first, Ne(TokenType::EndOfInput));
      ASSERT_THAT(tokenizer.NextToken().first, Eq(TokenType::EndOfInput));
    }
  }

  // NOLINTNEXTLINE
  TEST(TokenizerExample, ComplexCommandLine)
  {
    //! [Tokenizer example]
    const Tokenizer tokenizer { { "doit", "-flv", "--host", "192.168.10.2:8080",
      "--allowed_ips=10.0.0.0/8,172.16.0.1/16", "--allowed_ids", "one,two",
      "now" } };

    std::map<TokenType, std::vector<std::string>> tokens;
    while (tokenizer.HasMoreTokens()) {
      const auto [token_type, token_value] = tokenizer.NextToken();
      tokens[token_type].push_back(token_value);
    }
    //! [Tokenizer example]
    EXPECT_THAT(tokens.at(TokenType::Value).size(), Eq(5));
  }

} // namespace

} // namespace asap::clap::parser
