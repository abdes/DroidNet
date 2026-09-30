//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <ostream>
#include <string_view>

#include <nlohmann/json.hpp>

namespace oxygen::content::import::internal {
inline auto ParseDescriptorJsonObject(const std::string_view text,
  const std::string_view kind, std::ostream& errors)
  -> std::optional<nlohmann::json>
{
  try {
    auto document = nlohmann::json::parse(text);
    if (!document.is_object()) {
      errors << "ERROR: " << kind << " descriptor root must be a JSON object\n";
      return std::nullopt;
    }
    return document;
  } catch (const nlohmann::json::exception& error) {
    errors << "ERROR: invalid " << kind << " descriptor JSON: " << error.what()
           << '\n';
    return std::nullopt;
  }
}
} // namespace oxygen::content::import::internal
