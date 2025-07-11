//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <dxgiformat.h>
#include <windows.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Windows/ComError.h>
#include <Oxygen/Graphics/Common/ObjectRelease.h>
#include <Oxygen/Graphics/Direct3D12/Detail/SwapChain.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/RenderController.h>

namespace {

auto ToNonSrgb(const DXGI_FORMAT format) -> DXGI_FORMAT
{
  switch (format) { // NOLINT(clang-diagnostic-switch-enum)
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    return DXGI_FORMAT_R8G8B8A8_UNORM;
  case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    return DXGI_FORMAT_B8G8R8A8_UNORM;
  case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    return DXGI_FORMAT_B8G8R8X8_UNORM;
  default:
    return format;
  }
}
} // namespace

using oxygen::graphics::d3d12::detail::SwapChain;
using oxygen::windows::ThrowOnFailed;

SwapChain::~SwapChain() noexcept
{
  DLOG_F(INFO, "Release swapchain");
  ReleaseSwapChain();
}

void SwapChain::Present() const
{
  DCHECK_NOTNULL_F(swap_chain_);
  ThrowOnFailed(swap_chain_->Present(1, 0));
  current_back_buffer_index_ = swap_chain_->GetCurrentBackBufferIndex();
}

void SwapChain::UpdateDependencies(
  const std::function<Component&(TypeId)>& get_component) noexcept
{
  using WindowComponent = graphics::detail::WindowComponent;
  window_ = &static_cast<WindowComponent&>(
    get_component(WindowComponent::ClassTypeId()));
}

void SwapChain::CreateSwapChain()
{
  // This method may be called multiple times; therefore, we need to ensure
  // that any remaining resources from previous calls are released first.
  if (swap_chain_ != nullptr) {
    ReleaseSwapChain();
  }

  const DXGI_SWAP_CHAIN_DESC1 swap_chain_desc {
    .Width = window_->Width(),
    .Height = window_->Height(),
    .Format = ToNonSrgb(format_),
    .Stereo = FALSE,
    .SampleDesc = { 1, 0 }, // Always like this for D3D12
    .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_BACK_BUFFER,
    .BufferCount = kFrameBufferCount,
    .Scaling = DXGI_SCALING_STRETCH,
    .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
    .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
    .Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH
      | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING,
  };

  IDXGISwapChain1* swap_chain { nullptr };
  auto* const window_handle
    = static_cast<HWND>(window_->Native().window_handle);
  try {
    const auto& gfx = renderer_->GetGraphics();

    // NB: Misleading argument name for CreateSwapChainForHwnd().
    // For Direct3D 11, and earlier versions of Direct3D, the first argument
    // is a pointer to the Direct3D device for the swap chain. For Direct3D
    // 12 this is a pointer to a direct command queue (refer to
    // ID3D12CommandQueue). This parameter cannot be NULL.
    ThrowOnFailed(gfx.GetFactory()->CreateSwapChainForHwnd(
      command_queue_, // Yes, the command queue, for D3D12
      window_handle, &swap_chain_desc, nullptr, nullptr, &swap_chain));
    ThrowOnFailed(gfx.GetFactory()->MakeWindowAssociation(
      window_handle, DXGI_MWA_NO_ALT_ENTER));
    ThrowOnFailed(swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain_)));
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Failed to create swap chain: {}", e.what());
    ObjectRelease(swap_chain);
    ObjectRelease(swap_chain_);
  }
  ObjectRelease(swap_chain);
}

void SwapChain::ReleaseSwapChain()
{
  ReleaseRenderTargets();
  ObjectRelease(swap_chain_);
}

void SwapChain::AttachRenderer(
  std::shared_ptr<graphics::RenderController> renderer)
{
  CHECK_F(!renderer_, "A renderer is already attached to the swap chain");
  renderer_ = std::static_pointer_cast<RenderController>(std::move(renderer));
  CreateSwapChain();
  CreateRenderTargets();
}

void SwapChain::DetachRenderer()
{
  if (!renderer_) {
    return;
  }

  ReleaseRenderTargets();
  renderer_.reset();
}

void SwapChain::Resize()
{
  DCHECK_NOTNULL_F(renderer_);
  DCHECK_NOTNULL_F(swap_chain_);

  DLOG_F(
    INFO, "Resizing swap chain for window `{}`", window_->GetWindowTitle());

  ReleaseRenderTargets();

  const auto [width, height] = window_->FrameBufferSize();
  try {
    ThrowOnFailed(swap_chain_->ResizeBuffers(kFrameBufferCount, width, height,
      format_, // ToNonSrgb(format_),
      DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
        | DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Failed to resize swap chain: {}", e.what());
  }

  CreateRenderTargets();
}

void SwapChain::CreateRenderTargets()
{
  DCHECK_F(swap_chain_ != nullptr);
  DCHECK_F(render_targets_.size() == 0);

  DXGI_SWAP_CHAIN_DESC1 swap_chain_desc {};
  ThrowOnFailed(swap_chain_->GetDesc1(&swap_chain_desc));

  render_targets_.resize(kFrameBufferCount);
  for (uint32_t i = 0; i < kFrameBufferCount; ++i) {
    ID3D12Resource* back_buffer { nullptr };
    ThrowOnFailed(swap_chain_->GetBuffer(i, IID_PPV_ARGS(&back_buffer)));
    render_targets_[i] = std::make_shared<Texture>(
      TextureDesc {
        .width = swap_chain_desc.Width,
        .height = swap_chain_desc.Height,
        .sample_count = swap_chain_desc.SampleDesc.Count,
        .sample_quality = swap_chain_desc.SampleDesc.Quality,
        .format
        = Format::kRGBA8UNorm, // TODO(abdes): Use the format of the swap chain
        .debug_name = "SwapChain BackBuffer",
        .is_render_target = true,
        .clear_value = Color { 0.0f, 0.0f, 0.0f, 1.0f },
        .initial_state = ResourceStates::kPresent,
      },
      NativeObject(back_buffer, ClassTypeId()), &renderer_->GetGraphics());
  }
}

void SwapChain::ReleaseRenderTargets()
{
  DCHECK_F(swap_chain_ != nullptr);
  render_targets_.clear();
}
