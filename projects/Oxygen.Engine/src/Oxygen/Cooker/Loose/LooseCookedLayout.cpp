//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>

namespace oxygen::content::import {
namespace {
  [[nodiscard]] auto ExtractLeafStem(const std::string_view text) -> std::string
  {
    auto normalized = std::string(text);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');

    const auto slash_pos = normalized.find_last_of('/');
    auto leaf = (slash_pos == std::string::npos)
      ? normalized
      : normalized.substr(slash_pos + 1U);

    const auto dot_pos = leaf.find_last_of('.');
    if (dot_pos != std::string::npos) {
      leaf.resize(dot_pos);
    }

    return leaf;
  }

  [[nodiscard]] auto SanitizeStem(
    std::string stem, const std::string_view fallback) -> std::string
  {
    if (stem.empty()) {
      stem = std::string(fallback);
    }

    std::string out;
    out.reserve(stem.size());

    auto last_was_underscore = false;
    for (const auto ch : stem) {
      const auto u = static_cast<unsigned char>(ch);
      const bool keep = std::isalnum(u) != 0 || ch == '_' || ch == '-';
      const char normalized = keep ? static_cast<char>(ch) : '_';
      if (normalized == '_') {
        if (!last_was_underscore) {
          out.push_back('_');
        }
        last_was_underscore = true;
      } else {
        out.push_back(normalized);
        last_was_underscore = false;
      }
    }

    while (!out.empty() && out.front() == '_') {
      out.erase(out.begin());
    }
    while (!out.empty() && out.back() == '_') {
      out.pop_back();
    }

    if (out.empty()) {
      return std::string(fallback);
    }
    return out;
  }

  [[nodiscard]] auto BuildStem(std::string_view name_hint,
    std::string_view stable_id, std::string_view fallback) -> std::string
  {
    const auto base = SanitizeStem(ExtractLeafStem(name_hint), fallback);
    const auto id_source = stable_id.empty() ? name_hint : stable_id;
    return fmt::format(
      "{}_{:016x}", base, ComputeFNV1a64(id_source.data(), id_source.size()));
  }

} // namespace

auto LooseCookedLayout::TextureDescriptorRelPath(
  const std::string_view name_hint, const std::string_view stable_id) const
  -> std::string
{
  return TextureDescriptorRelPath(BuildStem(name_hint, stable_id, "texture"));
}

auto LooseCookedLayout::BufferDescriptorRelPath(
  const std::string_view name_hint, const std::string_view stable_id) const
  -> std::string
{
  return BufferDescriptorRelPath(BuildStem(name_hint, stable_id, "buffer"));
}

} // namespace oxygen::content::import
