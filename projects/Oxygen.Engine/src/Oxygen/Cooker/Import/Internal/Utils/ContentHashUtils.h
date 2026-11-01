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

[[nodiscard]] inline auto ComputeContentSha256(
  const std::span<const std::byte> bytes) -> base::Sha256Digest
{
  return base::ComputeSha256(bytes);
}

[[nodiscard]] inline auto IsZeroContentSha256(const base::Sha256Digest& digest)
  -> bool
{
  return base::IsAllZero(digest);
}

} // namespace oxygen::content::import::util
