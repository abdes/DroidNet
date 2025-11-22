//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <type_traits>

#include <algorithm>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Direct3D12/Constants.h>
#include <Oxygen/Graphics/Direct3D12/Detail/CompositionSwapChain.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>

namespace oxygen::graphics::d3d12 {
class Graphics;
}

namespace oxygen::graphics::d3d12::detail {

class CompositionSurface : public graphics::Surface {

public:
  CompositionSurface(dx::ICommandQueue* command_queue,
    const Graphics* graphics)
    : graphics::Surface("CompositionSurface")
  {
    AddComponent<CompositionSwapChain>(
      command_queue, kDefaultBackBufferFormat, graphics);
  }

  ~CompositionSurface() override = default;

  OXYGEN_MAKE_NON_COPYABLE(CompositionSurface);
  OXYGEN_DEFAULT_MOVABLE(CompositionSurface);

  [[nodiscard]] auto GetSwapChain() const
  {
    return GetComponent<CompositionSwapChain>().GetSwapChain();
  }

  auto RequestResize(uint32_t width, uint32_t height) const -> void
  {
    pending_width_.store(width, std::memory_order_relaxed);
    pending_height_.store(height, std::memory_order_relaxed);
    resize_pending_.store(true, std::memory_order_release);
  }

  auto GetCurrentBackBufferIndex() const -> uint32_t override
  {
    return GetComponent<CompositionSwapChain>().GetCurrentBackBufferIndex();
  }

  auto GetCurrentBackBuffer() const
    -> std::shared_ptr<graphics::Texture> override
  {
    ApplyPendingResize();
    return GetComponent<CompositionSwapChain>().GetCurrentBackBuffer();
  }
  auto GetBackBuffer(uint32_t index) const
    -> std::shared_ptr<graphics::Texture> override
  {
    ApplyPendingResize();
    return GetComponent<CompositionSwapChain>().GetBackBuffer(index);
  }

  void Present() const override
  {
    ApplyPendingResize();
    GetComponent<CompositionSwapChain>().Present();
  }

  void Resize() override { Resize(0, 0); }

  void Resize(uint32_t width, uint32_t height)
  {
    const auto target_width = std::max<uint32_t>(1u, width);
    const auto target_height = std::max<uint32_t>(1u, height);
    GetComponent<CompositionSwapChain>().Resize(target_width, target_height);
    ShouldResize(false);
  }

  [[nodiscard]] auto Width() const -> uint32_t override
  {
    return 0;
  } // TODO: Implement size tracking
  [[nodiscard]] auto Height() const -> uint32_t override
  {
    return 0;
  } // TODO: Implement size tracking

private:
  auto ApplyPendingResize() const -> void
  {
    if (!resize_pending_.exchange(false, std::memory_order_acq_rel)) {
      return;
    }

    const auto width = pending_width_.load(std::memory_order_acquire);
    const auto height = pending_height_.load(std::memory_order_acquire);
    const auto clamped_width = std::max<uint32_t>(1u, width);
    const auto clamped_height = std::max<uint32_t>(1u, height);
    const_cast<CompositionSurface*>(this)->Resize(clamped_width,
      clamped_height);
  }

  mutable std::atomic<bool> resize_pending_ { false };
  mutable std::atomic<uint32_t> pending_width_ { 1 };
  mutable std::atomic<uint32_t> pending_height_ { 1 };
};

} // namespace oxygen::graphics::d3d12::detail
