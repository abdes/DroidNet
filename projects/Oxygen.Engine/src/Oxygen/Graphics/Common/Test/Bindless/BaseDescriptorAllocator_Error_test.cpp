//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

/**
 * @file BaseDescriptorAllocator_Error_test.cpp
 *
 * Unit tests for the BaseDescriptorAllocator class covering error handling
 * and edge cases.
 */

#include <memory>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/Detail/BaseDescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

#include "./BaseDescriptorAllocatorTest.h"
#include "./Mocks/MockDescriptorAllocator.h"
#include "./Mocks/MockDescriptorHeapSegment.h"

using oxygen::graphics::DefaultDescriptorAllocationStrategy;
using oxygen::graphics::DescriptorAllocationStrategy;
using oxygen::graphics::DescriptorHandle;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::HeapDescription;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::bindless::testing::BaseDescriptorAllocatorTest;
using oxygen::graphics::bindless::testing::MockDescriptorAllocator;
using oxygen::graphics::bindless::testing::MockDescriptorHeapSegment;
using oxygen::graphics::detail::BaseDescriptorAllocatorConfig;
using oxygen::graphics::detail::DescriptorHeapSegment;

// -------------------- Test Fixture --------------------
class BaseDescriptorAllocatorErrorTest : public BaseDescriptorAllocatorTest {
};

// -------------------- Error Handling Tests --------------------

NOLINT_TEST_F(BaseDescriptorAllocatorErrorTest, ThrowsIfOutOfSpaceAndNoGrowth)
{
    // Tests that the allocator throws when out of space and growth is disabled

    // Setup: Create a segment that succeeds once then fails, with growth disabled

    // Explicitly disable heap growth
    DisableGrowth();

    bool one_segment = false;
    allocator->segment_factory_ = [this, &one_segment](auto, auto) -> std::unique_ptr<MockDescriptorHeapSegment> {
        if (one_segment) {
            ADD_FAILURE() << "Unexpected segment requested";
            return nullptr;
        }
        one_segment = true;
        auto testSegment = std::make_unique<MockDescriptorHeapSegment>();
        EXPECT_CALL(*testSegment, Allocate())
            .WillOnce(::testing::Return(0))
            .WillRepeatedly(::testing::Return(DescriptorHandle::kInvalidIndex));
        EXPECT_CALL(*testSegment, GetAvailableCount()).WillRepeatedly(::testing::Return(0));
        EXPECT_CALL(*testSegment, Release(0)).WillRepeatedly(::testing::Return(true));
        EXPECT_CALL(*testSegment, GetViewType()).WillRepeatedly(::testing::Return(ResourceViewType::kTexture_SRV));
        EXPECT_CALL(*testSegment, GetVisibility()).WillRepeatedly(::testing::Return(DescriptorVisibility::kShaderVisible));
        EXPECT_CALL(*testSegment, GetBaseIndex()).WillRepeatedly(::testing::Return(0));
        EXPECT_CALL(*testSegment, GetCapacity()).WillRepeatedly(::testing::Return(1));
        EXPECT_CALL(*testSegment, GetAllocatedCount())
            .WillOnce(::testing::Return(0))
            .WillRepeatedly(::testing::Return(1));
        return std::move(testSegment);
    };

    // Action & Verify: First allocation succeeds, second throws
    auto h1 = allocator->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    EXPECT_TRUE(h1.IsValid());
    EXPECT_THROW(allocator->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible), std::runtime_error);
}

NOLINT_TEST_F(BaseDescriptorAllocatorErrorTest, ReleaseFromDifferentAllocatorThrows)
{
    // Tests that attempting to release a handle from a different allocator throws

    // Setup: Create two allocators and a handle from the first
    auto allocator1 = std::make_unique<MockDescriptorAllocator>(default_config);
    auto allocator2 = std::make_unique<MockDescriptorAllocator>(default_config);

    allocator1->segment_factory_ = [](auto, auto) {
        // Create test segment for first allocator
        auto segment1 = std::make_unique<::testing::NiceMock<MockDescriptorHeapSegment>>();
        EXPECT_CALL(*segment1, Allocate()).WillOnce(::testing::Return(0));
        // Add proper release expectation for cleanup
        EXPECT_CALL(*segment1, Release(0)).WillOnce(::testing::Return(true));
        EXPECT_CALL(*segment1, GetViewType()).WillRepeatedly(::testing::Return(ResourceViewType::kTexture_SRV));
        EXPECT_CALL(*segment1, GetVisibility()).WillRepeatedly(::testing::Return(DescriptorVisibility::kShaderVisible));
        EXPECT_CALL(*segment1, GetBaseIndex()).WillRepeatedly(::testing::Return(0));
        EXPECT_CALL(*segment1, GetCapacity()).WillRepeatedly(::testing::Return(1));
        EXPECT_CALL(*segment1, GetAllocatedCount()).WillOnce(::testing::Return(0)).WillRepeatedly(::testing::Return(1));
        return std::move(segment1);
    };

    // Allocate a handle from the first allocator
    auto handle = allocator1->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);

    // Action & Verify: Attempting to release from allocator2 should throw
    EXPECT_THROW(allocator2->Release(handle), std::runtime_error);

    // Clean up properly with the original allocator
    allocator1->Release(handle);
    EXPECT_FALSE(handle.IsValid());
}
