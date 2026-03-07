//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include <xxhash.h>

#include <Oxygen/Data/AssetKey.h>

namespace oxygen::data {

namespace {

  constexpr auto kCanonicalTextLength = std::size_t { 36U };

  constexpr auto ParseHexNibble(const char c) noexcept -> uint8_t
  {
    constexpr auto kInvalidNibble = uint8_t { 0xFFU };
    if (c >= '0' && c <= '9') {
      return static_cast<uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
      return static_cast<uint8_t>(c - 'a' + 10);
    }
    return kInvalidNibble;
  }

  constexpr auto HasCanonicalHyphens(const std::string_view str) noexcept
    -> bool
  {
    return str.size() == kCanonicalTextLength && str[8] == '-' && str[13] == '-'
      && str[18] == '-' && str[23] == '-';
  }

} // namespace

auto AssetKey::FromVirtualPath(const std::string_view virtual_path) -> AssetKey
{
  const auto bytes
    = std::as_bytes(std::span(virtual_path.data(), virtual_path.size()));
  const auto hash = XXH3_128bits(bytes.data(), bytes.size());

  auto canonical = XXH128_canonical_t {};
  XXH128_canonicalFromHash(&canonical, hash);

  auto key_bytes = std::array<std::uint8_t, AssetKey::kSizeBytes> {};
  std::copy_n(canonical.digest, key_bytes.size(), key_bytes.begin());
  return FromBytes(key_bytes);
}

auto AssetKey::FromString(const std::string_view text) -> Result<AssetKey>
{
  constexpr auto kInvalidNibble = uint8_t { 0xFFU };

  if (!HasCanonicalHyphens(text)) {
    return Result<AssetKey>::Err(std::errc::invalid_argument);
  }

  auto bytes = ByteArray {};
  auto byte_index = std::size_t { 0U };

  for (auto i = std::size_t { 0U }; i < text.size();) {
    if (text[i] == '-') {
      ++i;
      continue;
    }
    if ((i + 1U) >= text.size() || text[i + 1U] == '-') {
      return Result<AssetKey>::Err(std::errc::invalid_argument);
    }

    const auto high = ParseHexNibble(text[i]);
    const auto low = ParseHexNibble(text[i + 1U]);
    if (high == kInvalidNibble || low == kInvalidNibble) {
      return Result<AssetKey>::Err(std::errc::invalid_argument);
    }

    bytes.at(byte_index) = static_cast<uint8_t>((high << 4U) | low);
    ++byte_index;
    i += 2U;
  }

  if (byte_index != bytes.size()) {
    return Result<AssetKey>::Err(std::errc::invalid_argument);
  }

  return Result<AssetKey>::Ok(FromBytes(bytes));
}

auto to_string(const AssetKey& value) -> std::string
{
  constexpr auto kTextLength = std::size_t { 36U };
  constexpr auto kHex = std::string_view { "0123456789abcdef" };

  auto out = std::string {};
  out.reserve(kTextLength);

  const auto* bytes = value.data();
  for (std::size_t i = 0; i < AssetKey::kSizeBytes; ++i) {
    const auto b = bytes[i];
    out.push_back(kHex[b >> 4]);
    out.push_back(kHex[b & 0x0F]);
    if (i == 3 || i == 5 || i == 7 || i == 9) {
      out.push_back('-');
    }
  }

  return out;
}

} // namespace oxygen::data
