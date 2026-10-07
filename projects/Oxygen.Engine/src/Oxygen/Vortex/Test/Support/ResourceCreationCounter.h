//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

#include <Oxygen/Core/Types/ByteUnits.h>

namespace oxygen::vortex::testing {

struct ResourceCreationCounts {
  std::uint64_t buffers { 0U };
  std::uint64_t textures { 0U };
  //! Requested buffer bytes, not alignment, heap growth or VRAM residency.
  SizeBytes requested_buffer_bytes { 0U };
};

//! Fixed-storage counters for successful resource factory calls in tests.
//! Concurrent writers are supported. Read at quiescent boundaries when exact
//! per-window totals are required; this is not a transactional snapshot.
class ResourceCreationCounter final {
public:
  auto RecordBuffer(SizeBytes requested_bytes) noexcept -> void;
  auto RecordTexture() noexcept -> void;
  //! Zero every count (fixtures that reuse a backend across tests).
  auto Reset() noexcept -> void
  {
    buffers_.store(0U);
    textures_.store(0U);
    requested_buffer_bytes_.store(0U);
    overflow_.store(false);
  }
  [[nodiscard]] auto Snapshot() const noexcept
    -> std::optional<ResourceCreationCounts>;

private:
  auto Add(std::atomic<std::uint64_t>& counter, std::uint64_t amount) noexcept
    -> void;
  std::atomic<std::uint64_t> buffers_ { 0U };
  std::atomic<std::uint64_t> textures_ { 0U };
  std::atomic<std::uint64_t> requested_buffer_bytes_ { 0U };
  std::atomic<bool> overflow_ { false };
};

} // namespace oxygen::vortex::testing
