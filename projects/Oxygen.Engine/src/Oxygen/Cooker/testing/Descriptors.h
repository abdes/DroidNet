//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Serio/ByteReader.h>

namespace oxygen::cooker::testing {

struct DescriptorFixture final {
  std::vector<std::byte> bytes;
  data::AssetReferences references;
};

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
