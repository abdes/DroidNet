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
  CompositionSurface(dx::ICommandQueue* command_queue, Graphics* graphics)
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
    // Mirror the resize intent onto the public Surface flag so engine modules
    // (which check Surface::ShouldResize()) will pick up and apply the
    // explicit Resize() call during frame start. RequestResize is const to
    // allow callers who only hold a const reference to the surface; we
    // therefore cast away constness to update the mutable engine-visible flag.
    const_cast<CompositionSurface*>(this)->ShouldResize(true);
  }

  auto GetCurrentBackBufferIndex() const -> uint32_t override
  {
    return GetComponent<CompositionSwapChain>().GetCurrentBackBufferIndex();
  }

  auto GetCurrentBackBuffer() const
    -> std::shared_ptr<graphics::Texture> override
  {
    // Do not apply pending resize implicitly here - applies must be
    // explicitly triggered by the engine module at frame start by calling
    // Resize().
    return GetComponent<CompositionSwapChain>().GetCurrentBackBuffer();
  }
  auto GetBackBuffer(uint32_t index) const
    -> std::shared_ptr<graphics::Texture> override
  {
    // No implicit resize here - engine must call Resize() explicitly.
    return GetComponent<CompositionSwapChain>().GetBackBuffer(index);
  }

  void Present() const override
  {
    // Present should not apply pending resizes. Resize application is an
    // explicit engine-module responsibility executed at frame start.
    GetComponent<CompositionSwapChain>().Present();
  }

  void Resize() override
  {
    // Apply any pending resize request set by RequestResize(). If there is
    // no pending request, no-op. This keeps resize application explicit and
    // only performed when called by the engine module at frame start.
    if (!resize_pending_.exchange(false, std::memory_order_acq_rel)) {
      return;
    }

    const auto width = pending_width_.load(std::memory_order_acquire);
    const auto height = pending_height_.load(std::memory_order_acquire);
    const auto clamped_width = std::max<uint32_t>(1u, width);
    const auto clamped_height = std::max<uint32_t>(1u, height);
    Resize(clamped_width, clamped_height);
  }

  void Resize(uint32_t width, uint32_t height)
  {
    const auto target_width = std::max<uint32_t>(1u, width);
    const auto target_height = std::max<uint32_t>(1u, height);
    GetComponent<CompositionSwapChain>().Resize(target_width, target_height);
    ShouldResize(false);
  }

  [[nodiscard]] auto Width() const -> uint32_t override
  {
    // Return the actual width of the swap chain backbuffers
    auto bb = GetComponent<CompositionSwapChain>().GetBackBuffer(0);
    return bb ? bb->GetDescriptor().width : 0;
  }
  [[nodiscard]] auto Height() const -> uint32_t override
  {
    // Return the actual height of the swap chain backbuffers
    auto bb = GetComponent<CompositionSwapChain>().GetBackBuffer(0);
    return bb ? bb->GetDescriptor().height : 0;
  }

private:
  mutable std::atomic<bool> resize_pending_ { false };
  mutable std::atomic<uint32_t> pending_width_ { 1 };
  mutable std::atomic<uint32_t> pending_height_ { 1 };
};

} // namespace oxygen::graphics::d3d12::detail
