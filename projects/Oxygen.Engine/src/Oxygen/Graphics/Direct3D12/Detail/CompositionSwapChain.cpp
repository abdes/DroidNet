//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <dxgiformat.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Windows/ComError.h>
#include <Oxygen/Graphics/Common/ObjectRelease.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/Texture.h>
#include <Oxygen/Graphics/Common/DeferredObjectRelease.h>

#include <Oxygen/Graphics/Direct3D12/Detail/CompositionSwapChain.h>

using oxygen::windows::ThrowOnFailed;

namespace oxygen::graphics::d3d12::detail {

CompositionSwapChain::CompositionSwapChain(dx::ICommandQueue* command_queue,
  const DXGI_FORMAT format, Graphics* graphics)
  : format_(format)
  , command_queue_(command_queue)
  , graphics_(graphics)
{
  CreateSwapChain();
  CreateRenderTargets();
}

CompositionSwapChain::~CompositionSwapChain() noexcept
{
  ReleaseSwapChain();
}

auto CompositionSwapChain::Present() const -> void
{
  if (swap_chain_) {
    DLOG_F(3, "CompositionSwapChain::Present swap_chain={} current_index={}", fmt::ptr(swap_chain_), current_back_buffer_index_);
    ThrowOnFailed(swap_chain_->Present(1, 0));
    current_back_buffer_index_ = swap_chain_->GetCurrentBackBufferIndex();
  }
}

auto CompositionSwapChain::CreateSwapChain() -> void
{
  if (swap_chain_) {
    ReleaseSwapChain();
  }

  DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
  swap_chain_desc.Width = 1; // Initial size, will be resized
  swap_chain_desc.Height = 1;
  swap_chain_desc.Format = format_;
  swap_chain_desc.Stereo = false;
  swap_chain_desc.SampleDesc.Count = 1;
  swap_chain_desc.SampleDesc.Quality = 0;
  swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swap_chain_desc.BufferCount = frame::kFramesInFlight.get();
  swap_chain_desc.Scaling = DXGI_SCALING_STRETCH;
  swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swap_chain_desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
  swap_chain_desc.Flags = 0;

  IDXGISwapChain1* swap_chain = nullptr;
  ThrowOnFailed(graphics_->GetFactory()->CreateSwapChainForComposition(
    command_queue_, &swap_chain_desc, nullptr, &swap_chain));

  ThrowOnFailed(swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain_)));
  ObjectRelease(swap_chain);

  current_back_buffer_index_ = swap_chain_->GetCurrentBackBufferIndex();
}

auto CompositionSwapChain::Resize(uint32_t width, uint32_t height) -> void
{
  if (!swap_chain_) {
    return;
  }

  DLOG_F(2, "CompositionSwapChain::Resize requested target={}x{} for swap_chain={}", width, height, fmt::ptr(swap_chain_));

  // ResizeBuffers requires that all outstanding references to buffers are
  // released. Force immediate release of our render target wrappers here so
  // there are no outstanding back-buffer references when calling ResizeBuffers.
  ReleaseRenderTargets(true);

  try {
    const auto target_width = std::max<uint32_t>(1u, width);
    const auto target_height = std::max<uint32_t>(1u, height);

    ThrowOnFailed(swap_chain_->ResizeBuffers(frame::kFramesInFlight.get(),
      target_width, target_height, format_, 0));

    // DXGI resets the current back buffer to zero after ResizeBuffers. Keep the
    // cached index in sync so we target the correct render target on the next
    // frame and avoid executing command lists against stale buffers.
    current_back_buffer_index_ = swap_chain_->GetCurrentBackBufferIndex();
    DLOG_F(2, "CompositionSwapChain::Resize completed new backbuffer_index={} for swap_chain={}", current_back_buffer_index_, fmt::ptr(swap_chain_));
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Failed to resize swap chain: {}", e.what());
  }

  CreateRenderTargets();
}

auto CompositionSwapChain::CreateRenderTargets() -> void
{
  DCHECK_NOTNULL_F(swap_chain_);
  DCHECK_F(render_targets_.empty());

  for (uint32_t i = 0; i < frame::kFramesInFlight.get(); ++i) {
    ID3D12Resource* back_buffer { nullptr };
    ThrowOnFailed(swap_chain_->GetBuffer(i, IID_PPV_ARGS(&back_buffer)));

    // Create Texture wrapper - query actual resource dimensions from the
    // native resource rather than using the placeholder 1x1 size.
    D3D12_RESOURCE_DESC native_desc = back_buffer->GetDesc();

    DLOG_F(2, "CompositionSwapChain::CreateRenderTargets created native backbuffer[{}] size={}x{} for swap_chain={}", i, static_cast<uint32_t>(std::min<uint64_t>(native_desc.Width, UINT32_MAX)), static_cast<uint32_t>(native_desc.Height), fmt::ptr(swap_chain_));

    TextureDesc desc;
    desc.debug_name = fmt::format("BackBuffer{}", i);
    // Width in D3D12_RESOURCE_DESC is 64-bit; clamp to 32-bit here.
    desc.width = static_cast<uint32_t>(std::min<uint64_t>(native_desc.Width, UINT32_MAX));
    desc.height = static_cast<uint32_t>(native_desc.Height);
    // TODO: Use a proper conversion from DXGI_FORMAT to Format
    desc.format = Format::kRGBA8UNorm;
    desc.is_render_target = true;
    desc.initial_state = ResourceStates::kPresent;

    auto texture = std::make_shared<Texture>(
      desc,
      NativeResource { back_buffer, ClassTypeId() },
      graphics_);
    DLOG_F(3, "CompositionSwapChain::CreateRenderTargets created texture[{}] size={}x{}", i, desc.width, desc.height);
    render_targets_.emplace_back(std::move(texture));
  }
}

auto CompositionSwapChain::ReleaseRenderTargets(bool immediate) -> void
{
  if (render_targets_.empty()) {
    return;
  }

  if (immediate) {
    // Immediate release: clear shared_ptrs so GetBuffer / ResizeBuffers will
    // not be blocked by outstanding references.
    DLOG_F(2, "CompositionSwapChain::ReleaseRenderTargets immediate clear (render_targets={})", render_targets_.size());
    render_targets_.clear();
    return;
  }

  // If we have a valid graphics instance, use the DeferredReclaimer so the
  // D3D back-buffer resources are not final-released while the GPU may still
  // have them referenced. This avoids OBJECT_DELETED_WHILE_STILL_IN_USE races
  // that occur under rapid resize/unregister scenarios.
  if (graphics_ != nullptr) {
    try {
      auto& reclaimer = static_cast<oxygen::Graphics*>(graphics_)->GetDeferredReclaimer();
      for (auto& rt : render_targets_) {
        // Transfer ownership to the deferred reclaimer; the texture shared_ptr
        // will be reset here but its underlying resource will be released
        // later when it's safe to do so (frame slot cycles).
        oxygen::graphics::DeferredObjectRelease(rt, reclaimer);
      }
    }
    catch (const std::exception& e) {
      LOG_F(WARNING, "ReleaseRenderTargets: deferred release failed, falling back to immediate clear: {}", e.what());
      render_targets_.clear();
      return;
    }
  }
  else {
    // No graphics reclaimer available - fall back to immediate drop.
    render_targets_.clear();
    return;
  }

  // Container should be empty once ownership has been transferred.
  render_targets_.clear();
}

auto CompositionSwapChain::ReleaseSwapChain() -> void
{
  ReleaseRenderTargets();

  if (swap_chain_ == nullptr) {
    return;
  }

  // Defer releasing the IDXGISwapChain object via the DeferredReclaimer when
  // possible so the final COM Release() happens only after the frame/timeline
  // indicates it is safe.
  if (graphics_ != nullptr) {
    try {
      auto& reclaimer = static_cast<oxygen::Graphics*>(graphics_)->GetDeferredReclaimer();
      oxygen::graphics::DeferredObjectRelease(swap_chain_, reclaimer);
    }
    catch (const std::exception& e) {
      LOG_F(WARNING, "ReleaseSwapChain: deferred release failed, calling immediate Release(): {}", e.what());
      ObjectRelease(swap_chain_);
    }
  }
  else {
    ObjectRelease(swap_chain_);
  }
}
} // namespace oxygen::graphics::d3d12::detail
