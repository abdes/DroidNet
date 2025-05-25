//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <format>
#include <ranges>
#include <stdexcept>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Detail/BaseDescriptorAllocator.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/D3D12HeapAllocationStrategy.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/DescriptorAllocator.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/DescriptorHeapSegment.h>
#include <Oxygen/Graphics/Direct3D12/CommandRecorder.h>

namespace oxygen::graphics::d3d12 {

DescriptorAllocator::DescriptorAllocator(
    std::shared_ptr<const DescriptorAllocationStrategy> heap_strategy,
    dx::IDevice* device)
    : BaseDescriptorAllocator(heap_strategy
              ? std::move(heap_strategy)
              : std::make_shared<D3D12HeapAllocationStrategy>())
    , device_(device)
{
    DCHECK_NOTNULL_F(device, "D3D12 device must not be null");
}

DescriptorAllocator::~DescriptorAllocator() = default;

auto DescriptorAllocator::GetCpuHandle(const DescriptorHandle& handle) const
    -> D3D12_CPU_DESCRIPTOR_HANDLE
{
    if (!handle.IsValid()) {
        throw std::runtime_error("Invalid descriptor handle passed to GetCpuHandle");
    }
    // Find the segment for the handle
    const auto* segment = GetD3D12Segment(handle);
    if (!segment) {
        throw std::runtime_error("Failed to find D3D12 segment for handle");
    }
    return segment->GetCpuHandle(handle);
}

auto DescriptorAllocator::GetGpuHandle(const DescriptorHandle& handle) const
    -> D3D12_GPU_DESCRIPTOR_HANDLE
{
    // TODO: check if handle is shader visible and throw if not
    if (!handle.IsValid()) {
        throw std::runtime_error("Invalid descriptor handle passed to GetGpuHandle");
    }
    // Find the segment for the handle
    const auto* segment = GetD3D12Segment(handle);
    if (!segment) {
        throw std::runtime_error("Failed to find D3D12 segment for handle");
    }
    return segment->GetGpuHandle(handle);
}

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

void DescriptorAllocator::UpdateShaderVisibleHeapsSet() const
{
    DLOG_F(1, "updating shader visible heaps set");
    shader_visible_heaps_.clear();

    // Filter for shader-visible heaps and transform directly to ID3D12DescriptorHeap*
    for (const auto& heap_view : Heaps()) {
        for (const auto& segment_ptr : heap_view.segments) {
            // NOLINTNEXTLINE(*-pro-type-static-cast-downcast)
            auto* d3d12_segment = static_cast<const DescriptorHeapSegment*>(segment_ptr.get());
            if (d3d12_segment->IsShaderVisible()) {
                // Debug check: Our allocation strategy ensures only one shader-visible
                // heap per type - check that the heap being added is not already in the set.
                DCHECK_F(
                    std::ranges::find_if(shader_visible_heaps_,
                        [&](const detail::ShaderVisibleHeapInfo& info) {
                            return info.heap == d3d12_segment->GetHeap();
                        })
                        == shader_visible_heaps_.end(),
                    "Multiple shader-visible heaps of the same type detected.");

                shader_visible_heaps_.emplace_back(
                    d3d12_segment->GetHeapType(),
                    d3d12_segment->GetHeap(),
                    d3d12_segment->GetGpuDescriptorTableStart());
            }
        }
    }
}

auto DescriptorAllocator::GetShaderVisibleHeaps()
    -> std::span<const detail::ShaderVisibleHeapInfo>
{
    // Check if we need to update the shader visible heaps set
    if (needs_update_shader_visible_heaps_) {
        UpdateShaderVisibleHeapsSet();
        needs_update_shader_visible_heaps_ = false;
    }

    if (shader_visible_heaps_.empty()) {
        DLOG_F(1, "descriptor allocator -> no shader visible heaps");
        return {};
    }

    // cast the command recorder to a D3D12 command recorder
    DLOG_F(1, "descriptor allocator -> {} shader visible heaps", shader_visible_heaps_.size());

    return shader_visible_heaps_;
}

auto DescriptorAllocator::CreateHeapSegment(
    uint32_t capacity,
    uint32_t base_index,
    ResourceViewType view_type,
    DescriptorVisibility visibility)
    -> std::unique_ptr<graphics::detail::DescriptorHeapSegment>
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

    // Mark the shader visible heaps set for update.
    // NB: DO NOT call UpdateShaderVisibleHeapsSet() here, as it will
    // deadlock due to the heaps mutex being already held when we are
    // creating a new segment.
    needs_update_shader_visible_heaps_ = true;
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
