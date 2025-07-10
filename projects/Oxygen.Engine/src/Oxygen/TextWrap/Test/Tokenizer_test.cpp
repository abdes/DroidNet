//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <type_traits>
#include <utility>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/TextWrap/Internal/Tokenizer.h>

using testing::Eq;
using testing::IsTrue;

using oxygen::wrap::internal::Token;
using oxygen::wrap::internal::TokenConsumer;
using oxygen::wrap::internal::Tokenizer;
using oxygen::wrap::internal::TokenType;

// ReSharper disable StringLiteralTypo

namespace {

// NOLINTNEXTLINE
TEST(TokenizerTest, Example)
{
  //! [Tokenizer example]
  constexpr auto tab = " ";
  constexpr bool collapse_ws = true;
  constexpr bool break_on_hyphens = true;

  const Tokenizer tokenizer { tab, collapse_ws, break_on_hyphens };

  constexpr auto text = "Why? \nJust plain \tfinger-licking good!";
  std::vector<Token> tokens;
  const auto status = tokenizer.Tokenize(
    text, [&tokens](TokenType token_type, std::string token) {
      if (token_type != TokenType::kEndOfInput) {
        tokens.emplace_back(token_type, std::move(token));
      }
    });
  // All white spaces replaced and collapsed, hyphenated words
  // broken, to produce the following tokens:
  //     "Why?", " ", "Just", " ", "plain", " ",
  //     "finger-", "licking", " ", "good!"
  //! [Tokenizer example]

  const auto expected = std::vector<Token> {
    Token { TokenType::kChunk, "Why?" }, Token { TokenType::kWhiteSpace, " " },
    Token { TokenType::kNewLine, "" }, Token { TokenType::kChunk, "Just" },
    Token { TokenType::kWhiteSpace, " " }, Token { TokenType::kChunk, "plain" },
    Token { TokenType::kWhiteSpace, " " },
    Token { TokenType::kChunk, "finger-" },
    Token { TokenType::kChunk, "licking" },
    Token { TokenType::kWhiteSpace, " " }, Token { TokenType::kChunk, "good!" }
  };

  EXPECT_THAT(status, IsTrue());
  EXPECT_THAT(tokens.size(), Eq(expected.size()));
  auto expected_token = expected.cbegin();
  auto token = tokens.cbegin();
  const auto end = tokens.cend();
  while (token != end) {
    EXPECT_THAT(*token, Eq(*expected_token));
    ++token;
    ++expected_token;
  }
}

// NOLINTNEXTLINE
TEST(TokenizerTest, CallsTokenConsumerWhenTokenIsReady)
{
  const Tokenizer tokenizer { "\t", false, false };

  //! [Example token consumer]
  std::vector<Token> tokens;
  const auto status = tokenizer.Tokenize(
    "Hello", [&tokens](TokenType token_type, std::string token) {
      if (token_type != TokenType::kEndOfInput) {
        tokens.emplace_back(token_type, std::move(token));
      }
    });
  //! [Example token consumer]

  EXPECT_THAT(status, IsTrue());
  EXPECT_THAT(tokens.size(), Eq(1));
  EXPECT_THAT(
    tokens, Eq(std::vector<Token> { Token { TokenType::kChunk, "Hello" } }));
}

using ParamType = std::tuple<
  // Input text
  std::string,
  // `tab` expansion
  std::string,
  // collapse white space
  bool,
  // break on hyphens
  bool,
  // expected tokens
  std::vector<Token>>;

class TokenizerScenariosTest : public testing::TestWithParam<ParamType> { };

// NOLINTNEXTLINE
TEST_P(TokenizerScenariosTest, Tokenize)
{
  const auto& [text, tab, collapse_ws, break_on_hyphens, expected] = GetParam();
  const Tokenizer tokenizer { tab, collapse_ws, break_on_hyphens };

  std::vector<Token> tokens;
  const auto status = tokenizer.Tokenize(
    text, [&tokens](TokenType token_type, std::string token) {
      if (token_type != TokenType::kEndOfInput) {
        tokens.emplace_back(token_type, std::move(token));
      }
    });

  EXPECT_THAT(status, IsTrue());
  EXPECT_THAT(tokens.size(), Eq(expected.size()));
  auto expected_token = expected.cbegin();
  auto token = tokens.cbegin();
  const auto end = tokens.cend();
  while (token != end) {
    EXPECT_THAT(*token, Eq(*expected_token));
    ++token;
    ++expected_token;
  }
}

// NOLINTNEXTLINE
INSTANTIATE_TEST_SUITE_P(AllOff, TokenizerScenariosTest,
  // clang-format off
    testing::Values(
      ParamType{"",
        "\t", false, false,
        {
        }},
      ParamType{"\n",
        "\t", false, false,
        {
          {TokenType::kNewLine, ""}
        }},
      ParamType{" \n",
        "\t", false, false,
        {
          {TokenType::kWhiteSpace, " "},
          {TokenType::kNewLine, ""}
        }},
      ParamType{"\t\n",
        "\t", false, false,
        {
          {TokenType::kWhiteSpace, "\t"},
          {TokenType::kNewLine, ""}
        }},
      ParamType{"\r\n",
        "\t", false, false,
        {
          {TokenType::kNewLine, ""}
        }},
      ParamType{" \t\n",
        "\t", false, false,
        {
          {TokenType::kWhiteSpace, " \t"},
          {TokenType::kNewLine, ""}
        }},
      ParamType{" \t\n ",
        "\t", false, false,
        {
          {TokenType::kWhiteSpace, " \t"},
          {TokenType::kNewLine, ""},
          {TokenType::kWhiteSpace, " "}
        }},
      ParamType{"\n\n",
        "\t", false, false,
        {
          {TokenType::kParagraphMark, ""}
        }},
      ParamType{" \n\n",
        "\t", false, false,
        {
          {TokenType::kWhiteSpace, " "},
          {TokenType::kParagraphMark, ""}
        }},
      ParamType{" \t\n \n\n \t\n \n",
        "\t", false, false,
        {
          {TokenType::kWhiteSpace, " \t"},
          {TokenType::kNewLine, ""},
          {TokenType::kWhiteSpace, " "},
          {TokenType::kParagraphMark, ""},
          {TokenType::kWhiteSpace, " \t"},
          {TokenType::kNewLine, ""},
          {TokenType::kWhiteSpace, " "},
          {TokenType::kNewLine, ""}
        }},
      ParamType{"\n\n\n",
        "\t", false, false,
        {
          {TokenType::kParagraphMark, ""},
          {TokenType::kNewLine, ""},
        }},
      ParamType{"\n\n\n\n",
        "\t", false, false,
        {
          {TokenType::kParagraphMark, ""},
          {TokenType::kParagraphMark, ""}
        }},
      ParamType{"very-very-long-word",
        "\t", false, false,
        {
          {TokenType::kChunk, "very-very-long-word"}
        }},
      ParamType{"Items\n\n1.\ta-a-a\n\n\n2.\tbbb or ccc",
        "\t", false, false,
        {
          {TokenType::kChunk, "Items"},
          {TokenType::kParagraphMark, ""},
          {TokenType::kChunk, "1."},
          {TokenType::kWhiteSpace, "\t"},
          {TokenType::kChunk, "a-a-a"},
          {TokenType::kParagraphMark, ""},
          {TokenType::kNewLine, ""},
          {TokenType::kChunk, "2."},
          {TokenType::kWhiteSpace, "\t"},
          {TokenType::kChunk, "bbb"},
          {TokenType::kWhiteSpace, " "},
          {TokenType::kChunk, "or"},
          {TokenType::kWhiteSpace, " "},
          {TokenType::kChunk, "ccc"}
        }}
    )
);

// NOLINTNEXTLINE
INSTANTIATE_TEST_SUITE_P(TabExpansionOn, TokenizerScenariosTest,
    // clang-format off
    testing::Values(
      ParamType{"\t", "    ", false, false,
        {{TokenType::kWhiteSpace, "    "}}},
      ParamType{"\t\taaa \t \tbbb", "__", false, false,
        {
          {TokenType::kChunk, "____aaa"},
          {TokenType::kWhiteSpace, " "},
          {TokenType::kChunk, "__"},
          {TokenType::kWhiteSpace, " "},
          {TokenType::kChunk, "__bbb"}
        }}
    )
);

// NOLINTNEXTLINE
INSTANTIATE_TEST_SUITE_P(CollapsekWhiteSpaceOn, TokenizerScenariosTest,
    // clang-format off
    testing::Values(
      ParamType{"\t",
        "\t", true, false,
        {{TokenType::kWhiteSpace, " "}}},
      ParamType{"\t",
        "  ", true, false,
        {{TokenType::kWhiteSpace, " "}}},
      ParamType{"\t",
        "....", true, false,
        {{TokenType::kChunk, "...."}}},
      ParamType{"\t",
        "-\n-", true, false,
        {
          {TokenType::kChunk, "-"},
          {TokenType::kNewLine, ""},
          {TokenType::kChunk, "-"}
        }},
      ParamType{"hello\f   world!\n\nbye\t\rbye \ncruel\v \t world! \r\n ",
        "..", true, false,
        {
          {TokenType::kChunk, "hello"},
          {TokenType::kWhiteSpace, " "},
          {TokenType::kChunk, "world!"},
          {TokenType::kParagraphMark, ""},
          {TokenType::kChunk, "bye..bye"},
          {TokenType::kWhiteSpace, " "},
          {TokenType::kNewLine, ""},
          {TokenType::kChunk, "cruel"},
          {TokenType::kNewLine, ""},
          {TokenType::kWhiteSpace, " "},
          {TokenType::kChunk, ".."},
          {TokenType::kWhiteSpace, " "},
          {TokenType::kChunk, "world!"},
          {TokenType::kWhiteSpace, " "},
          {TokenType::kNewLine, ""},
          {TokenType::kWhiteSpace, " "}
        }}
    )
);

// NOLINTNEXTLINE
INSTANTIATE_TEST_SUITE_P(BreakOnHyphensOn, TokenizerScenariosTest,
    // clang-format off
    testing::Values(
      ParamType{"a-b",
        "  ", false, true,
        {
          {TokenType::kChunk, "a-"},
          {TokenType::kChunk, "b"}
        }},
      ParamType{"universally-true",
        "  ", false, true,
        {
          {TokenType::kChunk, "universally-"},
          {TokenType::kChunk, "true"}
        }},
      ParamType{"some things-never-change",
        "  ", false, true,
        {
          {TokenType::kChunk, "some"},
          {TokenType::kWhiteSpace, " "},
          {TokenType::kChunk, "things-"},
          {TokenType::kChunk, "never-"},
          {TokenType::kChunk, "change"}
        }},
      ParamType{"a-",
        "  ", false, true,
        {
          {TokenType::kChunk, "a-"}
        }},
      ParamType{"a--",
        "  ", false, true,
        {
          {TokenType::kChunk, "a-"},
          {TokenType::kChunk, "-"}
        }},
      ParamType{"--",
        "  ", false, true,
        {
          {TokenType::kChunk, "--"}
        }},
      ParamType{"---",
        "  ", false, true,
        {
          {TokenType::kChunk, "---"}
        }},
      ParamType{"-a-b-c---d-ef",
        "  ", false, true,
        {
          {TokenType::kChunk, "-a-"},
          {TokenType::kChunk, "b-"},
          {TokenType::kChunk, "c-"},
          {TokenType::kChunk, "--d-"},
          {TokenType::kChunk, "ef"}
        }}
    )
);

} // namespace
