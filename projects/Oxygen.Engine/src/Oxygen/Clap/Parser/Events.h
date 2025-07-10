//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Clap/Parser/Tokenizer.h>

namespace oxygen::clap::parser::detail {

template <TokenType TokenT> struct TokenEvent {
  std::string token;
  TokenType token_type { TokenT };

  TokenEvent() = delete;
  TokenEvent(const TokenEvent&) = default;
  TokenEvent(TokenEvent&&) noexcept = default;
  auto operator=(const TokenEvent&) -> TokenEvent& = delete;
  auto operator=(TokenEvent&&) -> TokenEvent& = delete;
  virtual ~TokenEvent() = default;

  explicit TokenEvent(std::string token_value)
    : token { std::move(token_value) }
  {
  }
};

} // namespace oxygen::clap::parser::detail
