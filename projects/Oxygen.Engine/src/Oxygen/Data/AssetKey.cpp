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

#include <xxhash.h>

#include <Oxygen/Data/AssetKey.h>

namespace oxygen::data {

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
