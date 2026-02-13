//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Console/api_export.h>

namespace oxygen::console {

class Parser final {
public:
  Parser() = delete;
  ~Parser() = delete;

  Parser(const Parser&) = delete;
  auto operator=(const Parser&) -> Parser& = delete;
  Parser(Parser&&) = delete;
  auto operator=(Parser&&) -> Parser& = delete;

  OXGN_CONS_NDAPI static auto Tokenize(std::string_view line)
    -> std::vector<std::string>;
  OXGN_CONS_NDAPI static auto SplitCommands(std::string_view line)
    -> std::vector<std::string>;
};

} // namespace oxygen::console
