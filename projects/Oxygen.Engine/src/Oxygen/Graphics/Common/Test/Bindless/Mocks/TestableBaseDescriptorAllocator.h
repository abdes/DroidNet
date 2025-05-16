//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

/**
 * @file TestableBaseDescriptorAllocator.h
 *
 * Extended base descriptor allocator for testing protected methods.
 */

#pragma once

#include <functional>

#include <Oxygen/Graphics/Common/Detail/BaseDescriptorAllocator.h>
#include <Oxygen/Graphics/Common/NativeObject.h>

namespace oxygen::graphics::testing {

// For testing direct access to protected methods (including GetInitialCapacity)
class TestableBaseDescriptorAllocator : public detail::BaseDescriptorAllocator {
public:
    using BaseDescriptorAllocator::BaseDescriptorAllocator;
    using BaseDescriptorAllocator::GetInitialCapacity;

    std::function<std::unique_ptr<detail::DescriptorHeapSegment>(ResourceViewType, DescriptorVisibility)> segmentFactory;

    // Mock implementations of pure virtual methods
    void CopyDescriptor(const DescriptorHandle&, const DescriptorHandle&) override { }
    NativeObject GetNativeHandle(const DescriptorHandle&) const override { return NativeObject { nullptr, 0 }; }
    void PrepareForRendering(const NativeObject&) override { }

protected:
    std::unique_ptr<detail::DescriptorHeapSegment> CreateHeapSegment(ResourceViewType type, DescriptorVisibility vis) override
    {
        return segmentFactory ? segmentFactory(type, vis) : nullptr;
    }
};

} // namespace oxygen::graphics::testing
