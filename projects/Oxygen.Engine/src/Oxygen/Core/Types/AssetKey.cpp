//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include <xxhash.h>

#include <Oxygen/Base/Hex.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Core/Types/AssetKey.h>

namespace oxygen::data {

namespace {

  constexpr auto kCanonicalTextLength = std::size_t { 36U };

  constexpr auto kHyphenPositions
    = std::array<std::size_t, 4> { 8U, 13U, 18U, 23U };

  constexpr auto HasCanonicalHyphens(const std::string_view str) noexcept
    -> bool
  {
    return str.size() == kCanonicalTextLength
      && std::ranges::all_of(kHyphenPositions,
        [str](const auto position) -> bool { return str[position] == '-'; });
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

auto AssetKey::FromCodePath(const std::string_view code_path) -> AssetKey
{
  auto virtual_path = std::string { kCodeMount };
  virtual_path.push_back('/');
  virtual_path.append(code_path);
  return FromVirtualPath(virtual_path);
}

auto AssetKey::FromString(const std::string_view text) -> Result<AssetKey>
{
  if (!HasCanonicalHyphens(text)) {
    return Result<AssetKey>::Err(std::errc::invalid_argument);
  }

  // A hyphen anywhere else leaves too few digits for the codec to accept.
  auto digits = std::string { text };
  std::erase(digits, '-');
  const auto is_upper
    = [](const char c) -> bool { return c >= 'A' && c <= 'F'; };
  auto bytes = ByteArray {};
  if (std::ranges::any_of(digits, is_upper)
    || !base::FromHexString(
      digits, std::as_writable_bytes(std::span { bytes }))) {
    return Result<AssetKey>::Err(std::errc::invalid_argument);
  }

  return Result<AssetKey>::Ok(FromBytes(bytes));
}

auto to_string(const AssetKey& value) -> std::string
{
  auto out = base::ToHexString(
    std::as_bytes(std::span { value.data(), AssetKey::kSizeBytes }));
  for (const auto position : kHyphenPositions) {
    out.insert(position, 1U, '-');
  }
  return out;
}

} // namespace oxygen::data
