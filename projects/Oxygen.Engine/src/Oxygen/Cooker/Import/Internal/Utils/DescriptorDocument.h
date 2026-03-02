//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace oxygen::content::import::internal {

inline auto LoadDescriptorJsonObject(
  const std::filesystem::path& descriptor_path,
  const std::string_view descriptor_kind, std::ostream& error_stream)
  -> std::optional<nlohmann::json>
{
  auto input = std::ifstream(descriptor_path, std::ios::binary);
  if (!input.is_open()) {
    error_stream << "ERROR: failed to open " << descriptor_kind
                 << " descriptor: " << descriptor_path.string() << "\n";
    return std::nullopt;
  }

  try {
    auto doc = nlohmann::json {};
    input >> doc;
    if (!doc.is_object()) {
      error_stream << "ERROR: " << descriptor_kind
                   << " descriptor root must be a JSON object\n";
      return std::nullopt;
    }
    return doc;
  } catch (const std::exception& ex) {
    error_stream << "ERROR: invalid " << descriptor_kind
                 << " descriptor JSON: " << ex.what() << "\n";
    return std::nullopt;
  }
}

} // namespace oxygen::content::import::internal
