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
#include <Oxygen/Graphics/Direct3D12/Bindless/DescriptorSegment.h>
#include <Oxygen/Graphics/Direct3D12/CommandRecorder.h>

using oxygen::bindless::ShaderVisibleIndex;

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
  (void)ReserveRaw(ResourceViewType::kStructuredBuffer_SRV,
    DescriptorVisibility::kShaderVisible, bindless::Count { 1U });
  (void)ReserveRaw(ResourceViewType::kSampler,
    DescriptorVisibility::kShaderVisible, bindless::Count { 1U });
}

DescriptorAllocator::~DescriptorAllocator() = default;

auto DescriptorAllocator::GetCpuHandle(
  const DescriptorAllocationHandle& handle) const -> D3D12_CPU_DESCRIPTOR_HANDLE
{
  if (!handle.IsValid()) {
    throw std::runtime_error(
      "Invalid descriptor handle passed to GetCpuHandle");
  }
  if (handle.IsBindless()
    || handle.GetVisibility() == DescriptorVisibility::kShaderVisible) {
    const auto shader_index = GetShaderVisibleIndex(handle);
    const auto heap_type
      = handle.GetDomain() == bindless::generated::kSamplersDomain
        || handle.GetViewType() == ResourceViewType::kSampler
      ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
      : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    const auto* segment = GetSharedShaderVisibleSegment(heap_type);
    if (segment == nullptr) {
      throw std::runtime_error(
        "Failed to locate shared shader-visible heap for bindless handle");
    }
    auto cpu = segment->GetCpuDescriptorTableStart();
    cpu.ptr += static_cast<SIZE_T>(shader_index.get())
      * device_->GetDescriptorHandleIncrementSize(heap_type);
    return cpu;
  }
  // Find the segment for the handle
  const auto* segment = GetD3D12Segment(handle);
  if (!segment) {
    throw std::runtime_error("Failed to find D3D12 segment for handle");
  }
  return segment->GetCpuHandle(handle);
}

auto DescriptorAllocator::GetGpuHandle(
  const DescriptorAllocationHandle& handle) const -> D3D12_GPU_DESCRIPTOR_HANDLE
{
  // TODO: check if handle is shader visible and throw if not
  if (!handle.IsValid()) {
    throw std::runtime_error(
      "Invalid descriptor handle passed to GetGpuHandle");
  }
  if (handle.IsBindless()
    || handle.GetVisibility() == DescriptorVisibility::kShaderVisible) {
    const auto shader_index = GetShaderVisibleIndex(handle);
    const auto heap_type
      = handle.GetDomain() == bindless::generated::kSamplersDomain
        || handle.GetViewType() == ResourceViewType::kSampler
      ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
      : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    const auto* segment = GetSharedShaderVisibleSegment(heap_type);
    if (segment == nullptr) {
      throw std::runtime_error(
        "Failed to locate shared shader-visible heap for bindless handle");
    }
    auto gpu = segment->GetGpuDescriptorTableStart();
    gpu.ptr += static_cast<UINT64>(shader_index.get())
      * device_->GetDescriptorHandleIncrementSize(heap_type);
    return gpu;
  }
  // Find the segment for the handle
  const auto* segment = GetD3D12Segment(handle);
  if (!segment) {
    throw std::runtime_error("Failed to find D3D12 segment for handle");
  }
  return segment->GetGpuHandle(handle);
}

auto DescriptorAllocator::CopyDescriptor(const DescriptorAllocationHandle& dst,
  const DescriptorAllocationHandle& src) -> void
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
      1, dst_cpu, src_cpu, dst_segment->GetHeapType());
  } else {
    // Different heap types - this shouldn't happen with the current design
    throw std::runtime_error(std::format(
      "Cannot copy descriptors between different heap types: {} to {}",
      static_cast<int>(src_segment->GetHeapType()),
      static_cast<int>(dst_segment->GetHeapType())));
  }
}

auto DescriptorAllocator::UpdateShaderVisibleHeapsSet() const -> void
{
  DLOG_F(1, "updating shader visible heaps set");
  shader_visible_heaps_.clear();

  // Filter for shader-visible heaps and transform directly to
  // ID3D12DescriptorHeap*
  for (const auto& heap_view : Heaps()) {
    for (const auto& segment_ptr : heap_view.segments) {
      // NOLINTNEXTLINE(*-pro-type-static-cast-downcast)
      auto* d3d12_segment
        = static_cast<const DescriptorSegment*>(segment_ptr.get());
      if (d3d12_segment->IsShaderVisible()) {
        // Debug check: Our allocation strategy ensures only one shader-visible
        // heap per type - check that the heap being added is not already in the
        // set.
        DCHECK_F(std::ranges::find_if(shader_visible_heaps_,
                   [&](const detail::ShaderVisibleHeapInfo& info) {
                     return info.heap == d3d12_segment->GetHeap();
                   })
            == shader_visible_heaps_.end(),
          "Multiple shader-visible heaps of the same type detected.");

        shader_visible_heaps_.emplace_back(d3d12_segment->GetHeapType(),
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

  return shader_visible_heaps_;
}

auto DescriptorAllocator::CreateHeapSegment(bindless::Capacity capacity,
  bindless::HeapIndex base_index, ResourceViewType view_type,
  DescriptorVisibility visibility)
  -> std::unique_ptr<graphics::detail::DescriptorSegment>
{
  // For D3D12, each segment maps directly to a single D3D12 descriptor heap
  // Create a unique name for debugging
  const auto heap_name = std::format("DescHeap_{}_{}_{}",
    static_cast<int>(D3D12HeapAllocationStrategy::GetHeapType(view_type)),
    static_cast<int>(D3D12HeapAllocationStrategy::GetHeapFlags(visibility)),
    base_index.get());

  LOG_SCOPE_F(1, "New D3D12 DescriptorSegment");
  auto segment = std::make_unique<DescriptorSegment>(
    device_, capacity, base_index, view_type, visibility, heap_name.c_str());

  // Mark the shader visible heaps set for update.
  // NB: DO NOT call UpdateShaderVisibleHeapsSet() here, as it will
  // deadlock due to the heaps mutex being already held when we are
  // creating a new segment.
  needs_update_shader_visible_heaps_ = true;
  return segment;
}

auto DescriptorAllocator::GetD3D12Segment(
  const DescriptorAllocationHandle& handle) const -> const DescriptorSegment*
{
  if (!Contains(handle)) {
    return nullptr;
  }

  const auto segment_opt = GetSegmentForHandle(handle);
  DCHECK_F(segment_opt.has_value(),
    "expecting to find a segment if Contains(handle) returned true");
  if (!segment_opt.has_value()) {
    // Should never reach here if Contains returned true
    return nullptr;
  }

  // Cast to our D3D12-specific segment type
  return static_cast<const DescriptorSegment*>(
    *segment_opt); // NOLINT(*-static-cast-downcast)
}

auto DescriptorAllocator::GetSharedShaderVisibleSegment(
  const D3D12_DESCRIPTOR_HEAP_TYPE type) const -> const DescriptorSegment*
{
  for (const auto& heap_view : Heaps()) {
    for (const auto& segment_ptr : heap_view.segments) {
      auto* d3d12_segment
        = static_cast<const DescriptorSegment*>(segment_ptr.get());
      if (d3d12_segment->IsShaderVisible()
        && d3d12_segment->GetHeapType() == type) {
        return d3d12_segment;
      }
    }
  }
  return nullptr;
}

auto DescriptorAllocator::GetShaderVisibleIndex(
  const DescriptorAllocationHandle& handle) const noexcept -> ShaderVisibleIndex
{
  if (handle.IsBindless()) {
    const auto* const domain_desc
      = bindless::generated::TryGetDomainDesc(handle.GetDomain());
    if (domain_desc == nullptr) {
      return kInvalidShaderVisibleIndex;
    }
    return ShaderVisibleIndex {
      domain_desc->shader_index_base + handle.GetLocalSlot(),
    };
  }

  return GetRawShaderVisibleIndex(handle);
}

} // namespace oxygen::graphics::d3d12
