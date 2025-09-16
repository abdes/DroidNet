//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>
#include <utility>

#include <gmock/gmock.h>

#include <Oxygen/Graphics/Common/Detail/BaseDescriptorAllocator.h>

namespace oxygen::graphics::bindless::testing {

// ReSharper disable once CppClassCanBeFinal - Mock class cannot be final
// ReSharper disable CppClangTidyModernizeUseTrailingReturnType
class MockDescriptorAllocator : public detail::BaseDescriptorAllocator {
public:
  using SegmentFactory
    = std::function<std::unique_ptr<detail::DescriptorSegment>(
      ResourceViewType, DescriptorVisibility)>;

  using ExtendedSegmentFactory
    = std::function<std::unique_ptr<detail::DescriptorSegment>(
      oxygen::bindless::Capacity /*capacity*/,
      oxygen::bindless::HeapIndex /*base_index*/, ResourceViewType,
      DescriptorVisibility)>;

  // Explicit constructor to set up default action for the mocked Allocate
  // method
  explicit MockDescriptorAllocator(
    std::shared_ptr<const DescriptorAllocationStrategy> heap_strategy = nullptr)
    : BaseDescriptorAllocator(std::move(heap_strategy))
  {
    ON_CALL(*this, Allocate(::testing::_, ::testing::_))
      .WillByDefault(
        ::testing::Invoke(this, &MockDescriptorAllocator::RealAllocateForMock));
  }

  ExtendedSegmentFactory ext_segment_factory_;
  SegmentFactory segment_factory_;

  // Expose other public methods from BaseDescriptorAllocator for testing
  using BaseDescriptorAllocator::Contains;
  using BaseDescriptorAllocator::GetAllocatedDescriptorsCount;
  using BaseDescriptorAllocator::GetRemainingDescriptorsCount;
  using BaseDescriptorAllocator::Release;

  // clang-format off
  // NOLINTBEGIN
  MOCK_METHOD(DescriptorHandle, Allocate, (oxygen::graphics::ResourceViewType view_type, oxygen::graphics::DescriptorVisibility visibility), (override));
  MOCK_METHOD(void, CopyDescriptor, (const DescriptorHandle&, const DescriptorHandle&), (override));
  MOCK_METHOD(oxygen::bindless::ShaderVisibleIndex, GetShaderVisibleIndex, (const DescriptorHandle& handle), (const, noexcept, override));
  // NOLINTEND
  // clang-format off

  DescriptorHandle RealAllocateForMock(
    const ResourceViewType view_type, const DescriptorVisibility visibility)
  {
    return BaseDescriptorAllocator::Allocate(view_type, visibility);
  }

  // Expose GetInitialCapacity for testing
  using BaseDescriptorAllocator::GetInitialCapacity;

  // Expose GetAllocationStrategy for testing
  using BaseDescriptorAllocator::GetAllocationStrategy;

protected:
  // Manual override for heap segment creation (not mocked)
  auto CreateHeapSegment(const oxygen::bindless::Capacity capacity,
    const oxygen::bindless::HeapIndex base_index, const ResourceViewType view_type,
    const DescriptorVisibility visibility)
    -> std::unique_ptr<detail::DescriptorSegment> override
  {
    // capacity and base_index are ignored because test cases will mock the
    // segment methods
    return ext_segment_factory_
      ? ext_segment_factory_(capacity, base_index, view_type, visibility)
      : segment_factory_ ? segment_factory_(view_type, visibility)
                         : nullptr;
  }
};

} // namespace oxygen::graphics::bindless::testing
