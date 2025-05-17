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

// ReSharper disable CppClangTidyModernizeUseTrailingReturnType

namespace oxygen::graphics::bindless::testing {

// ReSharper disable once CppClassCanBeFinal - Mock class cannot be final
class MockDescriptorAllocator : public detail::BaseDescriptorAllocator {
public:
    using SegmentFactory = std::function<std::unique_ptr<detail::DescriptorHeapSegment>(
        ResourceViewType, DescriptorVisibility)>;

    using BaseDescriptorAllocator::BaseDescriptorAllocator;
    SegmentFactory segment_factory_;

    // Expose all public methods from BaseDescriptorAllocator for testing
    using BaseDescriptorAllocator::Allocate;
    using BaseDescriptorAllocator::Contains;
    using BaseDescriptorAllocator::GetAllocatedDescriptorsCount;
    using BaseDescriptorAllocator::GetRemainingDescriptorsCount;
    using BaseDescriptorAllocator::Release;

    // Google Mock for DescriptorAllocator interface
    MOCK_METHOD(void, CopyDescriptor, (const DescriptorHandle&, const DescriptorHandle&), (override));
    MOCK_METHOD(NativeObject, GetNativeHandle, (const DescriptorHandle&), (const, override));
    MOCK_METHOD(void, PrepareForRendering, (const NativeObject&), (override));

    // Expose GetInitialCapacity for testing
    using BaseDescriptorAllocator::GetInitialCapacity;

protected:
    // Manual override for heap segment creation (not mocked)
    auto CreateHeapSegment(
        uint32_t /*capacity*/,
        uint32_t /*base_index*/,
        const ResourceViewType view_type,
        const DescriptorVisibility visibility)
        -> std::unique_ptr<detail::DescriptorHeapSegment> override
    {
        // capacity and base_index are ignored becauuse test cases will mock the
        // segment methods
        return segment_factory_ ? segment_factory_(view_type, visibility) : nullptr;
    }
};

} // namespace oxygen::graphics::bindless::testing
