//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <deque>
#include <iostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <magic_enum/magic_enum.hpp>

#include <Oxygen/Clap/api_export.h>

namespace oxygen::clap::parser {

enum class TokenType : uint8_t {
  kShortOption,
  kLongOption,
  kLoneDash,
  kDashDash,
  kValue,
  kEqualSign,
  kEndOfInput
};

OXGN_CLP_API auto operator<<(std::ostream& out, const TokenType& token_type)
  -> std::ostream&;

using Token = std::pair<TokenType, std::string>;

/*!
 * \brief Transform a list of command line arguments into a stream of typed
 * tokens for later processing by the command line parser.
 *
 * **Example**
 *
 * \snippet tokenizer_test.cpp Tokenizer example
 */
class Tokenizer {
public:
  /*!
   * \brief Make a tokenizer with the given command line arguments.
   *
   * When calling this from a main function with argc/argv, remove the program
   * name (argv[0]) from the command line arguments before passing the remaining
   * arguments to the tokenizer.
   */
  explicit Tokenizer(std::span<const std::string> args)
    : args_ { std::move(args) }
    , cursor_ { args_.begin() }
  {
  }

  auto NextToken() const -> Token
  {
    if (!tokens_.empty()) {
      auto token = tokens_.front();
      tokens_.pop_front();
      return token;
    }
    if (cursor_ != args_.end()) {
      const auto arg = *cursor_++;
      Tokenize(arg);
      if (!tokens_.empty()) {
        auto token = tokens_.front();
        tokens_.pop_front();
        return token;
      }
    }
    return Token { TokenType::kEndOfInput, "" };
  }

  auto HasMoreTokens() const -> bool
  {
    return !tokens_.empty() || cursor_ != args_.end();
  }

private:
  OXGN_CLP_API auto Tokenize(const std::string& arg) const -> void;

  std::span<const std::string> args_;
  mutable std::span<const std::string>::iterator cursor_;
  mutable std::deque<Token> tokens_;
};

} // namespace oxygen::clap::parser

#if !defined(DOXYGEN_DOCUMENTATION_BUILD)
template <> struct fmt::formatter<oxygen::clap::parser::TokenType> {
  template <typename ParseContext>
  static constexpr auto parse(ParseContext& ctx)
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(
    const oxygen::clap::parser::TokenType& token_type, FormatContext& ctx) const
  {
    return fmt::format_to(ctx.out(), "{}", magic_enum::enum_name(token_type));
  }
};
#endif // DOXYGEN_DOCUMENTATION_BUILD
