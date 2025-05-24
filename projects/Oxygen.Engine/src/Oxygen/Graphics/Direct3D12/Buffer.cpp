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
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/Detail/PerFrameResourceManager.h>
#include <Oxygen/Graphics/Common/ObjectRelease.h>
#include <Oxygen/Graphics/Direct3D12/Allocator/D3D12MemAlloc.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/DescriptorAllocator.h>
#include <Oxygen/Graphics/Direct3D12/Buffer.h>
#include <Oxygen/Graphics/Direct3D12/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h>
#include <Oxygen/Graphics/Direct3D12/GraphicResource.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>

using oxygen::graphics::d3d12::DescriptorAllocator;
using oxygen::graphics::d3d12::GraphicResource;
using oxygen::graphics::d3d12::detail::GetDxgiFormatMapping;
using oxygen::graphics::d3d12::detail::GetGraphics;
using oxygen::graphics::detail::GetFormatInfo;
using oxygen::windows::ThrowOnFailed;

namespace oxygen::graphics::d3d12 {

Buffer::Buffer(const BufferDesc& desc)
    : Base(desc.debug_name)
    , desc_(desc)
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

    D3D12_RESOURCE_DESC resource_desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Alignment = 0,
        .Width = desc.size_bytes,
        .Height = 1,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .Format = DXGI_FORMAT_UNKNOWN,
        .SampleDesc = { .Count = 1, .Quality = 0 },
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags = D3D12_RESOURCE_FLAG_NONE
    };

    // Set appropriate resource flags based on buffer usage
    resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    if ((desc.usage & BufferUsage::kStorage) == BufferUsage::kStorage) {
        resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;
    if (desc.memory == BufferMemory::kUpload) {
        initial_state = D3D12_RESOURCE_STATE_GENERIC_READ;
    } else if (desc.memory == BufferMemory::kReadBack) {
        initial_state = D3D12_RESOURCE_STATE_COPY_DEST;
    } else if ((desc.usage & BufferUsage::kConstant) == BufferUsage::kConstant
        || (desc.usage & BufferUsage::kVertex) == BufferUsage::kVertex) {
        initial_state = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    } else if ((desc.usage & BufferUsage::kIndex) == BufferUsage::kIndex) {
        initial_state = D3D12_RESOURCE_STATE_INDEX_BUFFER;
    } else if ((desc.usage & BufferUsage::kStorage) == BufferUsage::kStorage) {
        initial_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    } else if ((desc.usage & BufferUsage::kIndirect) == BufferUsage::kIndirect) {
        initial_state = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    }

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

        // Use GraphicResource's built-in helper for deferred resource release
        AddComponent<GraphicResource>(
            Base::GetName(),
            resource,
            allocation);
    } catch (const std::exception& e) {
        ObjectRelease(resource);
        ObjectRelease(allocation);
        LOG_F(ERROR, "Failed to initialize {}: {}", Base::GetName(), e.what());
        throw;
    }
}

Buffer::~Buffer()
{
    DLOG_F(3, "destroying buffer: {}", Base::GetName());
    // Unmap the buffer if it's mapped before releasing
    if (mapped_) {
        // Cannot call UnMap() here as it is virtual, and will be resolved at
        // compile time inside the destructor.
        LOG_F(ERROR, "Buffer {} is still mapped: {}", Base::GetName());
    }

    // No need to manually register resources for deferred release. The
    // GraphicResource component's destructor will handle this through its
    // ManagedPtr deleter.
}

auto Buffer::GetResource() const -> ID3D12Resource*
{
    return GetComponent<GraphicResource>().GetResource();
}

auto Buffer::GetGPUVirtualAddress() const -> uint64_t
{
    auto* resource = GetResource();
    if (!resource) {
        return 0;
    }
    return resource->GetGPUVirtualAddress();
}

auto Buffer::Map(const size_t offset, const size_t size) -> void*
{
    // Validate buffer can be mapped
    if (desc_.memory == BufferMemory::kDeviceLocal) {
        LOG_F(ERROR, "Cannot map device-local buffer {} directly", Base::GetName());
        throw std::runtime_error("Cannot map device-local buffer directly");
    }

    void* mapped = nullptr;
    // For read operations on read-back buffers, we specify the read range
    // For upload buffers, we specify an empty range as we're only writing
    D3D12_RANGE read_range = { 0, 0 };
    if (desc_.memory == BufferMemory::kReadBack && size > 0) {
        read_range = { .Begin = offset, .End = offset + size };
    }

    ThrowOnFailed(GetResource()->Map(0, &read_range, &mapped));
    mapped_ = true;
    return static_cast<uint8_t*>(mapped) + offset;
}

void Buffer::UnMap()
{
    if (!mapped_) {
        return;
    }

    // For upload buffers, we specify the written range
    // For read-back buffers, we use nullptr as we don't write
    D3D12_RANGE written_range = { 0, 0 };
    if (desc_.memory == BufferMemory::kUpload) {
        // We could track the exact written range for more efficiency
        written_range = { .Begin = 0, .End = desc_.size_bytes };
    }

    GetResource()->Unmap(0, &written_range);
    mapped_ = false;
}

void Buffer::Update(const void* data, const size_t size, const size_t offset)
{
    if (size == 0) {
        return;
    }

    // For device local buffers, we should use a staging buffer and command list
    if (desc_.memory == BufferMemory::kDeviceLocal) {
        // This implementation assumes there's a command list available
        // A more robust implementation would track updates and apply them at submission time
        LOG_F(ERROR, "Direct update of device-local buffer {} not implemented, use staging buffers", Base::GetName());
        throw std::runtime_error("Direct update of device-local buffer not implemented");
    }

    void* dst = Map(offset, size);
    std::memcpy(dst, data, size);
    UnMap();
}

auto Buffer::GetSize() const noexcept -> size_t
{
    return desc_.size_bytes;
}

auto Buffer::GetUsage() const noexcept -> BufferUsage
{
    return desc_.usage;
}

auto Buffer::GetMemoryType() const noexcept -> BufferMemory
{
    return desc_.memory;
}

auto Buffer::IsMapped() const noexcept -> bool
{
    return mapped_;
}

auto Buffer::GetDescriptor() const noexcept -> BufferDesc
{
    return desc_;
}

auto Buffer::GetNativeResource() const -> NativeObject
{
    // Use the pointer constructor and the class type id for d3d12::Buffer
    return { GetResource(), ClassTypeId() };
}

void Buffer::SetName(const std::string_view name) noexcept
{
    Base::SetName(name);
    GetComponent<GraphicResource>().SetName(name);
}

auto Buffer::GetNativeView(
    const DescriptorHandle& view_handle,
    const BufferViewDescription& view_desc) const
    -> NativeObject
{
    using oxygen::graphics::ResourceViewType;

    switch (view_desc.view_type) {
    case ResourceViewType::kConstantBuffer:
        return CreateConstantBufferView(
            view_handle,
            view_desc.range);
    case ResourceViewType::kRawBuffer_SRV:
    case ResourceViewType::kTypedBuffer_SRV:
    case ResourceViewType::kStructuredBuffer_SRV:
        return CreateShaderResourceView(
            view_handle,
            view_desc.format,
            view_desc.range,
            view_desc.stride);
    case ResourceViewType::kRawBuffer_UAV:
    case ResourceViewType::kTypedBuffer_UAV:
    case ResourceViewType::kStructuredBuffer_UAV:
        return CreateUnorderedAccessView(
            view_handle,
            view_desc.format,
            view_desc.range,
            view_desc.stride);
    default:
        // Unknown or unsupported view type
        return {};
    }
}

auto Buffer::CreateConstantBufferView(
    const DescriptorHandle& view_handle,
    const BufferRange& range) const -> NativeObject
{
    if (!view_handle.IsValid()) {
        throw std::runtime_error("Invalid view handle");
    }
    if ((desc_.usage & BufferUsage::kConstant) != BufferUsage::kConstant) {
        LOG_F(WARNING, "Creating constant buffer view for buffer {} without kConstant usage flag", Base::GetName());
    }

    const auto* allocator = static_cast<DescriptorAllocator*>(view_handle.GetAllocator());
    auto cpu_handle = allocator->GetCpuHandle(view_handle);

    // Prepare CBV description
    const BufferRange resolved = range.Resolve(desc_);
    const D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {
        .BufferLocation = GetResource()->GetGPUVirtualAddress() + resolved.offset_bytes,
        // CB size must be a multiple of 256 bytes (D3D12 requirement)
        .SizeInBytes = static_cast<UINT>((resolved.size_bytes == ~0ull ? desc_.size_bytes : resolved.size_bytes) + 255) & ~255u,
    };

    GetGraphics().GetCurrentDevice()->CreateConstantBufferView(&cbv_desc, cpu_handle);

    auto gpu_handle = allocator->GetGpuHandle(view_handle);
    return { gpu_handle.ptr, ClassTypeId() };
}

auto Buffer::CreateShaderResourceView(
    const DescriptorHandle& view_handle,
    Format format, BufferRange range, uint32_t stride) const -> NativeObject
{
    if (!view_handle.IsValid()) {
        throw std::runtime_error("Invalid view handle");
    }

    // Validate parameters
    if (stride > 0 && format != Format::kUnknown) {
        LOG_F(WARNING, "Buffer {}: Both format and stride specified for SRV; format will be ignored", Base::GetName());
    }

    const auto* allocator = static_cast<DescriptorAllocator*>(view_handle.GetAllocator());
    auto cpu_handle = allocator->GetCpuHandle(view_handle);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    BufferRange resolved = range.Resolve(desc_);

    BufferViewDescription view_desc;
    view_desc.format = format;
    view_desc.range = range;
    view_desc.stride = stride;

    if (view_desc.IsStructuredBuffer()) {
        // Structured buffer view
        srv_desc.Format = DXGI_FORMAT_UNKNOWN;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Buffer.FirstElement = resolved.offset_bytes / stride;
        srv_desc.Buffer.NumElements = static_cast<UINT>((resolved.size_bytes == ~0ull ? desc_.size_bytes : resolved.size_bytes) / stride);
        srv_desc.Buffer.StructureByteStride = stride;
        srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    } else if (view_desc.IsTypedBuffer()) {
        // Typed buffer view
        srv_desc.Format = GetDxgiFormatMapping(format).srv_format;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        UINT element_size = GetFormatInfo(format).bytes_per_block;
        srv_desc.Buffer.FirstElement = static_cast<UINT64>(resolved.offset_bytes / element_size);
        srv_desc.Buffer.NumElements = static_cast<UINT>((resolved.size_bytes == ~0ull ? desc_.size_bytes : resolved.size_bytes) / element_size);
        srv_desc.Buffer.StructureByteStride = 0;
        srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    } else {
        // Raw buffer view
        srv_desc.Format = DXGI_FORMAT_R32_TYPELESS;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Buffer.FirstElement = static_cast<UINT64>(resolved.offset_bytes / 4);
        srv_desc.Buffer.NumElements = static_cast<UINT>((resolved.size_bytes == ~0ull ? desc_.size_bytes : resolved.size_bytes) / 4);
        srv_desc.Buffer.StructureByteStride = 0;
        srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    }

    try {
        GetGraphics().GetCurrentDevice()->CreateShaderResourceView(GetResource(), &srv_desc, cpu_handle);

        auto gpu_handle = allocator->GetGpuHandle(view_handle);
        return { gpu_handle.ptr, ClassTypeId() };
    } catch (const std::exception& e) {
        LOG_F(ERROR, "Failed to create SRV for buffer {}: {}", Base::GetName(), e.what());
        throw;
    }
}

auto Buffer::CreateUnorderedAccessView(
    const DescriptorHandle& view_handle,
    Format format, BufferRange range, uint32_t stride) const -> NativeObject
{
    if (!view_handle.IsValid()) {
        throw std::runtime_error("Invalid view handle");
    }

    // Validate buffer has UAV usage flag
    if ((desc_.usage & BufferUsage::kStorage) != BufferUsage::kStorage) {
        LOG_F(WARNING, "Creating UAV for buffer {} without kStorage usage flag", Base::GetName());
    }

    // Validate parameters
    if (stride > 0 && format != Format::kUnknown) {
        LOG_F(WARNING, "Buffer {}: Both format and stride specified for UAV; format will be ignored", Base::GetName());
    }

    const auto* allocator = static_cast<DescriptorAllocator*>(view_handle.GetAllocator());
    auto cpu_handle = allocator->GetCpuHandle(view_handle);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
    BufferRange resolved = range.Resolve(desc_);

    BufferViewDescription view_desc;
    view_desc.format = format;
    view_desc.range = range;
    view_desc.stride = stride;

    if (view_desc.IsStructuredBuffer()) {
        // Structured buffer view
        uav_desc.Format = DXGI_FORMAT_UNKNOWN;
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav_desc.Buffer.FirstElement = static_cast<UINT64>(resolved.offset_bytes / stride);
        uav_desc.Buffer.NumElements = static_cast<UINT>((resolved.size_bytes == ~0ull ? desc_.size_bytes : resolved.size_bytes) / stride);
        uav_desc.Buffer.StructureByteStride = stride;
        uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        uav_desc.Buffer.CounterOffsetInBytes = 0;
    } else if (view_desc.IsTypedBuffer()) {
        // Typed buffer view
        uav_desc.Format = GetDxgiFormatMapping(format).srv_format;
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        UINT element_size = GetFormatInfo(format).bytes_per_block;
        uav_desc.Buffer.FirstElement = static_cast<UINT64>(resolved.offset_bytes / element_size);
        uav_desc.Buffer.NumElements = static_cast<UINT>((resolved.size_bytes == ~0ull ? desc_.size_bytes : resolved.size_bytes) / element_size);
        uav_desc.Buffer.StructureByteStride = 0;
        uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        uav_desc.Buffer.CounterOffsetInBytes = 0;
    } else {
        // Raw buffer view
        uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav_desc.Buffer.FirstElement = static_cast<UINT64>(resolved.offset_bytes / 4);
        uav_desc.Buffer.NumElements = static_cast<UINT>((resolved.size_bytes == ~0ull ? desc_.size_bytes : resolved.size_bytes) / 4);
        uav_desc.Buffer.StructureByteStride = 0;
        uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        uav_desc.Buffer.CounterOffsetInBytes = 0;
    }

    try {
        GetGraphics().GetCurrentDevice()->CreateUnorderedAccessView(GetResource(), nullptr, &uav_desc, cpu_handle);
        auto gpu_handle = allocator->GetGpuHandle(view_handle);
        return { gpu_handle.ptr, ClassTypeId() };
    } catch (const std::exception& e) {
        LOG_F(ERROR, "Failed to create UAV for buffer {}: {}", Base::GetName(), e.what());
        throw;
    }
}

} // namespace oxygen::graphics::d3d12
