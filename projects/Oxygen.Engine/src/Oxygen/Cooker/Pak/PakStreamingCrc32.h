//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::pak {

class PakStreamingCrc32 final {
public:
  struct Config final {
    bool enabled = true;
    uint64_t skip_offset = 0;
    uint64_t skip_size = 0;
  };

  explicit PakStreamingCrc32(Config config) noexcept;

  OXGN_COOK_NDAPI auto Enabled() const noexcept -> bool;
  OXGN_COOK_NDAPI auto Update(uint64_t absolute_offset,
    std::span<const std::byte> bytes) noexcept -> bool;
  OXGN_COOK_NDAPI auto SkippedByteCount() const noexcept -> uint64_t;
  OXGN_COOK_NDAPI auto Finalize() const noexcept -> uint32_t;

private:
  [[nodiscard]] static auto UpdateCrc32Ieee(
    std::span<const std::byte> bytes, uint32_t state) noexcept -> uint32_t;

  Config config_ {};
  uint32_t state_ = 0xFFFFFFFFU;
  uint64_t skipped_bytes_ = 0;
};

} // namespace oxygen::content::pak
