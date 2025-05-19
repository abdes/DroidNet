//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/D3D12HeapAllocationStrategy.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/DescriptorHeapSegment.h>
#include <format> // Include for std::format

namespace oxygen::graphics::d3d12 {

DescriptorHeapSegment::DescriptorHeapSegment(
    dx::IDevice* device,
    IndexT capacity,
    const IndexT base_index,
    const ResourceViewType view_type,
    const DescriptorVisibility visibility,
    const char* debug_name)
    : FixedDescriptorHeapSegment(capacity, base_index, view_type, visibility)
    , device_(device)
    , heap_type_(D3D12HeapAllocationStrategy::GetHeapType(view_type))
{
    DCHECK_NOTNULL_F(device_, "Device must not be null");

    // Following RAII principles, create the D3D12 descriptor heap here
    const D3D12_DESCRIPTOR_HEAP_TYPE type = heap_type_;
    const D3D12_DESCRIPTOR_HEAP_FLAGS flags = D3D12HeapAllocationStrategy::GetHeapFlags(visibility);

    // Create heap description
    D3D12_DESCRIPTOR_HEAP_DESC desc {};
    desc.Type = type;
    desc.NumDescriptors = capacity;
    desc.Flags = flags;
    desc.NodeMask = 0;

    // Create the heap
    const HRESULT hr = device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap_));
    if (FAILED(hr)) {
        throw std::runtime_error(std::format(
            "Failed to create descriptor heap: type={}, flags={}, capacity={}, hr=0x{:x}",
            static_cast<int>(type), static_cast<int>(flags), capacity, static_cast<unsigned int>(hr)));
    }

    // Set debug name if provided
    if (debug_name) {
        heap_->SetName(std::wstring(debug_name, debug_name + strlen(debug_name)).c_str());
    }

    // Get handles and increment size
    cpu_start_ = heap_->GetCPUDescriptorHandleForHeapStart();
    handle_increment_size_ = device_->GetDescriptorHandleIncrementSize(type);

    // Get GPU handle if shader visible
    if (flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) {
        gpu_start_ = heap_->GetGPUDescriptorHandleForHeapStart();
    } else {
        gpu_start_ = D3D12_GPU_DESCRIPTOR_HANDLE { 0 };
    }

    // Log heap segment creation
    DLOG_F(INFO, "Created D3D12 descriptor heap segment: type={}, flags={}, capacity={}, base_index={}{}",
        static_cast<int>(desc.Type),
        static_cast<int>(desc.Flags),
        capacity,
        base_index,
        debug_name ? std::format(" ({})", debug_name) : "");
}

DescriptorHeapSegment::~DescriptorHeapSegment() = default;

auto DescriptorHeapSegment::GetCpuHandle(const IndexT global_index) const -> D3D12_CPU_DESCRIPTOR_HANDLE
{
    const auto local_index = GlobalToLocalIndex(global_index);
    D3D12_CPU_DESCRIPTOR_HANDLE handle = cpu_start_;
    handle.ptr += static_cast<SIZE_T>(local_index * handle_increment_size_);
    return handle;
}

auto DescriptorHeapSegment::GetGpuHandle(const IndexT global_index) const -> D3D12_GPU_DESCRIPTOR_HANDLE
{
    if (!IsShaderVisible()) {
        // Return a null GPU handle for CPU-only heaps
        return D3D12_GPU_DESCRIPTOR_HANDLE { 0 };
    }

    const auto local_index = GlobalToLocalIndex(global_index);
    D3D12_GPU_DESCRIPTOR_HANDLE handle = gpu_start_;
    handle.ptr += static_cast<UINT64>(local_index * handle_increment_size_);
    return handle;
}

auto DescriptorHeapSegment::IsShaderVisible() const noexcept -> bool
{
    const D3D12_DESCRIPTOR_HEAP_DESC& desc = heap_->GetDesc();
    return (desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0;
}

auto DescriptorHeapSegment::GetHeap() const -> ID3D12DescriptorHeap*
{
    return heap_.Get();
}

auto DescriptorHeapSegment::GetD3D12HeapType() const -> D3D12_DESCRIPTOR_HEAP_TYPE
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
