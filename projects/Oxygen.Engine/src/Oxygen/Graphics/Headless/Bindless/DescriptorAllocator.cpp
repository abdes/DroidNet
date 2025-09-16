//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Headless/Bindless/AllocationStrategy.h>
#include <Oxygen/Graphics/Headless/Bindless/DescriptorAllocator.h>
#include <Oxygen/Graphics/Headless/Bindless/DescriptorSegment.h>

using oxygen::bindless::ShaderVisibleIndex;

namespace oxygen::graphics::headless::bindless {

DescriptorAllocator::DescriptorAllocator(
  std::shared_ptr<const DescriptorAllocationStrategy> strategy)
  : BaseDescriptorAllocator(
      strategy ? std::move(strategy) : std::make_shared<AllocationStrategy>())
{
}

auto DescriptorAllocator::CreateHeapSegment(oxygen::bindless::Capacity capacity,
  oxygen::bindless::HeapIndex base_index, ResourceViewType view_type,
  DescriptorVisibility visibility) -> std::unique_ptr<detail::DescriptorSegment>
{
  return std::make_unique<DescriptorSegment>(
    capacity, base_index, view_type, visibility);
}

auto DescriptorAllocator::GetShaderVisibleIndex(
  const DescriptorHandle& handle) const noexcept -> ShaderVisibleIndex
{
  const auto segment = GetSegmentForHandle(handle);
  if (!segment.has_value()) {
    return kInvalidShaderVisibleIndex;
  }
  DCHECK_NOTNULL_F(segment.value());

  // Because we map bindless tables 1:1 to heap segments, the shader-visible
  // index is the local index into the segment.
  return ShaderVisibleIndex { handle.GetBindlessHandle().get()
    - (*segment)->GetBaseIndex().get() };
}

auto DescriptorAllocator::CopyDescriptor(
  const DescriptorHandle& source, const DescriptorHandle& destination) -> void
{
  // Validation: ensure handles are valid and of the same view type.
  if (!source.IsValid() || !destination.IsValid()) {
    throw std::runtime_error(
      "CopyDescriptor: source or destination is invalid");
  }
  if (source.GetViewType() != destination.GetViewType()) {
    throw std::runtime_error(
      "CopyDescriptor: source and destination view types differ");
  }

  // Headless backend: there is no native descriptor to copy. This is a
  // logical operation (CPU bookkeeping) which the base allocator does not
  // expose, so we simply validate and return.
}

} // namespace oxygen::graphics::headless::bindless
