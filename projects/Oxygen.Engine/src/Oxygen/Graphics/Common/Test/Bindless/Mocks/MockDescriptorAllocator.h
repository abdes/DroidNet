//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>

#include <gmock/gmock.h>

#include <Oxygen/Graphics/Common/Detail/BaseDescriptorAllocator.h>
#include <Oxygen/Graphics/Common/NativeObject.h>

namespace oxygen::graphics::bindless::testing {

// ReSharper disable once CppClassCanBeFinal - Mock class cannot be final
// ReSharper disable CppClangTidyModernizeUseTrailingReturnType
class MockDescriptorAllocator : public detail::BaseDescriptorAllocator {
public:
    using SegmentFactory = std::function<std::unique_ptr<detail::DescriptorHeapSegment>(
        ResourceViewType, DescriptorVisibility)>;

    using ExtendedSegmentFactory = std::function<std::unique_ptr<detail::DescriptorHeapSegment>(
        IndexT /*capacity*/, IndexT /*base_index*/, ResourceViewType, DescriptorVisibility)>;

    // Explicit constructor to set up default action for the mocked Allocate method
    explicit MockDescriptorAllocator(const detail::BaseDescriptorAllocatorConfig& config)
        : detail::BaseDescriptorAllocator(config)
    {
        ON_CALL(*this, Allocate(::testing::_, ::testing::_))
            .WillByDefault(::testing::Invoke(
                this, &MockDescriptorAllocator::RealAllocateForMock));
    }

    ExtendedSegmentFactory ext_segment_factory_;
    SegmentFactory segment_factory_;

    // Expose other public methods from BaseDescriptorAllocator for testing (Allocate is now mocked)
    using BaseDescriptorAllocator::Contains;
    using BaseDescriptorAllocator::GetAllocatedDescriptorsCount;
    using BaseDescriptorAllocator::GetRemainingDescriptorsCount;
    using BaseDescriptorAllocator::Release;

    // Google Mock for DescriptorAllocator interface
    MOCK_METHOD(DescriptorHandle, Allocate, (ResourceViewType view_type, DescriptorVisibility visibility), (override));
    MOCK_METHOD(void, CopyDescriptor, (const DescriptorHandle&, const DescriptorHandle&), (override));
    MOCK_METHOD(NativeObject, GetNativeHandle, (const DescriptorHandle&), (const, override));
    MOCK_METHOD(void, PrepareForRendering, (const NativeObject&), (override));

    // Helper method to call the real base implementation for the mock's default action.
    // This method is invoked by the ON_CALL setup.
    DescriptorHandle RealAllocateForMock(ResourceViewType view_type, DescriptorVisibility visibility)
    {
        return detail::BaseDescriptorAllocator::Allocate(view_type, visibility);
    }

    // Expose GetInitialCapacity for testing
    using BaseDescriptorAllocator::GetInitialCapacity;

    // Expose GetAllocationStrategy for testing
    using BaseDescriptorAllocator::GetAllocationStrategy;

protected:
    // Manual override for heap segment creation (not mocked)
    auto CreateHeapSegment(
        IndexT capacity,
        IndexT base_index,
        const ResourceViewType view_type,
        const DescriptorVisibility visibility)
        -> std::unique_ptr<detail::DescriptorHeapSegment> override
    {
        // capacity and base_index are ignored becauuse test cases will mock the
        // segment methods
        return ext_segment_factory_
            ? ext_segment_factory_(capacity, base_index, view_type, visibility)
            : segment_factory_ ? segment_factory_(view_type, visibility)
                               : nullptr;
    }
};

} // namespace oxygen::graphics::bindless::testing
