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

#include <Oxygen/Graphics/Direct3D12/Detail/CompositionSwapChain.h>

using oxygen::windows::ThrowOnFailed;

namespace oxygen::graphics::d3d12::detail {

CompositionSwapChain::CompositionSwapChain(dx::ICommandQueue* command_queue,
  const DXGI_FORMAT format, const Graphics* graphics)
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

  ReleaseRenderTargets();

  try {
    const auto target_width = std::max<uint32_t>(1u, width);
    const auto target_height = std::max<uint32_t>(1u, height);

    ThrowOnFailed(swap_chain_->ResizeBuffers(frame::kFramesInFlight.get(),
      target_width, target_height, format_, 0));

    // DXGI resets the current back buffer to zero after ResizeBuffers. Keep the
    // cached index in sync so we target the correct render target on the next
    // frame and avoid executing command lists against stale buffers.
    current_back_buffer_index_ = swap_chain_->GetCurrentBackBufferIndex();
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

    // Create Texture wrapper
    TextureDesc desc;
    desc.debug_name = fmt::format("BackBuffer{}", i);
    desc.width = 1; // TODO: Update on resize
    desc.height = 1;
    // TODO: Use a proper conversion from DXGI_FORMAT to Format
    desc.format = Format::kRGBA8UNorm;
    desc.is_render_target = true;
    desc.initial_state = ResourceStates::kPresent;

    auto texture = std::make_shared<Texture>(
      desc,
      NativeResource { back_buffer, ClassTypeId() },
      graphics_);

    render_targets_.emplace_back(std::move(texture));
  }
}

auto CompositionSwapChain::ReleaseRenderTargets() -> void
{
  render_targets_.clear();
}

auto CompositionSwapChain::ReleaseSwapChain() -> void
{
  ReleaseRenderTargets();
  ObjectRelease(swap_chain_);
}
} // namespace oxygen::graphics::d3d12::detail
