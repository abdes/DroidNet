//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>
#include <string_view>

#include <d3d12.h>

#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Direct3D12/Allocator/D3D12MemAlloc.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

namespace oxygen::graphics::d3d12 {

class Texture : public graphics::Texture {
    using Base = graphics::Texture;

public:
    using ResourceDeleter = std::function<void(ID3D12Resource*)>;
    using ResourcePtr = std::unique_ptr<ID3D12Resource, ResourceDeleter>;

    OXYGEN_D3D12_API explicit Texture(
        TextureDesc desc,
        D3D12_RESOURCE_DESC resource_desc,
        ResourcePtr resource,
        D3D12MA::Allocation* allocation,
        std::string_view name = "Texture");

    ~Texture() override;

    OXYGEN_MAKE_NON_COPYABLE(Texture);
    Texture(Texture&& other) noexcept;
    auto operator=(Texture&& other) noexcept -> Texture&;

    [[nodiscard]] auto GetNativeResource() const -> NativeObject override
    {
        return NativeObject(resource_.get(), ClassTypeId());
    }

    [[nodiscard]] virtual auto GetDescriptor() const -> const TextureDesc&
    {
        return desc_;
    }

    void SetName(std::string_view name) noexcept override;

private:
    TextureDesc desc_;
    ResourcePtr resource_;
    D3D12MA::Allocation* allocation_ { nullptr };
    D3D12_RESOURCE_DESC resource_desc_;
    uint8_t plane_count_ = 1;

    // std::vector<DescriptorIndex> m_ClearMipLevelUAVs;
};

} // namespace oxygen::graphics::d3d12
