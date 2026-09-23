//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>

#include <Oxygen/Core/Types/ByteUnits.h>
#include <Oxygen/Vortex/Test/Support/ResourceCreationCounter.h>

namespace oxygen::vortex::testing {

auto ResourceCreationCounter::Add(std::atomic<std::uint64_t>& counter,
  const std::uint64_t amount) noexcept -> void
{
  const auto previous = counter.fetch_add(amount, std::memory_order_relaxed);
  if (previous > std::numeric_limits<std::uint64_t>::max() - amount) {
    overflow_.store(true, std::memory_order_release);
  }
}

auto ResourceCreationCounter::RecordBuffer(
  const SizeBytes requested_bytes) noexcept -> void
{
  Add(buffers_, 1U);
  Add(requested_buffer_bytes_, requested_bytes.get());
}

auto ResourceCreationCounter::RecordTexture() noexcept -> void
{
  Add(textures_, 1U);
}

auto ResourceCreationCounter::Snapshot() const noexcept
  -> std::optional<ResourceCreationCounts>
{
  const auto snapshot = ResourceCreationCounts {
    .buffers = buffers_.load(std::memory_order_relaxed),
    .textures = textures_.load(std::memory_order_relaxed),
    .requested_buffer_bytes
    = SizeBytes { requested_buffer_bytes_.load(std::memory_order_relaxed) },
  };
  if (overflow_.load(std::memory_order_acquire)) {
    return std::nullopt;
  }
  return snapshot;
}

} // namespace oxygen::vortex::testing
