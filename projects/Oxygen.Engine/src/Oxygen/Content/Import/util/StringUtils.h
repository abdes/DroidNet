//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Data/AssetKey.h>

namespace oxygen::content::import::util {

//! Converts ufbx_string to std::string_view.
[[nodiscard]] inline auto ToStringView(const char* data, size_t length)
  -> std::string_view
{
  return std::string_view(data, length);
}

//! Checks if a string starts with a prefix, case-insensitive.
[[nodiscard]] inline auto StartsWithIgnoreCase(
  std::string_view str, std::string_view prefix) -> bool
{
  if (str.size() < prefix.size()) {
    return false;
  }
  return std::equal(
    prefix.begin(), prefix.end(), str.begin(), [](char a, char b) {
      return std::tolower(static_cast<unsigned char>(a))
        == std::tolower(static_cast<unsigned char>(b));
    });
}

//! Creates a deterministic AssetKey from a virtual path using SHA256.
[[nodiscard]] inline auto MakeDeterministicAssetKey(
  std::string_view virtual_path) -> oxygen::data::AssetKey
{
  const auto bytes = std::as_bytes(
    std::span(virtual_path.data(), static_cast<size_t>(virtual_path.size())));
  const auto digest = oxygen::base::ComputeSha256(bytes);

  oxygen::data::AssetKey key {};
  std::copy_n(digest.begin(), key.guid.size(), key.guid.begin());
  return key;
}

//! Creates a random AssetKey.
[[nodiscard]] inline auto MakeRandomAssetKey() -> oxygen::data::AssetKey
{
  oxygen::data::AssetKey key {};
  key.guid = oxygen::data::GenerateAssetGuid();
  return key;
}

//! Clamps a float to the [0, 1] range.
[[nodiscard]] inline auto Clamp01(const float v) noexcept -> float
{
  return std::clamp(v, 0.0F, 1.0F);
}

//! Converts ufbx_real to float.
[[nodiscard]] inline auto ToFloat(const double v) noexcept -> float
{
  return static_cast<float>(v);
}

//! Truncates a string and null-terminates it into a fixed-size buffer.
inline auto TruncateAndNullTerminate(
  char* dst, const size_t dst_size, std::string_view s) -> void
{
  if (dst == nullptr || dst_size == 0) {
    return;
  }

  std::fill_n(dst, dst_size, '\0');
  const auto copy_len = (std::min)(dst_size - 1, s.size());
  std::copy_n(s.data(), copy_len, dst);
}

} // namespace oxygen::content::import::util
