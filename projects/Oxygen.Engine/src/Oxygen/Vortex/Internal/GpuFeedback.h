//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Graphics/Common/ReadbackErrors.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen {
class Graphics;
}
namespace oxygen::graphics {
class Buffer;
struct BufferRange;
class CommandRecorder;
}
namespace oxygen::vortex::internal {
struct GpuFeedbackState;
struct GpuFeedbackSlot;
class GpuFeedbackPool;

enum class FeedbackReserveError : std::uint8_t {
  kBusy,
  kUnavailable,
  kAllocationFailed,
  kBackendFailure,
};
enum class FeedbackPayloadSize : std::uint8_t { kExact, kAtLeast };

//! One CPU-owned feedback delivery. Dropping it abandons delivery; an attached
//! recording still occupies pool capacity until Graphics releases its use.
class GpuFeedbackReservation final {
public:
  GpuFeedbackReservation() = default;
  OXGN_VRTX_API ~GpuFeedbackReservation();
  OXYGEN_MAKE_NON_COPYABLE(GpuFeedbackReservation)
  OXGN_VRTX_API GpuFeedbackReservation(GpuFeedbackReservation&& other) noexcept;
  OXGN_VRTX_API auto operator=(GpuFeedbackReservation&& other) noexcept
    -> GpuFeedbackReservation&;

  OXGN_VRTX_NDAPI auto EnqueueCopy(graphics::CommandRecorder& recorder,
    const graphics::Buffer& source, graphics::BufferRange range)
    -> Result<void, graphics::ReadbackError>;
  //! Enable delivery after Graphics reports an accepted submission.
  OXGN_VRTX_NDAPI auto Commit() noexcept
    -> Result<void, graphics::ReadbackError>;
  OXGN_VRTX_NDAPI auto IsReady() const -> Result<bool, graphics::ReadbackError>;
  OXGN_VRTX_API auto Reset() noexcept -> void;
  [[nodiscard]] explicit operator bool() const noexcept
  {
    return slot_ != nullptr;
  }

  template <typename T>
    requires(
      std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>)
  [[nodiscard]] auto Poll(
    FeedbackPayloadSize size = FeedbackPayloadSize::kExact) noexcept
    -> Result<std::optional<T>, graphics::ReadbackError>
  try {
    T value {};
    const auto ready
      = ReadBytes(std::as_writable_bytes(std::span(&value, 1)), size);
    if (!ready) {
      return Err(ready.error());
    }
    if (!*ready) {
      return Ok(std::optional<T> {});
    }
    return Ok(std::optional<T> { value });
  } catch (const std::exception&) {
    return Err(graphics::ReadbackError::kBackendFailure);
  }

private:
  friend class GpuFeedbackPool;
  GpuFeedbackReservation(
    std::shared_ptr<GpuFeedbackState> state, GpuFeedbackSlot& slot) noexcept;
  OXGN_VRTX_NDAPI auto ReadBytes(std::span<std::byte> destination,
    FeedbackPayloadSize size) -> Result<bool, graphics::ReadbackError>;
  std::shared_ptr<GpuFeedbackState> state_;
  observer_ptr<GpuFeedbackSlot> slot_;
};

//! Fixed-capacity transport; callers retain request identities and choose poll
//! order. CPU operations are serialized by the owning renderer service.
class GpuFeedbackPool final {
public:
  struct Stats {
    std::size_t capacity { 0 };
    std::size_t occupied { 0 };
    std::size_t created_readbacks { 0 };
  };
  OXGN_VRTX_API GpuFeedbackPool(std::shared_ptr<Graphics> graphics,
    std::size_t capacity, std::string_view debug_name);
  OXGN_VRTX_API ~GpuFeedbackPool();
  OXYGEN_MAKE_NON_COPYABLE(GpuFeedbackPool)
  OXYGEN_MAKE_NON_MOVABLE(GpuFeedbackPool)
  OXGN_VRTX_NDAPI auto Reserve() noexcept
    -> Result<GpuFeedbackReservation, FeedbackReserveError>;
  OXGN_VRTX_NDAPI auto InspectStats() const noexcept -> Stats;

private:
  std::shared_ptr<GpuFeedbackState> state_;
};
} // namespace oxygen::vortex::internal
