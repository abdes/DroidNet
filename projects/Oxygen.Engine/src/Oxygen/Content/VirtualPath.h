//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace oxygen::content {

enum class VirtualPathRuleSet : std::uint8_t {
  kSyntaxOnly = 0,
  kSyntaxAndStandardMountRoot,
};

constexpr std::size_t kMaxCanonicalVirtualPathBytes = 512;

namespace detail {

  [[nodiscard]] constexpr auto IsAsciiAlphaNumeric(const char c) noexcept
    -> bool
  {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
      || (c >= '0' && c <= '9');
  }

  [[nodiscard]] constexpr auto IsAllowedSegmentChar(const char c) noexcept
    -> bool
  {
    return IsAsciiAlphaNumeric(c) || c == '-' || c == '_' || c == '.';
  }

  [[nodiscard]] constexpr auto DotCount(const std::string_view segment) noexcept
    -> std::size_t
  {
    auto count = std::size_t { 0 };
    for (const auto c : segment) {
      if (c == '.') {
        ++count;
      }
    }
    return count;
  }

} // namespace detail

[[nodiscard]] inline auto ValidateCanonicalVirtualPath(
  const std::string_view virtual_path,
  const VirtualPathRuleSet rules = VirtualPathRuleSet::kSyntaxOnly)
  -> std::optional<std::string_view>
{
  if (virtual_path.empty()) {
    return "path must not be empty";
  }
  if (virtual_path.size() > kMaxCanonicalVirtualPathBytes) {
    return "path must be <= 512 bytes";
  }
  if (virtual_path.front() != '/') {
    return "path must start with '/'";
  }
  if (virtual_path.find('\\') != std::string_view::npos) {
    return "path must not contain '\\\\'";
  }
  if (virtual_path.size() > 1 && virtual_path.back() == '/') {
    return "path must not have a trailing '/'";
  }

  auto first_segment = std::string_view {};
  auto second_segment = std::string_view {};
  auto segment_index = std::size_t { 0 };

  auto pos = std::size_t { 1 };
  while (pos < virtual_path.size()) {
    const auto next = virtual_path.find('/', pos);
    const auto end
      = next == std::string_view::npos ? virtual_path.size() : next;
    const auto segment = virtual_path.substr(pos, end - pos);
    const auto is_leaf = next == std::string_view::npos;
    ++segment_index;

    if (segment.empty()) {
      return "path must not contain empty segments";
    }
    if (segment == "." || segment == "..") {
      return "path must not contain '.' or '..' segments";
    }

    if (segment_index == 1) {
      first_segment = segment;
    } else if (segment_index == 2) {
      second_segment = segment;
    }

    for (const auto c : segment) {
      if (!detail::IsAllowedSegmentChar(c)) {
        return "segment contains disallowed characters";
      }
    }

    if (is_leaf) {
      const auto dot_count = detail::DotCount(segment);
      if (dot_count != 1 || segment.front() == '.' || segment.back() == '.') {
        return "leaf segment must contain exactly one '.' separator";
      }
    } else {
      const auto allow_mount_dot = segment_index == 1 && segment == ".cooked";
      if (!allow_mount_dot && segment.find('.') != std::string_view::npos) {
        return "non-leaf segments must not contain '.'";
      }
    }

    if (next == std::string_view::npos) {
      break;
    }
    pos = next + 1;
  }

  if (segment_index == 0) {
    return "path must contain at least one segment";
  }

  if (rules == VirtualPathRuleSet::kSyntaxAndStandardMountRoot) {
    if (first_segment == "Engine" || first_segment == "Game"
      || first_segment == ".cooked") {
      return std::nullopt;
    }

    if (first_segment == "Pak") {
      if (second_segment.empty()) {
        return "pak mount root must be '/Pak/<name>'";
      }
      if (second_segment.find('.') != std::string_view::npos) {
        return "pak mount name must not contain '.'";
      }
      return std::nullopt;
    }

    return "unsupported mount root";
  }

  return std::nullopt;
}

[[nodiscard]] inline auto IsCanonicalVirtualPath(
  const std::string_view virtual_path,
  const VirtualPathRuleSet rules = VirtualPathRuleSet::kSyntaxOnly) -> bool
{
  return !ValidateCanonicalVirtualPath(virtual_path, rules).has_value();
}

} // namespace oxygen::content
