//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace oxygen::content::import::internal {

//! The levels a name-based LOD group needs: LOD0 and LOD1.
inline constexpr auto kMinNamedLodLevels = size_t { 2 };

//! Splits `<base>_LOD<n>`, with `LOD` in any case and `n` of one or two digits.
[[nodiscard]] inline auto ParseLodSuffix(const std::string_view name)
  -> std::optional<std::pair<std::string_view, size_t>>
{
  constexpr auto kTag = std::string_view { "LOD" };
  constexpr auto kMaxDigits = size_t { 2 };
  constexpr auto kRadix = size_t { 10 };
  const auto separator = name.rfind('_');
  if (separator == std::string_view::npos || separator == 0) {
    return std::nullopt;
  }
  const auto suffix = name.substr(separator + 1);
  const auto is_tag = suffix.size() > kTag.size()
    && suffix.size() <= kTag.size() + kMaxDigits
    && std::ranges::equal(suffix.substr(0, kTag.size()), kTag,
      [](const char a, const char b) -> bool {
        return std::toupper(static_cast<unsigned char>(a)) == b;
      });
  if (!is_tag) {
    return std::nullopt;
  }
  size_t level = 0;
  for (const auto digit : suffix.substr(kTag.size())) {
    if (std::isdigit(static_cast<unsigned char>(digit)) == 0) {
      return std::nullopt;
    }
    level = (level * kRadix) + static_cast<size_t>(digit - '0');
  }
  return std::pair { name.substr(0, separator), level };
}

//! Siblings named as LOD levels: by base name, then level, the sibling's index.
using NamedLods = std::map<std::string_view, std::map<size_t, size_t>>;

//! Adds a sibling to `lods` when its name is that of a LOD level. The first
//! sibling of a level keeps it.
inline auto AddNamedLod(
  NamedLods& lods, const std::string_view name, const size_t node) -> void
{
  if (const auto parsed = ParseLodSuffix(name); parsed.has_value()) {
    lods[parsed->first].try_emplace(parsed->second, node);
  }
}

//! Whether `levels` can form a name-based group: it has LOD0 and LOD1.
[[nodiscard]] inline auto HasNamedLodPair(
  const std::map<size_t, size_t>& levels) -> bool
{
  return levels.contains(0) && levels.contains(1);
}

//! Takes a group's levels in order while they follow one another and `try_add`
//! accepts them. Returns the nodes left out: from the first missing or refused
//! level on, none imports.
template <typename TryAdd>
[[nodiscard]] auto TakeConsecutiveLods(
  const std::map<size_t, size_t>& levels, TryAdd try_add) -> std::vector<size_t>
{
  auto skipped = std::vector<size_t> {};
  auto expected = size_t { 0 };
  for (const auto& [level, node] : levels) {
    if (!skipped.empty() || level != expected || !try_add(level, node)) {
      skipped.push_back(node);
    }
    ++expected;
  }
  return skipped;
}

} // namespace oxygen::content::import::internal
