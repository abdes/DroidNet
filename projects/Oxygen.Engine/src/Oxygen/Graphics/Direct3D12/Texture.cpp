//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <Oxygen/Base/logging.h>
#include <Oxygen/Graphics/Common/ObjectRelease.h>
#include <Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/Resources/GraphicResource.h>
#include <Oxygen/Graphics/Direct3D12/Texture.h>

using oxygen::graphics::d3d12::GraphicResource;
using oxygen::graphics::d3d12::Texture;

Texture::Texture(
    TextureDesc desc,
    D3D12_RESOURCE_DESC resource_desc,
    GraphicResource::ManagedPtr<ID3D12Resource> resource,
    GraphicResource::ManagedPtr<D3D12MA::Allocation> allocation)
    : Base(desc.debug_name)
    , desc_(desc)
    , resource_desc_(resource_desc)
{
    DCHECK_NOTNULL_F(resource);

    AddComponent<GraphicResource>(desc.debug_name, std::move(resource), std::move(allocation));

    // if (desc.is_uav) {
    //     m_ClearMipLevelUAVs.resize(desc.mip_levels);
    //     std::fill(m_ClearMipLevelUAVs.begin(), m_ClearMipLevelUAVs.end(), c_InvalidDescriptorIndex);
    // }

    plane_count_ = detail::GetGraphics().GetFormatPlaneCount(resource_desc_.Format);
}

Texture::~Texture()
{
}

Texture::Texture(Texture&& other) noexcept
    : Base(std::move(other))
    , desc_(std::move(other.desc_))
    , resource_desc_(other.resource_desc_)
    , plane_count_(std::exchange(other.plane_count_, 1))
{
    static_assert(std::is_trivially_copyable<D3D12_RESOURCE_DESC>());
}

auto Texture::operator=(Texture&& other) noexcept -> Texture&
{
    static_assert(std::is_trivially_copyable<D3D12_RESOURCE_DESC>());

    if (this != &other) {
        Base::operator=(std::move(other));
        desc_ = std::move(other.desc_);
        resource_desc_ = other.resource_desc_;
        plane_count_ = std::exchange(other.plane_count_, 1);
    }
    return *this;
}

auto Texture::GetNativeResource() const -> NativeObject
{
    return NativeObject(GetComponent<GraphicResource>().GetResource(), ClassTypeId());
}

void Texture::SetName(const std::string_view name) noexcept
{
    Base::SetName(name);
    GetComponent<GraphicResource>().SetName(name);
}
