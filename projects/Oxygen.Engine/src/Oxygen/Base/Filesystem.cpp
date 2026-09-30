//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Platforms.h>

#ifdef OXYGEN_WINDOWS
#  include <Windows.h> // IWYU pragma: keep

#  include <errhandlingapi.h>
#  include <winnls.h>
#endif

namespace oxygen::base {

#ifdef OXYGEN_WINDOWS
namespace {
  constexpr std::wstring_view kExtendedPrefix = LR"(\\?\)";
  constexpr std::wstring_view kDevicePrefix = LR"(\\.\)";
  constexpr std::wstring_view kUncPrefix = LR"(\\)";
  constexpr std::wstring_view kExtendedUncPrefix = LR"(\\?\UNC\)";

  auto IsExtendedUnc(const std::wstring_view path) -> bool
  {
    return path.size() >= kExtendedUncPrefix.size()
      && std::ranges::equal(path.substr(0, kExtendedUncPrefix.size()),
        kExtendedUncPrefix, [](const wchar_t lhs, const wchar_t rhs) -> bool {
          const auto upper
            = lhs >= L'a' && lhs <= L'z' ? lhs - (L'a' - L'A') : lhs;
          return upper == rhs;
        });
  }
} // namespace
#endif

auto ToNativePath(const std::filesystem::path& path) -> std::filesystem::path
{
#ifdef OXYGEN_WINDOWS
  if (path.empty()) {
    return path;
  }
  auto preferred = path;
  preferred.make_preferred();
  if (preferred.native().starts_with(kExtendedPrefix)
    || preferred.native().starts_with(kDevicePrefix)) {
    return preferred;
  }
  const auto absolute
    = std::filesystem::absolute(preferred).lexically_normal().make_preferred();
  const auto& native = absolute.native();
  if (native.starts_with(kUncPrefix)) {
    return { std::wstring(kExtendedUncPrefix)
      + native.substr(kUncPrefix.size()) };
  }
  return { std::wstring(kExtendedPrefix) + native };
#else
  return path;
#endif
}

auto ToLogicalPath(const std::filesystem::path& path) -> std::filesystem::path
{
#ifdef OXYGEN_WINDOWS
  const auto& native = path.native();
  if (IsExtendedUnc(native)) {
    return { std::wstring(kUncPrefix)
      + native.substr(kExtendedUncPrefix.size()) };
  }
  if (native.starts_with(kExtendedPrefix)) {
    const auto tail = std::wstring_view(native).substr(kExtendedPrefix.size());
    constexpr std::size_t kDriveRootLength = 3U;
    if (tail.size() >= kDriveRootLength && tail.at(1) == L':'
      && tail.at(2) == L'\\') {
      return { tail };
    }
  }
#endif
  return path;
}

auto PathIdentityKey(const std::filesystem::path& path) -> std::string
{
  if (path.empty()) {
    return {};
  }
  auto normalized = ToLogicalPath(
    std::filesystem::absolute(ToNativePath(path)).lexically_normal());
  while (!normalized.has_filename() && normalized != normalized.root_path()) {
    normalized = normalized.parent_path();
  }
#ifdef OXYGEN_WINDOWS
  const auto spelling = normalized.generic_wstring();
  if (spelling.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    throw std::length_error("Path exceeds Windows string limits");
  }
  const auto size = static_cast<int>(spelling.size());
  const auto required = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE,
    spelling.data(), size, nullptr, 0, nullptr, nullptr, 0);
  if (required == 0) {
    throw std::system_error(static_cast<int>(GetLastError()),
      std::system_category(), "Normalize path identity");
  }
  auto folded = std::wstring(static_cast<size_t>(required), L'\0');
  if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, spelling.data(),
        size, folded.data(), required, nullptr, nullptr, 0)
    == 0) {
    throw std::system_error(static_cast<int>(GetLastError()),
      std::system_category(), "Normalize path identity");
  }
  normalized = folded;
#endif
  const auto encoded = normalized.generic_u8string();
  return { encoded.begin(), encoded.end() };
}

} // namespace oxygen::base
