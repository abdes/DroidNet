//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Content/TextureResourceLocator.h>

namespace oxygen::content {
namespace {
  auto ToLowerAscii(std::string value) -> std::string
  {
    for (auto& ch : value) {
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
  }

  auto IsHexString(const std::string_view text) -> bool
  {
    if (text.empty()) {
      return false;
    }
    return std::ranges::all_of(text, [](const auto ch) -> bool {
      return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
    });
  }

  auto ResolveHashedTextureDescriptorPath(
    const std::filesystem::path& plain_path,
    std::filesystem::path& resolved_path, std::string& error_message) -> bool
  {
    constexpr size_t kHashHexLength = 16;

    const auto parent = plain_path.parent_path();
    if (!std::filesystem::exists(parent)
      || !std::filesystem::is_directory(parent)) {
      return false;
    }

    const auto ext = ToLowerAscii(plain_path.extension().string());
    if (ext != ".otex") {
      return false;
    }

    const auto plain_stem = plain_path.stem().string();
    if (plain_stem.empty()) {
      return false;
    }

    std::vector<std::filesystem::path> matches;
    const auto expected_prefix = ToLowerAscii(plain_stem) + "_";

    for (const auto& entry : std::filesystem::directory_iterator(parent)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      const auto& candidate = entry.path();
      if (ToLowerAscii(candidate.extension().string()) != ".otex") {
        continue;
      }

      const auto candidate_stem = candidate.stem().string();
      const auto candidate_lower = ToLowerAscii(candidate_stem);
      if (!candidate_lower.starts_with(expected_prefix)) {
        continue;
      }
      const auto suffix = candidate_lower.substr(expected_prefix.size());
      if (suffix.size() != kHashHexLength || !IsHexString(suffix)) {
        continue;
      }
      matches.push_back(candidate);
    }

    if (matches.empty()) {
      return false;
    }
    if (matches.size() > 1U) {
      error_message = "Multiple hashed texture descriptors match virtual path";
      return false;
    }

    resolved_path = std::move(matches.front());
    return true;
  }

} // namespace

auto FindTextureResourceDescriptorPath(
  const std::filesystem::path& descriptor_path)
  -> std::optional<std::filesystem::path>
{
  if (std::filesystem::is_regular_file(descriptor_path)) {
    return descriptor_path;
  }
  auto resolved = std::filesystem::path {};
  auto error = std::string {};
  if (ResolveHashedTextureDescriptorPath(descriptor_path, resolved, error)) {
    return resolved;
  }
  if (!error.empty()) {
    throw std::runtime_error(error + ": " + descriptor_path.string());
  }
  return std::nullopt;
}

} // namespace oxygen::content
