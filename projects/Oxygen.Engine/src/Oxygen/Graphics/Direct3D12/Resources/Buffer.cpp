//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstring>
#include <memory>
#include <stdexcept>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Windows/ComError.h>
#include <Oxygen/Graphics/Common/Detail/PerFrameResourceManager.h>
#include <Oxygen/Graphics/Common/ObjectRelease.h>
#include <Oxygen/Graphics/Direct3D12/Allocator/D3D12MemAlloc.h>
#include <Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/Resources/Buffer.h>
#include <Oxygen/Graphics/Direct3D12/Resources/GraphicResource.h>

using oxygen::graphics::d3d12::GraphicResource;
using oxygen::graphics::d3d12::detail::GetGraphics;
using oxygen::windows::ThrowOnFailed;

namespace oxygen::graphics::d3d12 {

Buffer::Buffer(
    graphics::detail::PerFrameResourceManager& resource_manager,
    const BufferDesc& desc,
    const void* initial_data)
    : Base(desc.debug_name)
    , size_(desc.size)
    , usage_(desc.usage)
    , memory_(desc.memory)
    , stride_(desc.stride)
    , mapped_(false)
{
    CreateBufferResource(resource_manager, desc, initial_data);
}

void Buffer::CreateBufferResource(
    graphics::detail::PerFrameResourceManager& resource_manager,
    const BufferDesc& desc,
    const void* initial_data)
{
    // Translate BufferDesc to D3D12MA::ALLOCATION_DESC and D3D12_RESOURCE_DESC
    D3D12MA::ALLOCATION_DESC alloc_desc = {};
    switch (desc.memory) {
    case BufferMemory::kDeviceLocal:
        alloc_desc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
        break;
    case BufferMemory::kUpload:
        alloc_desc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
        break;
    case BufferMemory::kReadBack:
        alloc_desc.HeapType = D3D12_HEAP_TYPE_READBACK;
        break;
    }

    D3D12_RESOURCE_DESC resource_desc = {};
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = 0;
    resource_desc.Width = desc.size;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    // Set resource flags based on usage if needed

    D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;
    if (desc.memory == BufferMemory::kUpload)
        initial_state = D3D12_RESOURCE_STATE_GENERIC_READ;
    else if ((desc.usage & BufferUsage::kConstant) == BufferUsage::kConstant)
        initial_state = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    else if ((desc.usage & BufferUsage::kVertex) == BufferUsage::kVertex)
        initial_state = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    else if ((desc.usage & BufferUsage::kIndex) == BufferUsage::kIndex)
        initial_state = D3D12_RESOURCE_STATE_INDEX_BUFFER;
    else if ((desc.usage & BufferUsage::kStorage) == BufferUsage::kStorage)
        initial_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    // Add more as needed

    ID3D12Resource* resource { nullptr };
    D3D12MA::Allocation* allocation { nullptr };

    try {
        ThrowOnFailed(GetGraphics().GetAllocator()->CreateResource(
            &alloc_desc,
            &resource_desc,
            initial_state,
            nullptr, // No optimized clear value for buffers
            &allocation,
            IID_PPV_ARGS(&resource)));

        AddComponent<GraphicResource>(
            Base::GetName(),
            GraphicResource::WrapForDeferredRelease<ID3D12Resource>(resource, resource_manager),
            GraphicResource::WrapForDeferredRelease<D3D12MA::Allocation>(allocation, resource_manager));

        if (initial_data && desc.size > 0) {
            void* dst = nullptr;
            D3D12_RANGE range = { 0, 0 };
            ThrowOnFailed(resource->Map(0, &range, &dst));
            std::memcpy(dst, initial_data, desc.size);
            resource->Unmap(0, nullptr);
        }
    } catch (const std::exception& e) {
        ObjectRelease(resource);
        ObjectRelease(allocation);
        LOG_F(ERROR, "Failed to initialize {}: {}", Base::GetName(), e.what());
        throw;
    }
}

Buffer::~Buffer() = default;

auto Buffer::GetResource() const -> ID3D12Resource*
{
    return GetComponent<GraphicResource>().GetResource();
}

auto Buffer::Map(size_t offset, size_t size) -> void*
{
    void* mapped = nullptr;
    D3D12_RANGE read_range = { offset, offset + size };
    if (size == 0) {
        read_range.Begin = 0;
        read_range.End = 0;
    }
    ThrowOnFailed(GetResource()->Map(0, &read_range, &mapped));
    mapped_ = true;
    return static_cast<uint8_t*>(mapped) + offset;
}

void Buffer::Unmap()
{
    GetResource()->Unmap(0, nullptr);
    mapped_ = false;
}

void Buffer::Update(const void* data, size_t size, size_t offset)
{
    void* dst = Map(offset, size);
    std::memcpy(dst, data, size);
    Unmap();
}

auto Buffer::GetSize() const noexcept -> size_t
{
    return size_;
}

auto Buffer::GetUsage() const noexcept -> BufferUsage
{
    return usage_;
}

auto Buffer::GetMemoryType() const noexcept -> BufferMemory
{
    return memory_;
}

auto Buffer::IsMapped() const noexcept -> bool
{
    return mapped_;
}

auto Buffer::GetDesc() const noexcept -> BufferDesc
{
    BufferDesc desc;
    desc.size = size_;
    desc.usage = usage_;
    desc.memory = memory_;
    desc.stride = stride_;
    return desc;
}

auto Buffer::GetNativeResource() const -> NativeObject
{
    // Use the pointer constructor and the class type id for d3d12::Buffer
    return NativeObject(GetResource(), ClassTypeId());
}

void Buffer::SetName(std::string_view name) noexcept
{
    Base::SetName(name);
    GetComponent<GraphicResource>().SetName(name);
}

} // namespace oxygen::graphics::d3d12
