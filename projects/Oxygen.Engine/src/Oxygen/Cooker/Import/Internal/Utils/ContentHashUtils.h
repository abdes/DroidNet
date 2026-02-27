//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::import::util {

//! Computes truncated 8-byte content hash from data.
/*!
 @param bytes The raw data bytes.
 @return First 8 bytes of SHA256 as uint64_t.
*/
[[nodiscard]] inline auto ComputeContentHash(
  const std::span<const std::byte> bytes) -> uint64_t
{
  const auto digest = base::ComputeSha256(bytes);
  uint64_t hash = 0;
  for (size_t i = 0; i < 8; ++i) {
    hash |= static_cast<uint64_t>(digest[i]) << (i * 8);
  }
  return hash;
}

} // namespace oxygen::content::import::util
