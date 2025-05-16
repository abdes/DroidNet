//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

/**
 * @file MockDescriptorAllocator.h
 *
 * Test implementation of a descriptor allocator for testing purposes.
 */

#pragma once

#include <functional>

#include <gmock/gmock.h>

#include <Oxygen/Graphics/Common/Detail/BaseDescriptorAllocator.h>

namespace oxygen::graphics::testing {

// Testable concrete allocator using Google Mock
class MockDescriptorAllocator : public detail::BaseDescriptorAllocator {
public:
    using SegmentFactory = std::function<std::unique_ptr<detail::DescriptorHeapSegment>(ResourceViewType, DescriptorVisibility)>;

    using BaseDescriptorAllocator::BaseDescriptorAllocator;
    SegmentFactory segmentFactory;

    // Google Mock for DescriptorAllocator interface
    MOCK_METHOD(void, CopyDescriptor, (const DescriptorHandle&, const DescriptorHandle&), (override));
    MOCK_METHOD(NativeObject, GetNativeHandle, (const DescriptorHandle&), (const, override));
    MOCK_METHOD(void, PrepareForRendering, (const NativeObject&), (override));

protected:
    // Manual override for heap segment creation (not mocked)
    std::unique_ptr<detail::DescriptorHeapSegment> CreateHeapSegment(ResourceViewType type, DescriptorVisibility vis) override
    {
        return segmentFactory ? segmentFactory(type, vis) : nullptr;
    }
};

} // namespace oxygen::graphics::testing
