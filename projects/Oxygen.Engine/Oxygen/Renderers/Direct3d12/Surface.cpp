//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Renderers/Direct3d12/Surface.h"

#include <shared_mutex>

#include "detail/dx12_utils.h"
#include "oxygen/base/logging.h"
#include "oxygen/base/resource_table.h"
#include "oxygen/base/win_errors.h"
#include "oxygen/platform/window.h"
#include "Oxygen/Renderers/Direct3d12/Renderer.h"

using oxygen::renderer::resources::SurfaceId;
using oxygen::platform::WindowPtr;
using oxygen::renderer::Surface;
using oxygen::renderer::d3d12::WindowSurface;

namespace oxygen::renderer::d3d12::detail {

  class WindowSurfaceImpl
  {
  public:

    explicit WindowSurfaceImpl(WindowPtr window)
      : window_(std::move(window))
    {
    }

    void SetSize(int width, int height);
    void Present() const;
    void CreateSwapChain(
      FactoryType* factory,
      CommandQueueType* command_queue,
      DXGI_FORMAT format = kDefaultBackBufferFormat);

    [[nodiscard]] uint32_t Width() const;
    [[nodiscard]] uint32_t Height() const;
    [[nodiscard]] ID3D12Resource* BackBuffer() const;
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE Rtv() const;
    [[nodiscard]] D3D12_VIEWPORT Viewport() const;
    [[nodiscard]] D3D12_RECT Scissor() const;

  private:
    friend class WindowSurface;
    void DoRelease();
    void Finalize();

    std::weak_ptr<platform::Window> window_;
    IDXGISwapChain4* swap_chain_{ nullptr };
    mutable UINT current_backbuffer_index_{ 0 };
    D3D12_VIEWPORT viewport_{};
    D3D12_RECT scissor_{};
    DXGI_FORMAT format_{ kDefaultBackBufferFormat };  // The format of the swap chain

    struct RenderTargetData
    {
      ID3D12Resource* resource{ nullptr };
      DescriptorHandle rtv{};
    };
    RenderTargetData render_targets_[kFrameBufferCount]{};
  };

}  // namespace oxygen::renderer::d3d12::detail

using oxygen::renderer::d3d12::detail::WindowSurfaceImpl;

namespace {
  using oxygen::renderer::resources::kSurface;
  oxygen::ResourceTable<WindowSurfaceImpl> surfaces(kSurface, 256);

  std::shared_mutex entity_mutex;

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
  CheckResult(swap_chain_->Present(0, 0));
  current_backbuffer_index_ = swap_chain_->GetCurrentBackBufferIndex();
}

void WindowSurfaceImpl::CreateSwapChain(
  FactoryType* factory,
  CommandQueueType* command_queue,
  const DXGI_FORMAT format)
{
  DCHECK_NOTNULL_F(factory);
  DCHECK_NOTNULL_F(command_queue);

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
    CheckResult(
      factory->CreateSwapChainForHwnd(
        command_queue,
        hwnd,
        &swap_chain_desc,
        nullptr,
        nullptr,
        &swap_chain));
    CheckResult(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));
    CheckResult(swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain_)));
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
      CheckResult(swap_chain_->GetBuffer(i, IID_PPV_ARGS(&back_buffer)));
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
  CheckResult(swap_chain_->GetDesc1(&swap_chain_desc));
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


WindowSurface::WindowSurface(const SurfaceId& surface_id, WindowSurfaceImpl* impl)
  : Surface(surface_id), impl_(impl)
{
}

WindowSurface::WindowSurface() = default;

WindowSurface::~WindowSurface() = default;

#define CALL_IMPL(statement, ...) \
  if (!surfaces.Contains(GetId())) { \
    LOG_F(WARNING, "WindowSurface resource with Id `{}` does not exist anymore", GetId().ToString()); \
    return; \
  } \
  impl_->statement

#define RETURN_IMPL(statement, error_value) \
  if (!surfaces.Contains(GetId())) { \
    LOG_F(WARNING, "WindowSurface resource with Id `{}` does not exist anymore", GetId().ToString()); \
    return error_value; \
  } \
  return impl_->statement

void WindowSurface::SetSize(const int width, const int height)
{
  CALL_IMPL(SetSize(width, height));
}

void WindowSurface::Present() const
{
  CALL_IMPL(Present());
}

uint32_t WindowSurface::Width() const
{
  RETURN_IMPL(Width(), 0);
}

uint32_t WindowSurface::Height() const
{
  RETURN_IMPL(Height(), 0);
}

ID3D12Resource* WindowSurface::BackBuffer() const
{
  RETURN_IMPL(BackBuffer(), nullptr);
}

D3D12_CPU_DESCRIPTOR_HANDLE WindowSurface::Rtv() const
{
  RETURN_IMPL(Rtv(), {});
}

D3D12_VIEWPORT WindowSurface::Viewport() const
{
  RETURN_IMPL(Viewport(), {});
}

D3D12_RECT WindowSurface::Scissor() const
{
  RETURN_IMPL(Scissor(), {});
}

void WindowSurface::CreateSwapChain(FactoryType* factory, CommandQueueType* command_queue, const DXGI_FORMAT format)
{
  CALL_IMPL(CreateSwapChain(factory, command_queue, format));
}

void WindowSurface::Finalize()
{
  CALL_IMPL(Finalize());
}

void WindowSurface::DoRelease()
{
  impl_->DoRelease();
  LOG_F(INFO, "Window Surface released: {}", GetId().ToString());
}

#undef CALL_IMPL
#undef RETURN_IMPL

auto oxygen::renderer::d3d12::CreateWindowSurface(WindowPtr window) -> WindowSurface
{
  DCHECK_NOTNULL_F(window.lock());
  DCHECK_F(window.lock()->IsValid());

  const auto surface_id = surfaces.Emplace(std::move(window));
  if (!surface_id.IsValid()) {
    return {};
  }
  LOG_F(INFO, "Window Surface created: {}", surface_id.ToString());

  auto& impl = surfaces.ItemAt(surface_id);
  return WindowSurface(surface_id, &impl);
}

auto oxygen::renderer::d3d12::DestroyWindowSurface(SurfaceId& surface_id) -> size_t
{
  DCHECK_F(GetSurface(surface_id).IsValid());

  std::unique_lock lock(entity_mutex);
  const auto surface_removed = surfaces.Erase(surface_id);
  if (surface_removed != 0) {
    surface_id.Invalidate();
    LOG_F(INFO, "Window Surface removed: {}", surface_id.ToString());
  }
  return surface_removed;
}

auto oxygen::renderer::d3d12::GetSurface(const SurfaceId& surface_id) -> WindowSurface
{
  try {
    auto& impl = surfaces.ItemAt(surface_id);
    return WindowSurface(surface_id, &impl);
  }
  catch (std::exception e)
  {
    LOG_F(ERROR, "Failed to get Window Surface: {}", e.what());
    return {};
  }
}
