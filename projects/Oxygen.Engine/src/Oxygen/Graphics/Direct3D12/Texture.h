//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>
#include <vector>

#include <d3d12.h>

#include <Oxygen/Graphics/Common/Detail/PerFrameResourceManager.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Direct3D12/Resources/DescriptorHeap.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

namespace oxygen::graphics {

namespace detail {
    class PerFrameResourceManager;
} // namespace detail

namespace d3d12 {

    class Texture : public graphics::Texture {
        using Base = graphics::Texture;

    public:
        OXYGEN_D3D12_API explicit Texture(TextureDesc desc);
        OXYGEN_D3D12_API Texture(TextureDesc desc, NativeObject native);

        OXYGEN_D3D12_API Texture(TextureDesc desc, graphics::detail::PerFrameResourceManager& resource_manager);
        OXYGEN_D3D12_API Texture(TextureDesc desc, NativeObject native, graphics::detail::PerFrameResourceManager& resource_manager);

        OXYGEN_D3D12_API ~Texture() override;

        OXYGEN_MAKE_NON_COPYABLE(Texture)

        Texture(Texture&& other) noexcept;
        OXYGEN_D3D12_API auto operator=(Texture&& other) noexcept -> Texture&;

        OXYGEN_D3D12_API void SetName(std::string_view name) noexcept override;

        [[nodiscard]] OXYGEN_D3D12_API auto GetNativeResource() const -> NativeObject override;
        [[nodiscard]] OXYGEN_D3D12_API auto GetDescriptor() const -> const TextureDesc& override { return desc_; }

        // Abstract method implementations from base class
        [[nodiscard]] OXYGEN_D3D12_API auto CreateShaderResourceView(
            Format format,
            TextureDimension dimension,
            TextureSubResourceSet sub_resources) const -> NativeObject override;

        [[nodiscard]] OXYGEN_D3D12_API auto CreateUnorderedAccessView(
            Format format,
            TextureDimension dimension,
            TextureSubResourceSet sub_resources) const -> NativeObject override;

        [[nodiscard]] OXYGEN_D3D12_API auto CreateRenderTargetView(
            Format format,
            TextureSubResourceSet sub_resources) const -> NativeObject override;

        [[nodiscard]] OXYGEN_D3D12_API auto CreateDepthStencilView(
            Format format,
            TextureSubResourceSet sub_resources,
            bool is_read_only) const -> NativeObject override;

        // Internal helper methods that create views with provided descriptor handles
        // TODO: use descriptor allocator and registry
        OXYGEN_D3D12_API void CreateShaderResourceView(
            detail::DescriptorHandle& dh,
            Format format,
            TextureDimension dimension,
            TextureSubResourceSet sub_resources) const;

        OXYGEN_D3D12_API void CreateUnorderedAccessView(
            detail::DescriptorHandle& dh,
            Format format,
            TextureDimension dimension,
            TextureSubResourceSet sub_resources) const;

        OXYGEN_D3D12_API void CreateRenderTargetView(
            detail::DescriptorHandle& dh,
            Format format,
            TextureSubResourceSet sub_resources) const;

        OXYGEN_D3D12_API void CreateDepthStencilView(
            detail::DescriptorHandle& dh,
            Format format,
            TextureSubResourceSet sub_resources,
            bool is_read_only = false) const;

        OXYGEN_D3D12_API auto GetClearMipLevelUnorderedAccessView(uint32_t mip_level)
            -> const detail::DescriptorHandle&;

    private:
        TextureDesc desc_;
        D3D12_RESOURCE_DESC resource_desc_;
        uint8_t plane_count_ = 1;

        std::vector<detail::DescriptorHandle> clear_mip_level_uav_cache_;
    };

} // namespace d3d12

} // namespace oxygen::graphics
