//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <format>
#include <stdexcept>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Detail/BaseDescriptorAllocator.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/D3D12HeapAllocationStrategy.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/DescriptorAllocator.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/DescriptorHeapSegment.h>

namespace oxygen::graphics::d3d12 {

DescriptorAllocator::DescriptorAllocator(
    std::shared_ptr<const DescriptorAllocationStrategy> heap_strategy,
    dx::IDevice* device)
    : BaseDescriptorAllocator(std::move(heap_strategy))
    , device_(device)
{
    DCHECK_NOTNULL_F(device, "D3D12 device must not be null");
}

DescriptorAllocator::~DescriptorAllocator() = default;

void DescriptorAllocator::CopyDescriptor(const DescriptorHandle& dst, const DescriptorHandle& src)
{
    if (!dst.IsValid() || !src.IsValid()) {
        throw std::runtime_error("Cannot copy from/to invalid descriptor handles");
    }

    const auto* dst_segment = GetD3D12Segment(dst);
    const auto* src_segment = GetD3D12Segment(src);

    if (!dst_segment || !src_segment) {
        throw std::runtime_error("Cannot find D3D12 segment for handle");
    }

    // Get D3D12 handles
    auto dst_cpu = dst_segment->GetCpuHandle(dst);
    auto src_cpu = src_segment->GetCpuHandle(src);

    // Check if the descriptors are in the same heap type
    if (dst_segment->GetHeapType() == src_segment->GetHeapType()) {
        // Simple case: same heap type, can use CopyDescriptorsSimple
        // Use the device from the segment
        device_->CopyDescriptorsSimple(
            1,
            dst_cpu,
            src_cpu,
            dst_segment->GetHeapType());
    } else {
        // Different heap types - this shouldn't happen with the current design
        throw std::runtime_error(std::format(
            "Cannot copy descriptors between different heap types: {} to {}",
            static_cast<int>(src_segment->GetHeapType()),
            static_cast<int>(dst_segment->GetHeapType())));
    }
}

// TODO: this will most likely be significantly refactored in the future
void DescriptorAllocator::PrepareForRendering(const NativeObject& command_list_obj)
{
    if (!command_list_obj.IsValid()) {
        LOG_F(WARNING, "Invalid command list object passed to PrepareForRendering");
        return;
    }

    ID3D12GraphicsCommandList* command_list;
    try {
        command_list = command_list_obj.AsPointer<ID3D12GraphicsCommandList>();
    } catch (const std::exception& e) {
        LOG_F(ERROR, "Failed to get ID3D12GraphicsCommandList pointer from NativeObject: {}", e.what());
        return;
    }

    if (!command_list) {
        LOG_F(ERROR, "Failed to get ID3D12GraphicsCommandList pointer from NativeObject");
        return;
    }

    // Collect all unique shader-visible heaps from segments using Heaps()
    std::vector<ID3D12DescriptorHeap*> heaps;
    for (const auto& [_, segments] : Heaps()) {
        for (const auto& segment_ptr : segments) {
            auto* d3d12_segment = static_cast<const DescriptorHeapSegment*>(segment_ptr.get()); // NOLINT(*-static-cast-downcast)
            if (d3d12_segment->IsShaderVisible()) {
                ID3D12DescriptorHeap* heap = d3d12_segment->GetHeap();
                // Assert no duplicates in debug builds
                DCHECK_F(std::ranges::find(heaps, heap) == heaps.end(),
                    "Duplicate ID3D12DescriptorHeap detected in PrepareForRendering");
                heaps.push_back(heap);
            }
        }
    }

    if (!heaps.empty()) {
        command_list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
    }
}

auto DescriptorAllocator::CreateHeapSegment(
    uint32_t capacity,
    uint32_t base_index,
    ResourceViewType view_type,
    DescriptorVisibility visibility)
    -> std::unique_ptr<detail::DescriptorHeapSegment>
{
    // For D3D12, each segment maps directly to a single D3D12 descriptor heap
    // Create a unique name for debugging
    const auto heap_name = std::format("DescHeap_{}_{}_{}",
        static_cast<int>(D3D12HeapAllocationStrategy::GetHeapType(view_type)),
        static_cast<int>(D3D12HeapAllocationStrategy::GetHeapFlags(visibility)),
        base_index);

    auto segment = std::make_unique<DescriptorHeapSegment>(
        device_,
        capacity,
        base_index,
        view_type,
        visibility,
        heap_name.c_str());

    return segment;
}

auto DescriptorAllocator::GetD3D12Segment(const DescriptorHandle& handle) const
    -> const DescriptorHeapSegment*
{
    if (!Contains(handle)) {
        return nullptr;
    }

    const auto segment_opt = GetSegmentForHandle(handle);
    DCHECK_F(segment_opt.has_value(), "expecting to find a segment if Contains(handle) returned true");
    if (!segment_opt.has_value()) {
        // Should never reach here if Contains returned true
        return nullptr;
    }

    // Cast to our D3D12-specific segment type
    return static_cast<const DescriptorHeapSegment*>(*segment_opt); // NOLINT(*-static-cast-downcast)
}

} // namespace oxygen::graphics::d3d12
