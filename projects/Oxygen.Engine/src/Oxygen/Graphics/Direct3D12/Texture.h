//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <d3d12.h>

#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

namespace oxygen::graphics {
class DescriptorHandle;

namespace d3d12 {
  class Graphics;

  /*!
   When constructing Texture objects, we take the backend `Graphics` as a
   pointer because there is no way the texture can outlive the graphics
   context. We keep things simple, and we clearly state that the texture is
   owned by the graphics context and not the opposite.
  */
  class Texture : public graphics::Texture {
    using Base = graphics::Texture;

  public:
    OXGN_D3D12_API explicit Texture(TextureDesc desc, const Graphics* gfx);

    OXGN_D3D12_API Texture(
      TextureDesc desc, const NativeResource& native, const Graphics* gfx);

    OXGN_D3D12_API ~Texture() override;

    OXYGEN_MAKE_NON_COPYABLE(Texture)

    Texture(Texture&& other) noexcept;
    OXGN_D3D12_API auto operator=(Texture&& other) noexcept -> Texture&;

    OXGN_D3D12_API auto SetName(std::string_view name) noexcept
      -> void override;

    OXGN_D3D12_NDAPI auto GetNativeResource() const -> NativeResource override;

    OXGN_D3D12_NDAPI auto GetDescriptor() const -> const TextureDesc& override
    {
      return desc_;
    }

  protected:
    // Abstract method implementations from base class
    OXGN_D3D12_NDAPI auto CreateShaderResourceView(
      const DescriptorHandle& view_handle, Format format,
      TextureType texture_type, TextureSubResourceSet sub_resources) const
      -> NativeView override;

    OXGN_D3D12_NDAPI auto CreateUnorderedAccessView(
      const DescriptorHandle& view_handle, Format format,
      TextureType texture_type, TextureSubResourceSet sub_resources) const
      -> NativeView override;

    OXGN_D3D12_NDAPI auto CreateRenderTargetView(
      const DescriptorHandle& view_handle, Format format,
      TextureSubResourceSet sub_resources) const -> NativeView override;

    OXGN_D3D12_NDAPI auto CreateDepthStencilView(
      const DescriptorHandle& view_handle, Format format,
      TextureSubResourceSet sub_resources, bool is_read_only) const
      -> NativeView override;

  private:
    // Abstract method implementations from base class
    OXGN_D3D12_API auto CreateShaderResourceView(
      D3D12_CPU_DESCRIPTOR_HANDLE& dh_cpu, Format format,
      TextureType texture_type, TextureSubResourceSet sub_resources) const
      -> void;

    OXGN_D3D12_API auto CreateUnorderedAccessView(
      D3D12_CPU_DESCRIPTOR_HANDLE& dh_cpu, Format format,
      TextureType texture_type, TextureSubResourceSet sub_resources) const
      -> void;

    OXGN_D3D12_API auto CreateRenderTargetView(
      D3D12_CPU_DESCRIPTOR_HANDLE& dh_cpu, Format format,
      TextureSubResourceSet sub_resources) const -> void;

    OXGN_D3D12_API auto CreateDepthStencilView(
      D3D12_CPU_DESCRIPTOR_HANDLE& dh_cpu, Format format,
      TextureSubResourceSet sub_resources, bool is_read_only) const -> void;

    auto CurrentDevice() const -> dx::IDevice*;

    const Graphics* gfx_ { nullptr };
    TextureDesc desc_;
    D3D12_RESOURCE_DESC resource_desc_;
    uint8_t plane_count_ = 1;
  };

} // namespace d3d12

} // namespace oxygen::graphics
