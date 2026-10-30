//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <Oxygen/Base/api_export.h>

namespace oxygen::base {

//! Incremental IEEE CRC-32 (the zlib, PNG and pak footer checksum).
/*!
 Reflected polynomial `0xEDB88320`, initial value and final XOR `0xFFFFFFFF`,
 computed by carry-less multiplication folding (PCLMULQDQ) when the CPU has
 it and eight bytes at a time (slicing-by-8) otherwise. Feeding the data in
 any number of pieces gives the same value as one call over all of it.
*/
class Crc32 final {
public:
  //! Check if the CPU has the carry-less multiplication the fast path uses.
  OXGN_BASE_NDAPI static auto HasHardwareSupport() noexcept -> bool;

  //! Continue the checksum with `data`.
  OXYGEN_BASE_API auto Update(std::span<const std::byte> data) noexcept -> void;

  //! The checksum of every byte given so far. The state is kept, so more
  //! bytes may follow.
  OXGN_BASE_NDAPI auto Finalize() const noexcept -> uint32_t;

private:
  uint32_t state_ = 0xFFFFFFFFU;
};

//! Compute the IEEE CRC-32 of a memory buffer in one call.
OXGN_BASE_NDAPI auto ComputeCrc32(std::span<const std::byte> data) noexcept
  -> uint32_t;

} // namespace oxygen::base
