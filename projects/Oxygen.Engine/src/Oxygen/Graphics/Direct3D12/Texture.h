//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <d3d12.h>

#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Direct3D12/Allocator/D3D12MemAlloc.h>
#include <Oxygen/Graphics/Direct3D12/Resources/DescriptorHeap.h>
#include <Oxygen/Graphics/Direct3D12/Resources/GraphicResource.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

namespace oxygen::graphics {

namespace detail {
    class PerFrameResourceManager;
} // namespace detail

namespace d3d12 {

    class Texture : public graphics::Texture {
        using Base = graphics::Texture;

    public:
        OXYGEN_D3D12_API Texture(
            TextureDesc desc,
            D3D12_RESOURCE_DESC resource_desc,
            GraphicResource::ManagedPtr<ID3D12Resource> resource = nullptr,
            GraphicResource::ManagedPtr<D3D12MA::Allocation> allocation = nullptr);

        ~Texture() override;

        OXYGEN_MAKE_NON_COPYABLE(Texture)

        Texture(Texture&& other) noexcept;
        auto operator=(Texture&& other) noexcept -> Texture&;

        void SetName(std::string_view name) noexcept override;

        [[nodiscard]] auto GetNativeResource() const -> NativeObject override;
        [[nodiscard]] auto GetDescriptor() const -> const TextureDesc& override { return desc_; }

        // Abstract method implementations from base class
        [[nodiscard]] auto CreateShaderResourceView(
            Format format,
            TextureDimension dimension,
            TextureSubResourceSet sub_resources) const -> NativeObject override;

        [[nodiscard]] auto CreateUnorderedAccessView(
            Format format,
            TextureDimension dimension,
            TextureSubResourceSet sub_resources) const -> NativeObject override;

        [[nodiscard]] auto CreateRenderTargetView(
            Format format,
            TextureSubResourceSet sub_resources) const -> NativeObject override;

        [[nodiscard]] auto CreateDepthStencilView(
            Format format,
            TextureSubResourceSet sub_resources,
            bool is_read_only) const -> NativeObject override;

        // Internal helper methods that create views with provided descriptor handles
        void CreateShaderResourceView(
            detail::DescriptorHandle& dh,
            Format format,
            TextureDimension dimension,
            TextureSubResourceSet sub_resources) const;

        void CreateUnorderedAccessView(
            detail::DescriptorHandle& dh,
            Format format,
            TextureDimension dimension,
            TextureSubResourceSet sub_resources) const;

        void CreateRenderTargetView(
            detail::DescriptorHandle& dh,
            Format format,
            TextureSubResourceSet sub_resources) const;

        void CreateDepthStencilView(
            detail::DescriptorHandle& dh,
            TextureSubResourceSet sub_resources,
            bool is_read_only = false) const;

        auto GetClearMipLevelUnorderedAccessView(uint32_t mip_level) -> const detail::DescriptorHandle&;

    private:
        TextureDesc desc_;
        D3D12_RESOURCE_DESC resource_desc_;
        uint8_t plane_count_ = 1;

        std::vector<detail::DescriptorHandle> clear_mip_level_uav_cache_;
    };

} // namespace d3d12

} // namespace oxygen::graphics
