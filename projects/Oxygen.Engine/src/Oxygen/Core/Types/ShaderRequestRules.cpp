//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Core/Types/ShaderRequestRules.h>
#include <Oxygen/Core/Types/ShaderType.h>

namespace {

[[nodiscard]] auto IsUpper(const char c) noexcept -> bool
{
  return c >= 'A' && c <= 'Z';
}

[[nodiscard]] auto IsLetter(const char c) noexcept -> bool
{
  return IsUpper(c) || (c >= 'a' && c <= 'z');
}

[[nodiscard]] auto IsDigit(const char c) noexcept -> bool
{
  return c >= '0' && c <= '9';
}

} // namespace

auto oxygen::IsShaderEntryPoint(const std::string_view name) noexcept -> bool
{
  const auto starts_well
    = !name.empty() && (IsLetter(name.front()) || name.front() == '_');
  return starts_well && std::ranges::all_of(name, [](const char c) -> bool {
    return IsLetter(c) || IsDigit(c) || c == '_';
  });
}

auto oxygen::IsShaderStage(const std::uint32_t value) noexcept -> bool
{
  return value != static_cast<std::uint32_t>(ShaderType::kUnknown)
    && value <= static_cast<std::uint32_t>(ShaderType::kMaxShaderType);
}

auto oxygen::IsShaderDefineName(const std::string_view name) noexcept -> bool
{
  const auto starts_well = !name.empty() && IsUpper(name.front());
  return starts_well && std::ranges::all_of(name, [](const char c) -> bool {
    return IsUpper(c) || IsDigit(c) || c == '_';
  });
}

auto oxygen::IsShaderDefineValue(const std::string_view value) noexcept -> bool
{
  // The whitespace of the C locale.
  return value.find_first_of(" \t\n\v\f\r") == std::string_view::npos;
}

auto oxygen::CanonicalShaderDefines(const std::string_view defines)
  -> std::optional<std::string>
{
  if (defines.empty()) {
    return std::string {};
  }
  auto entries = std::vector<std::string_view> {};
  for (const auto part : std::views::split(defines, ',')) {
    const auto entry = std::string_view { part.begin(), part.end() };
    const auto equals = entry.find('=');
    const auto name = entry.substr(0, equals);
    const auto value = equals == std::string_view::npos
      ? std::string_view {}
      : entry.substr(equals + 1U);
    if (!IsShaderDefineName(name) || !IsShaderDefineValue(value)) {
      return std::nullopt;
    }
    entries.push_back(entry);
  }
  const auto name_of = [](const std::string_view entry) -> std::string_view {
    return entry.substr(0, entry.find('='));
  };
  std::ranges::sort(entries, {}, name_of);
  const auto repeated = std::ranges::adjacent_find(entries, {}, name_of);
  if (repeated != entries.end()) {
    return std::nullopt;
  }
  auto canonical = std::string {};
  for (const auto entry : entries) {
    if (!canonical.empty()) {
      canonical += ',';
    }
    canonical += entry;
  }
  return canonical;
}

auto oxygen::CanonicalShaderSourcePath(const std::string_view path)
  -> std::optional<std::string>
{
  // Works on bytes, so UTF-8 names pass through whatever the platform's
  // narrow encoding.
  auto slashed = std::string(path);
  std::ranges::replace(slashed, '\\', '/');
  const auto names_drive = slashed.contains(':');
  const auto rooted = slashed.starts_with('/');
  if (names_drive || rooted) {
    return std::nullopt;
  }
  auto parts = std::vector<std::string_view> {};
  for (const auto part : std::views::split(std::string_view { slashed }, '/')) {
    const auto name = std::string_view { part.begin(), part.end() };
    if (name.empty() || name == ".") {
      continue;
    }
    if (name == "..") {
      if (parts.empty()) {
        return std::nullopt;
      }
      parts.pop_back();
      continue;
    }
    parts.push_back(name);
  }
  if (parts.empty()) {
    return std::nullopt;
  }
  auto canonical = std::string {};
  for (const auto part : parts) {
    if (!canonical.empty()) {
      canonical += '/';
    }
    canonical += part;
  }
  return canonical;
}
