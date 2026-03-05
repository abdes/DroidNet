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

  [[nodiscard]] constexpr auto IsAsciiAlpha(const char c) noexcept -> bool
  {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
  }

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

  [[nodiscard]] constexpr auto IsValidMountRootIdentifier(
    const std::string_view segment) noexcept -> bool
  {
    // Core engine mount root token.
    if (segment == ".cooked") {
      return true;
    }
    if (segment.empty()) {
      return false;
    }

    const auto first = segment.front();
    if (!(IsAsciiAlpha(first) || first == '_')) {
      return false;
    }

    for (const auto c : segment.substr(1)) {
      if (!(IsAsciiAlphaNumeric(c) || c == '_' || c == '-')) {
        return false;
      }
    }
    return true;
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
    if (!detail::IsValidMountRootIdentifier(first_segment)) {
      return "mount root must be a valid identifier";
    }
    return std::nullopt;
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
