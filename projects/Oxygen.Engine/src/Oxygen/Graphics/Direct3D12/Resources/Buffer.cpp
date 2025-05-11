//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/DeferredObjectRelease.h>
#include <Oxygen/Graphics/Common/Detail/PerFrameResourceManager.h>
#include <Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/Resources/Buffer.h>
#include <Oxygen/Graphics/Direct3D12/Resources/GraphicResource.h>

using oxygen::graphics::DeferredObjectRelease;
using oxygen::graphics::d3d12::Buffer;
using oxygen::graphics::d3d12::detail::GetGraphics;

Buffer::~Buffer() = default;

Buffer::Buffer(oxygen::graphics::detail::PerFrameResourceManager& resource_manager, const BufferInitInfo& init_info)
    : Base(init_info.debug_name)
{
    ID3D12Resource* resource { nullptr };
    D3D12MA::Allocation* allocation { nullptr };

    try {
        // Create the buffer resource using D3D12MemAlloc
        const HRESULT hr = GetGraphics().GetAllocator()->CreateResource(
            &init_info.alloc_desc,
            &init_info.resource_desc,
            init_info.initial_state,
            nullptr,
            &allocation,
            IID_PPV_ARGS(&resource));

        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create buffer resource");
        }

        AddComponent<GraphicResource>(
            init_info.debug_name,
            GraphicResource::WrapForDeferredRelease<ID3D12Resource>(resource, resource_manager),
            GraphicResource::WrapForDeferredRelease<D3D12MA::Allocation>(allocation, resource_manager));

        size_ = init_info.size_in_bytes;
    } catch (const std::exception& e) {
        ObjectRelease(resource);
        ObjectRelease(allocation);
        LOG_F(ERROR, "Failed to initialize {}: {}", Base::GetName(), e.what());
        throw;
    }
}

auto Buffer::GetResource() const -> ID3D12Resource*
{
    return GetComponent<GraphicResource>().GetResource();
}

auto Buffer::Map() -> void*
{
    void* mappedData = nullptr;
    const D3D12_RANGE readRange = { 0, 0 };
    if (const HRESULT hr = GetResource()->Map(0, &readRange, &mappedData); FAILED(hr)) {
        throw std::runtime_error("Failed to map buffer");
    }
    return mappedData;
}

void Buffer::Unmap()
{
    GetResource()->Unmap(0, nullptr);
}

void Buffer::SetName(std::string_view name) noexcept
{
    Base::SetName(name);
    GetComponent<GraphicResource>().SetName(name);
}
