
#include "Oxygen/Renderers/Direct3d12/Detail/WindowSurfaceImpl.h"

#include "dx12_utils.h"

using oxygen::renderer::d3d12::detail::WindowSurfaceImpl;

namespace {

  DXGI_FORMAT ToNonSrgb(const DXGI_FORMAT format)
  {
    switch (format) {  // NOLINT(clang-diagnostic-switch-enum)
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
}  // namespace

void WindowSurfaceImpl::SetSize(int width, int height)
{
  // TODO: Implement
}

void WindowSurfaceImpl::Present() const
{
  DCHECK_NOTNULL_F(swap_chain_);
  ThrowOnFailed(swap_chain_->Present(0, 0));
  current_backbuffer_index_ = swap_chain_->GetCurrentBackBufferIndex();
}

void WindowSurfaceImpl::CreateSwapChain(const DXGI_FORMAT format)
{
  // This method may be called multiple times, therefore we need to ensure that
  // any remaining resources from previous calls are released first.
  if (swap_chain_) DoRelease();

  // Remember the format used during swap-chain creation, and use it for the
  // render target creation in Finalize()
  format_ = format;

  const auto window = window_.lock();
  CHECK_NOTNULL_F(window, "window is not valid");

  const DXGI_SWAP_CHAIN_DESC1 swap_chain_desc{
        .Width = Width(),
        .Height = Height(),
        .Format = ToNonSrgb(format),
        .Stereo = FALSE,
        .SampleDesc = {1, 0},
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = kFrameBufferCount,
        .Scaling = DXGI_SCALING_STRETCH,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
        .Flags = 0
  };

  IDXGISwapChain1* swap_chain{ nullptr };
  const auto hwnd = static_cast<HWND>(window->NativeWindow().window_handle);
  try {
    ThrowOnFailed(
      GetFactory()->CreateSwapChainForHwnd(
        command_queue_,
        hwnd,
        &swap_chain_desc,
        nullptr,
        nullptr,
        &swap_chain));
    ThrowOnFailed(GetFactory()->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));
    ThrowOnFailed(swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain_)));
  }
  catch (const std::exception& e) {
    LOG_F(ERROR, "Failed to create swap chain: {}", e.what());
    ObjectRelease(swap_chain);
    ObjectRelease(swap_chain_);
  }
  ObjectRelease(swap_chain);

  current_backbuffer_index_ = swap_chain_->GetCurrentBackBufferIndex();

  for (auto& [resource, rtv] : render_targets_) {
    rtv = GetRenderer().RtvHeap().Allocate();
  }
  Finalize();
}

void WindowSurfaceImpl::Finalize()
{
  for (uint32_t i = 0; i < kFrameBufferCount; ++i) {
    DCHECK_F(render_targets_[i].resource == nullptr);
    ID3D12Resource* back_buffer{ nullptr };
    try {
      ThrowOnFailed(swap_chain_->GetBuffer(i, IID_PPV_ARGS(&back_buffer)));
      render_targets_[i].resource = back_buffer;
      const D3D12_RENDER_TARGET_VIEW_DESC rtv_desc{
        .Format = format_,
        .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
        .Texture2D = {0, 0}
      };
      GetMainDevice()->CreateRenderTargetView(back_buffer, &rtv_desc, render_targets_[i].rtv.cpu);
    }
    catch (const std::exception& e) {
      LOG_F(ERROR, "Failed to create render target view: {}", e.what());
      ObjectRelease(back_buffer);
    }
  }

  DXGI_SWAP_CHAIN_DESC1 swap_chain_desc{};
  ThrowOnFailed(swap_chain_->GetDesc1(&swap_chain_desc));
  const auto [width, height] = window_.lock()->GetFrameBufferSize();
  DCHECK_EQ_F(width, swap_chain_desc.Width);
  DCHECK_EQ_F(height, swap_chain_desc.Height);

  // Set viewport
  viewport_.TopLeftX = 0.0f;
  viewport_.TopLeftY = 0.0f;
  viewport_.Width = static_cast<float>(width);
  viewport_.Height = static_cast<float>(height);
  viewport_.MinDepth = 0.0f;
  viewport_.MaxDepth = 1.0f;

  // Set scissor rectangle
  scissor_.left = 0;
  scissor_.top = 0;
  scissor_.right = static_cast<LONG>(width);
  scissor_.bottom = static_cast<LONG>(height);
}

void WindowSurfaceImpl::DoRelease()
{
  for (auto& [resource, rtv] : render_targets_) {
    ObjectRelease(resource);
    GetRenderer().RtvHeap().Free(rtv);
  }
  ObjectRelease(swap_chain_);
}

uint32_t WindowSurfaceImpl::Width() const
{
  return static_cast<uint32_t>(viewport_.Width);
}

uint32_t WindowSurfaceImpl::Height() const
{
  return static_cast<uint32_t>(viewport_.Height);
}

ID3D12Resource* WindowSurfaceImpl::BackBuffer() const
{
  return render_targets_[current_backbuffer_index_].resource;
}

D3D12_CPU_DESCRIPTOR_HANDLE WindowSurfaceImpl::Rtv() const
{
  return render_targets_[current_backbuffer_index_].rtv.cpu;
}

D3D12_VIEWPORT WindowSurfaceImpl::Viewport() const
{
  return viewport_;
}

D3D12_RECT WindowSurfaceImpl::Scissor() const
{
  return scissor_;
}
