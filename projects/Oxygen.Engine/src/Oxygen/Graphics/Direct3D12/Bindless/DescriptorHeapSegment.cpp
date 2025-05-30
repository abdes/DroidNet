//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Windows/ComError.h>
#include <Oxygen/Graphics/Common/ObjectRelease.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/D3D12HeapAllocationStrategy.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/DescriptorHeapSegment.h>
#include <Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h>

using oxygen::windows::ThrowOnFailed;

namespace oxygen::graphics::d3d12 {

DescriptorHeapSegment::DescriptorHeapSegment(
    dx::IDevice* device,
    IndexT capacity,
    const IndexT base_index,
    const ResourceViewType view_type,
    const DescriptorVisibility visibility,
    std::string_view debug_name)
    : FixedDescriptorHeapSegment(capacity, base_index, view_type, visibility)
    , device_(device)
    , heap_type_(D3D12HeapAllocationStrategy::GetHeapType(view_type))
{
    DCHECK_NOTNULL_F(device_, "Device must not be null");

    // Following RAII principles, create the D3D12 descriptor heap here
    const D3D12_DESCRIPTOR_HEAP_TYPE type = heap_type_;
    const D3D12_DESCRIPTOR_HEAP_FLAGS flags = D3D12HeapAllocationStrategy::GetHeapFlags(visibility);

    // Create heap description
    const D3D12_DESCRIPTOR_HEAP_DESC desc {
        .Type = type,
        .NumDescriptors = capacity,
        .Flags = flags,
        .NodeMask = 0,
    };

    // Create the heap
    ThrowOnFailed(device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap_)),
        fmt::format(
            "Failed to create descriptor heap: type={}, flags={}, capacity={}",
            static_cast<int>(type), static_cast<int>(flags), capacity));

    // Double-check the heap creation
    DCHECK_NOTNULL_F(heap_, "ThrowOnFailed passed but heap is null");

    // Set debug name if provided.
    if (!debug_name.empty()) {
        NameObject(heap_, debug_name);
    }

    // Get handles and increment size
    cpu_start_ = heap_->GetCPUDescriptorHandleForHeapStart();
    handle_increment_size_ = device_->GetDescriptorHandleIncrementSize(type);

    // Get GPU handle if shader visible
    if (flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) {
        gpu_start_ = heap_->GetGPUDescriptorHandleForHeapStart();
    }

    // Log heap segment creation
    DLOG_F(INFO, "Created D3D12 descriptor heap segment: type={}, flags={}, capacity={}, base_index={}{}",
        static_cast<int>(desc.Type),
        static_cast<int>(desc.Flags),
        capacity,
        base_index,
        !debug_name.empty() ? fmt::format(" ({})", debug_name) : "");
}

DescriptorHeapSegment::~DescriptorHeapSegment()
{
    ObjectRelease(heap_);
}

auto DescriptorHeapSegment::GetCpuHandle(const DescriptorHandle& handle) const
    -> D3D12_CPU_DESCRIPTOR_HANDLE
{
    const auto local_index = GlobalToLocalIndex(handle.GetIndex());
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = cpu_start_;
    cpu_handle.ptr += static_cast<SIZE_T>(local_index * handle_increment_size_);
    return cpu_handle;
}

auto DescriptorHeapSegment::GetGpuHandle(const DescriptorHandle& handle) const
    -> D3D12_GPU_DESCRIPTOR_HANDLE
{
    if (!IsShaderVisible()) {
        throw std::runtime_error("Descriptor heap is not shader visible, cannot get GPU handle.");
    }

    const auto local_index = GlobalToLocalIndex(handle.GetIndex());
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle = gpu_start_;
    gpu_handle.ptr += static_cast<UINT64>(local_index * handle_increment_size_);
    return gpu_handle;
}

auto DescriptorHeapSegment::GetGpuDescriptorTableStart() const
    -> D3D12_GPU_DESCRIPTOR_HANDLE
{
    if (!IsShaderVisible()) {
        throw std::runtime_error("Descriptor heap is not shader visible, cannot get GPU handle.");
    }
    return gpu_start_;
}

auto DescriptorHeapSegment::GetCpuDescriptorTableStart() const
    -> D3D12_CPU_DESCRIPTOR_HANDLE
{
    return cpu_start_;
}

auto DescriptorHeapSegment::IsShaderVisible() const noexcept -> bool
{
    const auto& desc = heap_->GetDesc();
    return (desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0;
}

auto DescriptorHeapSegment::GetHeap() const -> dx::IDescriptorHeap*
{
    return heap_;
}

auto DescriptorHeapSegment::GetHeapType() const -> D3D12_DESCRIPTOR_HEAP_TYPE
{
    return heap_type_;
}

auto DescriptorHeapSegment::GlobalToLocalIndex(const IndexT global_index) const -> IndexT
{
    // Validate the global index is within this segment's range
    DCHECK_GE_F(global_index, GetBaseIndex(), "Global index {} is less than base index {}",
        global_index, GetBaseIndex());
    DCHECK_LT_F(global_index, GetBaseIndex() + GetCapacity(),
        "Global index {} is outside segment capacity (base={}, capacity={})",
        global_index, GetBaseIndex(), GetCapacity());

    return global_index - GetBaseIndex();
}

} // namespace oxygen::graphics::d3d12
