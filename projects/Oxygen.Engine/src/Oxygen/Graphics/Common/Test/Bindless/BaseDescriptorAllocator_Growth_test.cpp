//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

/**
 * @file BaseDescriptorAllocator_Growth_test.cpp
 *
 * Unit tests for the BaseDescriptorAllocator class covering heap growth and
 * management behaviors.
 */

#include <memory>
#include <set>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/Detail/BaseDescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

#include "./BaseDescriptorAllocatorTest.h"
#include "./Mocks/MockDescriptorAllocator.h"
#include "./Mocks/MockDescriptorHeapSegment.h"

using oxygen::graphics::DescriptorHandle;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::bindless::testing::BaseDescriptorAllocatorTest;
using oxygen::graphics::bindless::testing::MockDescriptorAllocator;
using oxygen::graphics::bindless::testing::MockDescriptorHeapSegment;
using oxygen::graphics::bindless::testing::OneCapacityDescriptorAllocationStrategy;
using oxygen::graphics::detail::BaseDescriptorAllocatorConfig;
using oxygen::graphics::detail::DescriptorHeapSegment;

// -------------------- Test Fixture --------------------
class BaseDescriptorAllocatorGrowthTest : public BaseDescriptorAllocatorTest {
};

// -------------------- Growth Policy Tests --------------------

NOLINT_TEST_F(BaseDescriptorAllocatorGrowthTest, GrowthPolicyRespected)
{
    // Tests that the heap grows when full if growth is enabled

    // Track segment creation
    int segmentCount = 0;
    allocator->segment_factory_ = [this, &segmentCount](auto type, auto vis) {
        segmentCount++;
        if (segmentCount == 1) {
            // Setup: Create a segment that will fill up and need to grow
            auto first_segment = std::make_unique<MockDescriptorHeapSegment>();
            EXPECT_CALL(*first_segment, Allocate())
                .WillOnce(::testing::Return(0))
                .WillRepeatedly(::testing::Return(DescriptorHandle::kInvalidIndex)); // Second call indicates full
            EXPECT_CALL(*first_segment, GetViewType()).WillRepeatedly(::testing::Return(type));
            EXPECT_CALL(*first_segment, GetVisibility()).WillRepeatedly(::testing::Return(vis));
            EXPECT_CALL(*first_segment, Release(::testing::_)).Times(1).WillOnce(::testing::Return(true));
            EXPECT_CALL(*first_segment, GetBaseIndex()).WillRepeatedly(::testing::Return(0));
            EXPECT_CALL(*first_segment, GetCapacity()).WillRepeatedly(::testing::Return(1));
            EXPECT_CALL(*first_segment, GetAllocatedCount())
                .WillOnce(::testing::Return(0))
                .WillRepeatedly(::testing::Return(1));
            return first_segment;
        }

        // Second segment for growth
        auto growth_segment = std::make_unique<MockDescriptorHeapSegment>();
        EXPECT_CALL(*growth_segment, Allocate()).WillOnce(::testing::Return(100)); // Different index to distinguish
        EXPECT_CALL(*growth_segment, GetAvailableCount()).WillRepeatedly(::testing::Return(1));
        EXPECT_CALL(*growth_segment, GetViewType()).WillRepeatedly(::testing::Return(type));
        EXPECT_CALL(*growth_segment, GetVisibility()).WillRepeatedly(::testing::Return(vis));
        EXPECT_CALL(*growth_segment, Release(::testing::_)).Times(1).WillOnce(::testing::Return(true));
        EXPECT_CALL(*growth_segment, GetBaseIndex()).WillRepeatedly(::testing::Return(100));
        EXPECT_CALL(*growth_segment, GetCapacity()).WillRepeatedly(::testing::Return(1));
        return growth_segment;
    };

    // Action: Allocate twice, second allocation should cause growth
    auto h1 = allocator->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    auto h2 = allocator->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);

    // Verify: Both handles are valid, second segment was created
    EXPECT_TRUE(h1.IsValid());
    EXPECT_TRUE(h2.IsValid());
    EXPECT_EQ(h1.GetIndex(), 0);
    EXPECT_EQ(h2.GetIndex(), 100); // From second segment
    EXPECT_EQ(segmentCount, 2);

    h1.Release();
    h2.Release();
}

NOLINT_TEST_F(BaseDescriptorAllocatorGrowthTest, GrowthFactorRespected)
{
    // Tests that the growth factor is respected when creating new segments
    // This ensures that segments grow at the expected rate

    // Setup: Track the requested segment sizes
    std::vector<uint32_t> requestedSizes;
    auto key = default_config.heap_strategy->GetHeapKey(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    const auto initialCapacity = default_config.heap_strategy->GetHeapDescription(key).shader_visible_capacity;
    const auto growthFactor = default_config.heap_strategy->GetHeapDescription(key).growth_factor;
    const auto maxGrowthIterations = default_config.heap_strategy->GetHeapDescription(key).max_growth_iterations;

    // Setup factory to track the sizes that would be used for segment creation
    // NOTE: We're tracking what would be requested, not actually creating segments of those sizes
    allocator->segment_factory_ = [&requestedSizes, initialCapacity, growthFactor](auto type, auto vis) {
        uint32_t expectedSize = 0;

        // Calculate the expected size based on the number of segments already requested
        if (requestedSizes.empty()) {
            expectedSize = initialCapacity;
        } else {
            expectedSize = static_cast<uint32_t>(requestedSizes.back() * growthFactor);
        }

        requestedSizes.push_back(expectedSize);

        // Create a segment that will immediately fail allocation to trigger growth
        auto seg = std::make_unique<::testing::NiceMock<MockDescriptorHeapSegment>>();
        EXPECT_CALL(*seg, Allocate()).WillRepeatedly(::testing::Return(UINT32_MAX));
        EXPECT_CALL(*seg, GetAvailableCount()).WillRepeatedly(::testing::Return(0));
        EXPECT_CALL(*seg, GetViewType()).WillRepeatedly(::testing::Return(type));
        EXPECT_CALL(*seg, GetVisibility()).WillRepeatedly(::testing::Return(vis));
        return seg;
    };

    // Action: Try to allocate until max growth iterations
    // Each allocation will fail and trigger segment creation with increasing sizes
    for (uint32_t i = 0; i < maxGrowthIterations; ++i) {
        try {
            allocator->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
        } catch (const std::runtime_error&) {
            // Expected to throw when we run out of growth iterations
            break;
        }
    }

    // Verify: We should have seen requested sizes grow according to the growth factor
    ASSERT_GE(requestedSizes.size(), 2);
    for (size_t i = 1; i < requestedSizes.size(); ++i) {
        const float actualRatio = static_cast<float>(requestedSizes[i]) / requestedSizes[i - 1];
        EXPECT_NEAR(actualRatio, growthFactor, 0.1f);
    }
}

NOLINT_TEST_F(BaseDescriptorAllocatorGrowthTest, SegmentCreatedOnlyWhenNeeded)
{
    // Tests that segments are created only when needed, not preemptively

    int segmentCreateCount = 0;

    allocator->segment_factory_ = [&segmentCreateCount](auto, auto) {
        auto testSegment = std::make_unique<MockDescriptorHeapSegment>();
        EXPECT_CALL(*testSegment, Allocate()).WillOnce(::testing::Return(0));
        EXPECT_CALL(*testSegment, GetAvailableCount()).WillRepeatedly(::testing::Return(1));
        EXPECT_CALL(*testSegment, GetViewType()).WillRepeatedly(::testing::Return(ResourceViewType::kTexture_SRV));
        EXPECT_CALL(*testSegment, GetVisibility()).WillRepeatedly(::testing::Return(DescriptorVisibility::kShaderVisible));
        EXPECT_CALL(*testSegment, GetBaseIndex()).WillRepeatedly(::testing::Return(0));
        EXPECT_CALL(*testSegment, GetCapacity()).WillRepeatedly(::testing::Return(1));
        // Add these to avoid uninteresting call warnings:
        EXPECT_CALL(*testSegment, GetAllocatedCount()).WillRepeatedly(::testing::Return(0));
        EXPECT_CALL(*testSegment, Release(0)).WillRepeatedly(::testing::Return(true));
        segmentCreateCount++;
        return std::move(testSegment);
    };

    // Verify that no segments are created initially
    EXPECT_EQ(segmentCreateCount, 0);

    // Action: Ask for remaining descriptors (should not create segment)
    allocator->GetRemainingDescriptorsCount(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    EXPECT_EQ(segmentCreateCount, 0);

    // Action: Allocate a descriptor (should create segment)
    auto handle = allocator->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    EXPECT_EQ(segmentCreateCount, 1);
}

NOLINT_TEST_F(BaseDescriptorAllocatorGrowthTest, ThrowsIfOutOfSpaceWithGrowthLimit)
{
    // Tests that allocator throws when reaching the growth limit

    // Setup: Create segments that allow one allocation each, then fail
    uint32_t createCount { 0 };
    uint32_t lastBaseIndex = 0;
    allocator->segment_factory_ = [&createCount, &lastBaseIndex](auto type, auto vis) {
        auto seg = std::make_unique<MockDescriptorHeapSegment>();
        uint32_t baseIndex = lastBaseIndex;
        lastBaseIndex = baseIndex + 1; // Each segment has capacity 1
        // Each segment allows one allocation, then returns kInvalidIndex
        EXPECT_CALL(*seg, Allocate())
            .WillOnce(::testing::Return(baseIndex))
            .WillRepeatedly(::testing::Return(DescriptorHandle::kInvalidIndex));
        EXPECT_CALL(*seg, Release(baseIndex)).WillOnce(::testing::Return(true));
        EXPECT_CALL(*seg, GetViewType()).WillRepeatedly(::testing::Return(type));
        EXPECT_CALL(*seg, GetVisibility()).WillRepeatedly(::testing::Return(vis));
        EXPECT_CALL(*seg, GetBaseIndex()).WillRepeatedly(::testing::Return(baseIndex));
        EXPECT_CALL(*seg, GetCapacity()).WillRepeatedly(::testing::Return(1));
        EXPECT_CALL(*seg, GetAllocatedCount()).WillOnce(::testing::Return(0)).WillRepeatedly(::testing::Return(1));
        createCount++;
        return seg;
    };

    // Action & Verify: Should succeed for maxGrowthIterations + 1 allocations, then throw
    auto key = default_config.heap_strategy->GetHeapKey(
        ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    const auto maxGrowthIterations = default_config.heap_strategy->GetHeapDescription(key).max_growth_iterations;

    std::vector<DescriptorHandle> handles;
    for (uint32_t i = 0; i < maxGrowthIterations + 1; ++i) {
        auto h = allocator->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
        EXPECT_TRUE(h.IsValid());
        handles.push_back(std::move(h));
        EXPECT_TRUE(handles.back().IsValid());
        EXPECT_EQ(handles.back().GetIndex(), i);
    }

    // Next allocation should throw
    EXPECT_THROW(
        allocator->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible),
        std::runtime_error);

    EXPECT_EQ(createCount, maxGrowthIterations + 1);
}

NOLINT_TEST_F(BaseDescriptorAllocatorGrowthTest, ReuseAfterGrowth)
{
    // Setup: Create segments that allow one allocation each, then fail
    uint32_t createCount { 0 };
    uint32_t lastBaseIndex = 0;
    allocator->segment_factory_ = [&createCount, &lastBaseIndex](auto type, auto vis) {
        auto seg = std::make_unique<MockDescriptorHeapSegment>();
        uint32_t baseIndex = lastBaseIndex;
        lastBaseIndex = baseIndex + 1; // Each segment has capacity 1
        if (baseIndex == 0) {
            EXPECT_CALL(*seg, Allocate())
                .WillOnce(::testing::Return(0))
                .WillOnce(::testing::Return(0))
                .WillRepeatedly(::testing::Return(DescriptorHandle::kInvalidIndex));
        } else {
            EXPECT_CALL(*seg, Allocate())
                .WillOnce(::testing::Return(baseIndex))
                .WillRepeatedly(::testing::Return(DescriptorHandle::kInvalidIndex));
        }
        if (baseIndex == 0) {
            EXPECT_CALL(*seg, GetAllocatedCount())
                .WillOnce(::testing::Return(0))
                .WillOnce(::testing::Return(1))
                .WillOnce(::testing::Return(0))
                .WillRepeatedly(::testing::Return(1));
        }
        EXPECT_CALL(*seg, Release(baseIndex)).WillRepeatedly(::testing::Return(true));
        EXPECT_CALL(*seg, GetViewType()).WillRepeatedly(::testing::Return(type));
        EXPECT_CALL(*seg, GetVisibility()).WillRepeatedly(::testing::Return(vis));
        EXPECT_CALL(*seg, GetBaseIndex()).WillRepeatedly(::testing::Return(baseIndex));
        EXPECT_CALL(*seg, GetCapacity()).WillRepeatedly(::testing::Return(1));
        createCount++;
        return seg;
    };

    // Action 1: First allocation - should use segment 1 with index 100
    auto h1 = allocator->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    EXPECT_TRUE(h1.IsValid());
    EXPECT_EQ(h1.GetIndex(), 0);

    // Action 2: Second allocation - segment 1 is full, should use segment 2 with index 200
    auto h2 = allocator->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    EXPECT_TRUE(h2.IsValid());
    EXPECT_EQ(h2.GetIndex(), 1);

    // Action 3: Release the first allocation, making space in segment 1
    allocator->Release(h1);

    // Action 4: Third allocation - should reuse segment 1 (index 100)
    auto h3 = allocator->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    EXPECT_TRUE(h3.IsValid());
    EXPECT_EQ(h3.GetIndex(), 0); // Should reuse the index from segment 1

    EXPECT_EQ(createCount, 2);
}

NOLINT_TEST_F(BaseDescriptorAllocatorGrowthTest, SegmentCreationFailureDuringGrowth)
{
    // Setup: Factory that fails after first segment
    int createCount = 0;
    uint32_t lastBaseIndex = 0;
    allocator->segment_factory_ = [&createCount, &lastBaseIndex](auto type, auto vis) {
        createCount++;
        if (createCount == 1) {
            auto seg = std::make_unique<MockDescriptorHeapSegment>();
            uint32_t baseIndex = lastBaseIndex;
            lastBaseIndex = baseIndex + 1;
            EXPECT_CALL(*seg, Allocate()).WillOnce(::testing::Return(baseIndex)).WillRepeatedly(::testing::Return(DescriptorHandle::kInvalidIndex));
            EXPECT_CALL(*seg, Release(baseIndex)).WillOnce(::testing::Return(true));
            EXPECT_CALL(*seg, GetViewType()).WillRepeatedly(::testing::Return(type));
            EXPECT_CALL(*seg, GetVisibility()).WillRepeatedly(::testing::Return(vis));
            EXPECT_CALL(*seg, GetBaseIndex()).WillRepeatedly(::testing::Return(baseIndex));
            EXPECT_CALL(*seg, GetCapacity()).WillRepeatedly(::testing::Return(1));
            EXPECT_CALL(*seg, GetAllocatedCount()).WillOnce(::testing::Return(0)).WillRepeatedly(::testing::Return(1));
            return seg;
        }
        // Return null to simulate segment creation failure
        return std::unique_ptr<MockDescriptorHeapSegment> {};
    };

    // First allocation should succeed
    auto handle = allocator->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    EXPECT_TRUE(handle.IsValid());
    EXPECT_EQ(handle.GetIndex(), 0);

    // Second allocation should throw due to segment creation failure
    EXPECT_THROW(allocator->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible), std::runtime_error);

    EXPECT_EQ(createCount, 2);
}
