//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <stdexcept>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Renderer/RenderContext.h>

namespace oxygen::engine {

// Small header-only utility that centralizes per-frame RenderContext pool
// management. This encapsulates the fixed-size array of per-frame
// RenderContext instances and the atomic 'in-use' markers that guarded the
// previous implementation in Renderer.
class RenderContextPool {
public:
  RenderContextPool()
  {
    DLOG_F(2, "RenderContextPool constructed successfully.");
  }
  RenderContextPool(const RenderContextPool&) = delete;
  RenderContextPool& operator=(const RenderContextPool&) = delete;
  RenderContextPool(RenderContextPool&&) = default;
  RenderContextPool& operator=(RenderContextPool&&) = default;

  ~RenderContextPool() { DLOG_F(2, "RenderContextPool was destroyed."); }

  // Claim a context for a specific frame slot. Throws if the slot is already
  // in use.
  auto Acquire(frame::Slot slot) -> RenderContext&
  {
    const auto idx = static_cast<std::size_t>(slot.get());
    bool expected = false;
    if (!in_use_[idx].compare_exchange_strong(expected, true)) {
      LOG_F(WARNING,
        "Failed to acquire RenderContext: slot {0} is already in use.", idx);
      throw std::runtime_error(
        "RenderContextPool::Acquire: slot already in use");
    }

    // Reset the context to a clean state before returning.
    pool_[idx].Reset();
    DLOG_F(2, "RenderContextPool successfully acquired slot {0}.", idx);
    return pool_[idx];
  }

  // Release the claimed context for the given slot and reset the in-use flag.
  auto Release(frame::Slot slot) -> void
  {
    const auto idx = static_cast<std::size_t>(slot.get());
    pool_[idx].Reset();
    in_use_[idx].store(false);
    DLOG_F(2, "RenderContextPool released slot {0}.", idx);
  }

  // Read-only check whether a slot is currently claimed.
  [[nodiscard]] auto IsInUse(frame::Slot slot) const noexcept -> bool
  {
    const auto idx = static_cast<std::size_t>(slot.get());
    return in_use_[idx].load();
  }

private:
  // Fixed size arrays sized by frame::kFramesInFlight — the engine guarantees
  // this is a small compile-time constant.
  std::array<RenderContext, frame::kFramesInFlight.get()> pool_ {};
  std::array<std::atomic_bool,
    static_cast<std::size_t>(frame::kFramesInFlight.get())>
    in_use_ {};
};

} // namespace oxygen::engine
