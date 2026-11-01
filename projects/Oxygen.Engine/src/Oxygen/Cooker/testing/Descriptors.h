//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Serio/ByteReader.h>

namespace oxygen::cooker::testing {

struct DescriptorFixture final {
  std::vector<std::byte> bytes;
  data::AssetReferences references;
};

//! Records in the asset header of the descriptor `bytes` the SHA-256 of
//! its bytes, taken with that field zeroed, as a cook does.
inline auto StampContentHash(std::vector<std::byte>& bytes) -> void
{
  constexpr auto kOffset = offsetof(data::pak::core::AssetHeader, content_hash);
  constexpr auto kSize = sizeof(data::pak::core::AssetHeader::content_hash);
  if (bytes.size() < kOffset + kSize) {
    throw std::invalid_argument("descriptor is smaller than its header");
  }
  const auto field = std::span(bytes).subspan(kOffset, kSize);
  std::ranges::fill(field, std::byte { 0 });
  const auto hash = base::ComputeSha256(bytes);
  std::ranges::copy(std::as_bytes(std::span(hash)), field.begin());
}

template <typename T>
auto AppendRecord(std::vector<std::byte>& bytes, const T& record) -> void
{
  const auto encoded = std::as_bytes(std::span(&record, 1U));
  bytes.insert(bytes.end(), encoded.begin(), encoded.end());
}

//! Resolves a string of the cooked descriptor `bytes` hold.
/*!
 @throw std::invalid_argument when the table or the offset is invalid.
*/
inline auto ReadDescriptorString(const std::span<const std::byte> bytes,
  const data::pak::core::StringTableOffsetT offset) -> std::string
{
  const auto header = serio::CopyAt<data::pak::core::AssetHeader>(bytes);
  if (!header) {
    throw std::invalid_argument("descriptor is smaller than its header");
  }
  return std::string {
    data::DescriptorStrings::FromDescriptor(bytes, header->strings).Get(offset),
  };
}

} // namespace oxygen::cooker::testing
