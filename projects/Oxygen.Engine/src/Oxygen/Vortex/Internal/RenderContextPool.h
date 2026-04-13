//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <type_traits>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/Frame.h>

namespace oxygen::vortex {
struct RenderContext;
} // namespace oxygen::vortex

namespace oxygen::vortex::internal {

// Header-only utility that centralizes per-frame render-context pooling.
// The template keeps this file movable into Vortex before RenderContext.h
// lands in 01-10; the concrete alias becomes usable once the root type exists.
template <typename RenderContextT> class BasicRenderContextPool {
public:
  BasicRenderContextPool()
  {
    DLOG_F(2, "RenderContextPool constructed successfully.");
  }
  BasicRenderContextPool(const BasicRenderContextPool&) = delete;
  auto operator=(const BasicRenderContextPool&)
    -> BasicRenderContextPool& = delete;
  BasicRenderContextPool(BasicRenderContextPool&&) = default;
  auto operator=(BasicRenderContextPool&&) -> BasicRenderContextPool&
    = default;

  ~BasicRenderContextPool()
  {
    DLOG_F(2, "RenderContextPool was destroyed.");
  }

  auto Acquire(frame::Slot slot) -> RenderContextT&
  {
    static_assert(std::is_default_constructible_v<RenderContextT>);
    static_assert(requires(RenderContextT& context) { context.Reset(); });

    const auto idx = static_cast<std::size_t>(slot.get());
    bool expected = false;
    if (!in_use_[idx].compare_exchange_strong(expected, true)) {
      LOG_F(WARNING,
        "Failed to acquire RenderContext: slot {0} is already in use.", idx);
      throw std::runtime_error(
        "RenderContextPool::Acquire: slot already in use");
    }

    pool_[idx].Reset();
    DLOG_F(2, "RenderContextPool successfully acquired slot {0}.", idx);
    return pool_[idx];
  }

  auto Release(frame::Slot slot) -> void
  {
    const auto idx = static_cast<std::size_t>(slot.get());
    pool_[idx].Reset();
    in_use_[idx].store(false);
    DLOG_F(2, "RenderContextPool released slot {0}.", idx);
  }

  [[nodiscard]] auto IsInUse(frame::Slot slot) const noexcept -> bool
  {
    const auto idx = static_cast<std::size_t>(slot.get());
    return in_use_[idx].load();
  }

private:
  std::array<RenderContextT, frame::kFramesInFlight.get()> pool_ {};
  std::array<std::atomic_bool,
    static_cast<std::size_t>(frame::kFramesInFlight.get())>
    in_use_ {};
};

using RenderContextPool = BasicRenderContextPool<RenderContext>;

} // namespace oxygen::vortex::internal
