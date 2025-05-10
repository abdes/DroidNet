//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <Oxygen/Base/logging.h>
#include <Oxygen/Graphics/Common/ObjectRelease.h>
#include <Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h>
#include <Oxygen/Graphics/Direct3D12/Texture.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>

using oxygen::graphics::d3d12::Texture;

Texture::Texture(
    TextureDesc desc,
    D3D12_RESOURCE_DESC resource_desc,
    ResourcePtr resource,
    D3D12MA::Allocation* allocation,
    const std::string_view name)
    : Base(name)
    , desc_(desc)
    , resource_desc_(resource_desc)
    , resource_(std::move(resource))
    , allocation_(allocation)
{
    DCHECK_NOTNULL_F(resource_);

    NameObject(resource_.get(), name);

    // if (desc.is_uav) {
    //     m_ClearMipLevelUAVs.resize(desc.mip_levels);
    //     std::fill(m_ClearMipLevelUAVs.begin(), m_ClearMipLevelUAVs.end(), c_InvalidDescriptorIndex);
    // }

    plane_count_ = detail::GetGraphics().GetFormatPlaneCount(resource_desc_.Format);
}

Texture::~Texture()
{
    ObjectRelease(allocation_);

    // The resource will be autoimatically released by the unique_ptr custom
    // deleter when it goes out of scope.
}

Texture::Texture(Texture&& other) noexcept
    : Base(std::move(other))
    , desc_(other.desc_)
    , resource_(std::exchange(other.resource_, nullptr))
    , resource_desc_(other.resource_desc_)
    , plane_count_(std::exchange(other.plane_count_, 1))
{
    static_assert(std::is_trivially_copyable<TextureDesc>());
    static_assert(std::is_trivially_copyable<D3D12_RESOURCE_DESC>());
}

auto Texture::operator=(Texture&& other) noexcept -> Texture&
{
    static_assert(std::is_trivially_copyable<TextureDesc>());
    static_assert(std::is_trivially_copyable<D3D12_RESOURCE_DESC>());

    if (this != &other) {
        Base::operator=(std::move(other));
        desc_ = other.desc_;
        resource_ = std::exchange(other.resource_, nullptr);
        resource_desc_ = other.resource_desc_;
        plane_count_ = std::exchange(other.plane_count_, 1);
    }
    return *this;
}

void Texture::SetName(std::string_view name) noexcept
{
    Base::SetName(name);
    NameObject(resource_.get(), name);
}
