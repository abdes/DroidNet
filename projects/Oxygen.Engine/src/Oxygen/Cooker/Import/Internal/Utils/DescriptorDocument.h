//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/Filesystem.h>

namespace oxygen::content::import::internal {

//! Reads descriptor bytes once; interpretation is owned by the descriptor's
//! preparation API.
inline auto LoadDescriptorText(const std::filesystem::path& descriptor_path,
  const std::string_view descriptor_kind, std::ostream& error_stream)
  -> std::optional<std::string>
{
  auto input
    = std::ifstream(base::ToNativePath(descriptor_path), std::ios::binary);
  if (!input.is_open()) {
    error_stream << "ERROR: failed to open " << descriptor_kind
                 << " descriptor: " << descriptor_path.string() << "\n";
    return std::nullopt;
  }
  auto text = std::string(std::istreambuf_iterator<char>(input), {});
  if (input.bad()) {
    error_stream << "ERROR: failed to read " << descriptor_kind
                 << " descriptor: " << descriptor_path.string() << "\n";
    return std::nullopt;
  }
  return text;
}

inline auto LoadDescriptorJsonObject(
  const std::filesystem::path& descriptor_path,
  const std::string_view descriptor_kind, std::ostream& error_stream)
  -> std::optional<nlohmann::json>
{
  const auto text
    = LoadDescriptorText(descriptor_path, descriptor_kind, error_stream);
  if (!text) {
    return std::nullopt;
  }
  try {
    auto doc = nlohmann::json::parse(*text);
    if (!doc.is_object()) {
      error_stream << "ERROR: " << descriptor_kind
                   << " descriptor root must be a JSON object\n";
      return std::nullopt;
    }
    return doc;
  } catch (const nlohmann::json::exception& failure) {
    error_stream << "ERROR: invalid " << descriptor_kind
                 << " descriptor JSON: " << failure.what() << "\n";
    return std::nullopt;
  }
}

} // namespace oxygen::content::import::internal
