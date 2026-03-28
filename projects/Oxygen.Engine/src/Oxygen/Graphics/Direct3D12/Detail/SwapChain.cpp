//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <dxgiformat.h>
#include <windows.h>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Windows/ComError.h>
#include <Oxygen/Graphics/Common/ObjectRelease.h>
#include <Oxygen/Graphics/Direct3D12/Detail/SwapChain.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>

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

auto SupportsAllowTearing(oxygen::graphics::d3d12::dx::IFactory* factory)
  -> bool
{
  if (factory == nullptr) {
    return false;
  }

  BOOL allow_tearing = FALSE;
  return SUCCEEDED(
           factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
             &allow_tearing, sizeof(allow_tearing)))
    && allow_tearing != FALSE;
}
} // namespace

using oxygen::graphics::d3d12::detail::SwapChain;
using oxygen::windows::ThrowOnFailed;

SwapChain::~SwapChain() noexcept
{
  DLOG_F(INFO, "Release swapchain");
  ReleaseSwapChain();
}

auto SwapChain::GetCurrentBackBuffer() const -> std::shared_ptr<Texture>
{
  return render_targets_[current_back_buffer_index_];
}

auto SwapChain::Present() const -> void
{
  DCHECK_NOTNULL_F(swap_chain_);
  // Use sync_interval of 1 for V-Sync enabled, 0 for V-Sync disabled
  const UINT sync_interval = graphics_->IsVSyncEnabled() ? 1U : 0U;
  ThrowOnFailed(swap_chain_->Present(sync_interval, 0));
  current_back_buffer_index_
    = (current_back_buffer_index_ + 1U) % frame::kFramesInFlight.get();
}

auto SwapChain::UpdateDependencies(
  const std::function<Component&(TypeId)>& get_component) noexcept -> void
{
  using WindowComponent = graphics::detail::WindowComponent;
  window_ = &static_cast<WindowComponent&>(
    get_component(WindowComponent::ClassTypeId()));

  // Initialize SwapChain now that window is available
  if (window_ && !swap_chain_) {
    CreateSwapChain();
    if (swap_chain_ != nullptr) {
      CreateRenderTargets();
    }
  }
}

auto SwapChain::CreateSwapChain() -> void
{
  // This method may be called multiple times; therefore, we need to ensure
  // that any remaining resources from previous calls are released first.
  if (swap_chain_ != nullptr) {
    ReleaseSwapChain();
  }

  const auto [framebuffer_width, framebuffer_height]
    = window_->FrameBufferSize();
  const auto width = std::max<uint32_t>(1U, framebuffer_width);
  const auto height = std::max<uint32_t>(1U, framebuffer_height);

  auto* const window_handle
    = static_cast<HWND>(window_->Native().window_handle);
  if (window_handle == nullptr || ::IsWindow(window_handle) == FALSE) {
    throw std::runtime_error("Failed to create swap chain: invalid HWND");
  }

  swap_chain_flags_ = SupportsAllowTearing(graphics_->GetFactory())
    ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
    : 0U;

  const DXGI_SWAP_CHAIN_DESC1 swap_chain_desc {
    .Width = 0,
    .Height = 0,
    .Format = ToNonSrgb(format_),
    .Stereo = FALSE,
    .SampleDesc = { 1, 0 }, // Always like this for D3D12
    .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
    .BufferCount = frame::kFramesInFlight.get(),
    .Scaling = DXGI_SCALING_STRETCH,
    .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
    .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
    .Flags = swap_chain_flags_,
  };

  dx::ISwapChain* swap_chain { nullptr };
  try {
    // NB: Misleading argument name for CreateSwapChainForHwnd().
    // For Direct3D 11, and earlier versions of Direct3D, the first argument
    // is a pointer to the Direct3D device for the swap chain. For Direct3D
    // 12 this is a pointer to a direct command queue (refer to
    // ID3D12CommandQueue). This parameter cannot be NULL.
    auto create_desc = swap_chain_desc;
    HRESULT hr = graphics_->GetFactory()->CreateSwapChainForHwnd(
      command_queue_, // Yes, the command queue, for D3D12
      window_handle, &create_desc, nullptr, nullptr, &swap_chain);
    if (FAILED(hr) && create_desc.Flags != 0U) {
      LOG_F(WARNING,
        "CreateSwapChainForHwnd failed with flags=0x{:X}; retrying without "
        "optional flags",
        create_desc.Flags);
      create_desc.Flags = 0U;
      hr = graphics_->GetFactory()->CreateSwapChainForHwnd(command_queue_,
        window_handle, &create_desc, nullptr, nullptr, &swap_chain);
    }
    if (FAILED(hr)) {
      LOG_F(ERROR,
        "CreateSwapChainForHwnd hr=0x{:08X} hwnd={} framebuffer={}x{} "
        "desc(width={}, height={}, format={}, usage=0x{:X}, buffers={}, "
        "scaling={}, swap_effect={}, flags=0x{:X})",
        static_cast<unsigned int>(hr), fmt::ptr(window_handle), width, height,
        create_desc.Width, create_desc.Height,
        static_cast<unsigned int>(create_desc.Format),
        static_cast<unsigned int>(create_desc.BufferUsage),
        create_desc.BufferCount, static_cast<unsigned int>(create_desc.Scaling),
        static_cast<unsigned int>(create_desc.SwapEffect), create_desc.Flags);
    }
    ThrowOnFailed(hr,
      fmt::format(
        "Failed to create HWND swap chain (hwnd={}, size={}x{}, flags=0x{:X})",
        fmt::ptr(window_handle), width, height, create_desc.Flags));

    swap_chain_flags_ = create_desc.Flags;
    ThrowOnFailed(graphics_->GetFactory()->MakeWindowAssociation(
      window_handle, DXGI_MWA_NO_ALT_ENTER));
    swap_chain_ = swap_chain;
    swap_chain = nullptr;
    current_back_buffer_index_ = 0U;
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Failed to create swap chain: {}", e.what());
    ObjectRelease(swap_chain);
    ObjectRelease(swap_chain_);
    swap_chain_flags_ = 0U;
    current_back_buffer_index_ = 0U;
  }
  ObjectRelease(swap_chain);
}

auto SwapChain::ReleaseSwapChain() -> void
{
  ReleaseRenderTargets();
  ObjectRelease(swap_chain_);
  swap_chain_flags_ = 0U;
  current_back_buffer_index_ = 0U;
}

auto SwapChain::Resize() -> void
{
  DCHECK_NOTNULL_F(graphics_);
  DCHECK_NOTNULL_F(swap_chain_);

  DLOG_F(
    INFO, "Resizing swap chain for window `{}`", window_->GetWindowTitle());

  ReleaseRenderTargets();

  const auto [width, height] = window_->FrameBufferSize();
  try {
    ThrowOnFailed(swap_chain_->ResizeBuffers(frame::kFramesInFlight.get(),
      std::max<uint32_t>(1U, width), std::max<uint32_t>(1U, height), format_,
      swap_chain_flags_));
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Failed to resize swap chain: {}", e.what());
    return;
  }

  current_back_buffer_index_ = 0U;

  CreateRenderTargets();
}

auto SwapChain::CreateRenderTargets() -> void
{
  DCHECK_F(swap_chain_ != nullptr);
  DCHECK_F(render_targets_.size() == 0);

  DXGI_SWAP_CHAIN_DESC1 swap_chain_desc {};
  ThrowOnFailed(swap_chain_->GetDesc1(&swap_chain_desc));

  render_targets_.resize(frame::kFramesInFlight.get());
  for (uint32_t i = 0; i < frame::kFramesInFlight.get(); ++i) {
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
      NativeResource(back_buffer, ClassTypeId()), graphics_);
  }
  DLOG_F(
    INFO, "Created {} render targets for swap chain", render_targets_.size());
}

auto SwapChain::ReleaseRenderTargets() -> void
{
  if (swap_chain_ == nullptr) {
    render_targets_.clear();
    return;
  }
  DLOG_F(
    INFO, "Releasing {} render targets for swap chain", render_targets_.size());
  render_targets_.clear();
}
