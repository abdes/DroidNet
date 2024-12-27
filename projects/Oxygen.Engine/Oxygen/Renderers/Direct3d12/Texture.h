//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Renderers/Direct3d12/D3DPtr.h"
#include "Oxygen/Renderers/Direct3d12/Detail/DescriptorHeap.h"
#include "Oxygen/Renderers/Direct3d12/Renderer.h"

namespace oxygen::renderer::d3d12 {

  struct TextureInitInfo
  {
    ID3D12Heap1* heap{ nullptr };
    D3D12_RESOURCE_ALLOCATION_INFO1 alloc_info{ .Offset = 0 };
    ID3D12Resource* resource{ nullptr };
    D3D12_SHADER_RESOURCE_VIEW_DESC* srv_dec{ nullptr };
    D3D12_RESOURCE_DESC* desc{ nullptr };
    D3D12_RESOURCE_STATES initial_state{ D3D12_RESOURCE_STATE_COMMON };
    D3D12_CLEAR_VALUE* clear_value{ nullptr };
  };

  class Texture
  {
  public:
    constexpr static uint32_t max_mips{ 14 }; // 2^14 = 16384

    Texture() = default;
    ~Texture() { Release(); }

    OXYGEN_MAKE_NON_COPYABLE(Texture);
    OXYGEN_MAKE_NON_MOVEABLE(Texture);

    [[nodiscard]] auto GetResource() const -> ID3D12Resource* { return resource_.get(); }
    [[nodiscard]] auto GetSrv() const -> detail::DescriptorHandle { return srv_; }

    void Initialize(const TextureInitInfo& info);

    void Release();

  private:
    D3DDeferredPtr<ID3D12Resource> resource_;
    detail::DescriptorHandle srv_;
  };

  class RenderTexture
  {
  public:
    RenderTexture() = default;
    ~RenderTexture() { Release(); }

    OXYGEN_MAKE_NON_COPYABLE(RenderTexture);
    OXYGEN_MAKE_NON_MOVEABLE(RenderTexture);

    void Initialize(const TextureInitInfo& info);
    void Release();

    [[nodiscard]] auto GetResource() const -> ID3D12Resource* { return texture_.GetResource(); }
    [[nodiscard]] auto GetSrv() const -> detail::DescriptorHandle { return texture_.GetSrv(); }
    [[nodiscard]] auto GetRtv(const uint32_t mip_index) const -> detail::DescriptorHandle
    {
      DCHECK_LT_F(mip_index, mip_count_);
      return rtv_[mip_index];
    }
    [[nodiscard]] constexpr auto GetMipCount() const -> uint32_t { return mip_count_; }

  private:
    Texture texture_{};
    detail::DescriptorHandle rtv_[Texture::max_mips]{};
    uint32_t mip_count_{ 0 };
  };

  class DepthBuffer
  {
  public:
    DepthBuffer() = default;
    ~DepthBuffer() { Release(); }

    OXYGEN_MAKE_NON_COPYABLE(DepthBuffer);
    OXYGEN_MAKE_NON_MOVEABLE(DepthBuffer);

    void Initialize(TextureInitInfo info);
    void Release();

    [[nodiscard]] auto GetResource() const -> ID3D12Resource* { return texture_.GetResource(); }
    [[nodiscard]] auto GetDsv() const -> detail::DescriptorHandle { return dsv_; }
    [[nodiscard]] auto GetSrv() const -> detail::DescriptorHandle { return texture_.GetSrv(); }

  private:
    Texture texture_{};
    detail::DescriptorHandle dsv_{};
  };

}  // namespace oxygen::renderer::d3d12
