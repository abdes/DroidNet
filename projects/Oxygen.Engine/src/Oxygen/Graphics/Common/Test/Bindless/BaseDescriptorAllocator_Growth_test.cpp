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

#include <cmath>
#include <limits>
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
using IndexT = DescriptorHandle::IndexT;

using oxygen::graphics::bindless::testing::BaseDescriptorAllocatorTest;
using oxygen::graphics::bindless::testing::MockDescriptorHeapSegment;

class BaseDescriptorAllocatorGrowthTest : public BaseDescriptorAllocatorTest {
};

NOLINT_TEST_F(BaseDescriptorAllocatorGrowthTest, GrowthPolicyRespected)
{
    // Tests that the heap grows when full if growth is enabled

    // Track segment creation
    auto segment_count = 0U;
    allocator_->segment_factory_ = [this, &segment_count](auto type, auto vis) {
        segment_count++;
        if (segment_count == 1) {
            // Setup: Create a segment that will fill up and need to grow
            auto first_segment = std::make_unique<MockDescriptorHeapSegment>();
            EXPECT_CALL(*first_segment, Allocate())
                .WillOnce(testing::Return(0))
                .WillRepeatedly(testing::Return(DescriptorHandle::kInvalidIndex)); // Second call indicates full
            EXPECT_CALL(*first_segment, GetViewType()).WillRepeatedly(testing::Return(type));
            EXPECT_CALL(*first_segment, GetVisibility()).WillRepeatedly(testing::Return(vis));
            EXPECT_CALL(*first_segment, Release(::testing::_)).Times(1).WillOnce(testing::Return(true));
            EXPECT_CALL(*first_segment, GetBaseIndex()).WillRepeatedly(testing::Return(0));
            EXPECT_CALL(*first_segment, GetCapacity()).WillRepeatedly(testing::Return(1));
            EXPECT_CALL(*first_segment, GetAllocatedCount())
                .WillOnce(testing::Return(0))
                .WillRepeatedly(testing::Return(1));
            return first_segment;
        }

        // Second segment for growth
        auto growth_segment = std::make_unique<MockDescriptorHeapSegment>();
        EXPECT_CALL(*growth_segment, Allocate()).WillOnce(testing::Return(100)); // Different index to distinguish
        EXPECT_CALL(*growth_segment, GetAvailableCount()).WillRepeatedly(testing::Return(1));
        EXPECT_CALL(*growth_segment, GetViewType()).WillRepeatedly(testing::Return(type));
        EXPECT_CALL(*growth_segment, GetVisibility()).WillRepeatedly(testing::Return(vis));
        EXPECT_CALL(*growth_segment, Release(::testing::_)).Times(1).WillOnce(testing::Return(true));
        EXPECT_CALL(*growth_segment, GetBaseIndex()).WillRepeatedly(testing::Return(100));
        EXPECT_CALL(*growth_segment, GetCapacity()).WillRepeatedly(testing::Return(1));
        return growth_segment;
    };

    // Action: Allocate twice, second allocation should cause growth
    auto h1 = allocator_->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    auto h2 = allocator_->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);

    // Verify: Both handles are valid, second segment was created
    EXPECT_TRUE(h1.IsValid());
    EXPECT_TRUE(h2.IsValid());
    EXPECT_EQ(h1.GetIndex(), 0);
    EXPECT_EQ(h2.GetIndex(), 100); // From second segment
    EXPECT_EQ(segment_count, 2);

    h1.Release();
    h2.Release();
}

NOLINT_TEST_F(BaseDescriptorAllocatorGrowthTest, GrowthFactorRespected)
{
    // Tests that the growth factor is respected when creating new segments
    // This ensures that segments grow at the expected rate

    // Setup: Track the requested segment sizes
    std::vector<size_t> requested_sizes;
    const auto key = default_config_.heap_strategy->GetHeapKey(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    const auto initial_capacity = default_config_.heap_strategy->GetHeapDescription(key).shader_visible_capacity;
    const auto growth_factor = default_config_.heap_strategy->GetHeapDescription(key).growth_factor;
    const auto max_growth_iterations = default_config_.heap_strategy->GetHeapDescription(key).max_growth_iterations;

    // Setup factory to track the sizes that would be used for segment creation
    IndexT base_index { 0 };
    allocator_->segment_factory_ = [&requested_sizes, &base_index, initial_capacity, growth_factor](auto type, auto vis) {
        size_t expected_size;

        // Calculate the expected size based on the number of segments already requested
        if (requested_sizes.empty()) {
            expected_size = initial_capacity;
        } else {
            // Cast to double to avoid overflow in the calculation, and then
            // round to the nearest integer to get the expected size
            expected_size = static_cast<size_t>(std::llround(
                static_cast<double>(requested_sizes.back()) * static_cast<double>(growth_factor)));
        }

        requested_sizes.push_back(expected_size);

        // Create a segment that will immediately fail allocation to trigger growth
        auto segment = std::make_unique<MockDescriptorHeapSegment>();
        EXPECT_CALL(*segment, Allocate()).WillRepeatedly(testing::Return(DescriptorHandle::kInvalidIndex));
        EXPECT_CALL(*segment, GetAvailableCount()).WillRepeatedly(testing::Return(0));
        EXPECT_CALL(*segment, GetViewType()).WillRepeatedly(testing::Return(type));
        EXPECT_CALL(*segment, GetVisibility()).WillRepeatedly(testing::Return(vis));
        EXPECT_CALL(*segment, GetCapacity()).WillRepeatedly(testing::Return(1));
        EXPECT_CALL(*segment, GetAllocatedCount()).WillRepeatedly(testing::Return(1));
        EXPECT_CALL(*segment, GetBaseIndex()).WillRepeatedly(testing::Return(base_index));

        ++base_index;
        return segment;
    };

    // Action: Try to allocate until max growth iterations
    // Each allocation will fail and trigger segment creation with increasing sizes
    for (auto i = 0U; i < max_growth_iterations; ++i) {
        try {
            allocator_->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
        } catch (const std::runtime_error&) {
            // Expected to throw when we run out of growth iterations
            break;
        }
    }

    // Verify: We should have seen requested sizes grow according to the growth factor
    ASSERT_GE(requested_sizes.size(), 2);
    for (auto i = 1U; i < requested_sizes.size(); ++i) {
        const float actual_ratio = static_cast<float>(static_cast<double>(requested_sizes[i]) / static_cast<double>(requested_sizes[i - 1]));
        EXPECT_NEAR(actual_ratio, growth_factor, 0.1f);
    }
}

NOLINT_TEST_F(BaseDescriptorAllocatorGrowthTest, SegmentCreatedOnlyWhenNeeded)
{
    // Tests that segments are created only when needed, not preemptively

    auto create_count = 0U;

    allocator_->segment_factory_ = [&create_count](auto, auto) {
        auto segment = std::make_unique<MockDescriptorHeapSegment>();
        EXPECT_CALL(*segment, Allocate()).WillOnce(testing::Return(0));
        EXPECT_CALL(*segment, GetAvailableCount()).WillRepeatedly(testing::Return(1));
        EXPECT_CALL(*segment, GetViewType()).WillRepeatedly(testing::Return(ResourceViewType::kTexture_SRV));
        EXPECT_CALL(*segment, GetVisibility()).WillRepeatedly(testing::Return(DescriptorVisibility::kShaderVisible));
        EXPECT_CALL(*segment, GetBaseIndex()).WillRepeatedly(testing::Return(0));
        EXPECT_CALL(*segment, GetCapacity()).WillRepeatedly(testing::Return(1));
        EXPECT_CALL(*segment, GetAllocatedCount()).WillRepeatedly(testing::Return(0));
        EXPECT_CALL(*segment, Release(0)).WillRepeatedly(testing::Return(true));
        create_count++;
        return std::move(segment);
    };

    // Verify that no segments are created initially
    EXPECT_EQ(create_count, 0);

    // Action: Ask for remaining descriptors (should not create segment)
    [[maybe_unused]] auto _ = allocator_->GetRemainingDescriptorsCount(
        ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    EXPECT_EQ(create_count, 0);

    // Action: Allocate a descriptor (should create segment)
    auto handle = allocator_->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    EXPECT_EQ(create_count, 1);
}

NOLINT_TEST_F(BaseDescriptorAllocatorGrowthTest, ThrowsIfOutOfSpaceWithGrowthLimit)
{
    // Tests that allocator throws when reaching the growth limit

    // Setup: Create segments that allow one allocation each, then fail
    uint32_t create_count { 0 };
    IndexT last_base_index = 0;
    allocator_->segment_factory_ = [&create_count, &last_base_index](auto type, auto vis) {
        auto segment = std::make_unique<MockDescriptorHeapSegment>();
        const auto base_index = last_base_index;
        last_base_index = base_index + 1; // Each segment has capacity 1

        // Each segment allows one allocation, then returns kInvalidIndex
        EXPECT_CALL(*segment, Allocate())
            .WillOnce(testing::Return(base_index))
            .WillRepeatedly(testing::Return(DescriptorHandle::kInvalidIndex));
        EXPECT_CALL(*segment, Release(base_index)).WillOnce(testing::Return(true));
        EXPECT_CALL(*segment, GetViewType()).WillRepeatedly(testing::Return(type));
        EXPECT_CALL(*segment, GetVisibility()).WillRepeatedly(testing::Return(vis));
        EXPECT_CALL(*segment, GetBaseIndex()).WillRepeatedly(testing::Return(base_index));
        EXPECT_CALL(*segment, GetCapacity()).WillRepeatedly(testing::Return(1));
        EXPECT_CALL(*segment, GetAllocatedCount()).WillOnce(testing::Return(0)).WillRepeatedly(testing::Return(1));

        create_count++;
        return segment;
    };

    // Action & Verify: Should succeed for maxGrowthIterations + 1 allocations, then throw
    const auto key = default_config_.heap_strategy->GetHeapKey(
        ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    const auto max_growth_iterations = default_config_.heap_strategy->GetHeapDescription(key).max_growth_iterations;

    std::vector<DescriptorHandle> handles;
    for (auto i = 0U; i < max_growth_iterations + 1; ++i) {
        auto h = allocator_->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
        EXPECT_TRUE(h.IsValid());
        handles.push_back(std::move(h));
        EXPECT_TRUE(handles.back().IsValid());
        EXPECT_EQ(handles.back().GetIndex(), i);
    }

    // Next allocation should throw
    EXPECT_THROW(
        allocator_->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible),
        std::runtime_error);

    EXPECT_EQ(create_count, max_growth_iterations + 1);
}

NOLINT_TEST_F(BaseDescriptorAllocatorGrowthTest, ReuseAfterGrowth)
{
    // Setup: Create segments that allow one allocation each, then fail
    auto create_count { 0U };
    IndexT last_base_index { 0 };
    allocator_->segment_factory_ = [&create_count, &last_base_index](auto type, auto vis) {
        auto segment = std::make_unique<MockDescriptorHeapSegment>();
        const auto base_index = last_base_index;
        last_base_index = base_index + 1;

        // Each segment has capacity 1
        if (base_index == 0) {
            EXPECT_CALL(*segment, Allocate())
                .WillOnce(testing::Return(0))
                .WillOnce(testing::Return(0))
                .WillRepeatedly(testing::Return(DescriptorHandle::kInvalidIndex));
        } else {
            EXPECT_CALL(*segment, Allocate())
                .WillOnce(testing::Return(base_index))
                .WillRepeatedly(testing::Return(DescriptorHandle::kInvalidIndex));
        }
        if (base_index == 0) {
            EXPECT_CALL(*segment, GetAllocatedCount())
                .WillOnce(testing::Return(0))
                .WillOnce(testing::Return(1))
                .WillOnce(testing::Return(0))
                .WillRepeatedly(testing::Return(1));
        }
        EXPECT_CALL(*segment, Release(base_index)).WillRepeatedly(testing::Return(true));
        EXPECT_CALL(*segment, GetViewType()).WillRepeatedly(testing::Return(type));
        EXPECT_CALL(*segment, GetVisibility()).WillRepeatedly(testing::Return(vis));
        EXPECT_CALL(*segment, GetBaseIndex()).WillRepeatedly(testing::Return(base_index));
        EXPECT_CALL(*segment, GetCapacity()).WillRepeatedly(testing::Return(1));

        create_count++;
        return segment;
    };

    // Action 1: First allocation - should use segment 1 with index 100
    auto h1 = allocator_->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    EXPECT_TRUE(h1.IsValid());
    EXPECT_EQ(h1.GetIndex(), 0);

    // Action 2: Second allocation - segment 1 is full, should use segment 2 with index 200
    const auto h2 = allocator_->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    EXPECT_TRUE(h2.IsValid());
    EXPECT_EQ(h2.GetIndex(), 1);

    // Action 3: Release the first allocation, making space in segment 1
    allocator_->Release(h1);

    // Action 4: Third allocation - should reuse segment 1 (index 100)
    const auto h3 = allocator_->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    EXPECT_TRUE(h3.IsValid());
    EXPECT_EQ(h3.GetIndex(), 0); // Should reuse the index from segment 1

    EXPECT_EQ(create_count, 2);
}

NOLINT_TEST_F(BaseDescriptorAllocatorGrowthTest, SegmentCreationFailureDuringGrowth)
{
    // Setup: Factory that fails after first segment
    auto create_count { 0U };
    IndexT last_base_index { 0 };
    allocator_->segment_factory_ = [&create_count, &last_base_index](auto type, auto vis) {
        create_count++;
        if (create_count == 1) {
            auto segment = std::make_unique<MockDescriptorHeapSegment>();
            const uint32_t base_index = last_base_index;
            last_base_index = base_index + 1;

            EXPECT_CALL(*segment, Allocate())
                .WillOnce(testing::Return(base_index))
                .WillRepeatedly(testing::Return(DescriptorHandle::kInvalidIndex));
            EXPECT_CALL(*segment, Release(base_index)).WillOnce(testing::Return(true));
            EXPECT_CALL(*segment, GetViewType()).WillRepeatedly(testing::Return(type));
            EXPECT_CALL(*segment, GetVisibility()).WillRepeatedly(testing::Return(vis));
            EXPECT_CALL(*segment, GetBaseIndex()).WillRepeatedly(testing::Return(base_index));
            EXPECT_CALL(*segment, GetCapacity()).WillRepeatedly(testing::Return(1));
            EXPECT_CALL(*segment, GetAllocatedCount())
                .WillOnce(testing::Return(0))
                .WillRepeatedly(testing::Return(1));

            return segment;
        }
        // Return null to simulate segment creation failure
        return std::unique_ptr<MockDescriptorHeapSegment> {};
    };

    // First allocation should succeed
    const auto handle = allocator_->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    EXPECT_TRUE(handle.IsValid());
    EXPECT_EQ(handle.GetIndex(), 0);

    // Second allocation should throw due to segment creation failure
    EXPECT_THROW(allocator_->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible), std::runtime_error);

    EXPECT_EQ(create_count, 2);
}

//! Allocator that tracks growth requests for testing purposes.
class GrowthTestAllocator final : public oxygen::graphics::detail::BaseDescriptorAllocator {
    using Base = BaseDescriptorAllocator;

public:
    using IndexT = DescriptorHandle::IndexT;
    IndexT last_requested_capacity_ = 0;
    IndexT last_requested_base_index_ = 0;
    ResourceViewType last_type_ = ResourceViewType::kNone;
    DescriptorVisibility last_vis_ = DescriptorVisibility::kNone;

    explicit GrowthTestAllocator(const oxygen::graphics::detail::BaseDescriptorAllocatorConfig& config)
        : BaseDescriptorAllocator(config)
    {
    }

protected:
    auto CreateHeapSegment(
        const IndexT capacity,
        const IndexT base_index,
        const ResourceViewType type,
        const DescriptorVisibility vis)
        -> std::unique_ptr<oxygen::graphics::detail::DescriptorHeapSegment> override
    {
        last_requested_capacity_ = capacity;
        last_requested_base_index_ = base_index;
        last_type_ = type;
        last_vis_ = vis;

        // Return a dummy segment that always fails allocation (to avoid further growth)
        struct DummySegment : oxygen::graphics::detail::DescriptorHeapSegment {
            bool allocated_once { false };

            auto Allocate() noexcept -> IndexT override
            {
                if (allocated_once) {
                    return DescriptorHandle::kInvalidIndex;
                }
                allocated_once = true;
                return 0;
            }

            auto Release(IndexT) noexcept -> bool override { return true; }
            [[nodiscard]] auto GetAvailableCount() const noexcept -> IndexT override { return allocated_once ? 0 : 10; }
            [[nodiscard]] auto GetViewType() const noexcept -> ResourceViewType override { return ResourceViewType::kTexture_SRV; }
            [[nodiscard]] auto GetVisibility() const noexcept -> DescriptorVisibility override { return DescriptorVisibility::kShaderVisible; }
            [[nodiscard]] auto GetBaseIndex() const noexcept -> IndexT override { return 0; }
            [[nodiscard]] auto GetCapacity() const noexcept -> IndexT override { return 10; }
            [[nodiscard]] auto GetAllocatedCount() const noexcept -> IndexT override { return allocated_once ? 10 : 0; }
        };
        return std::make_unique<DummySegment>();
    }

    // Expose GetInitialCapacity for testing
    using Base::GetInitialCapacity;

public:
    void CopyDescriptor(const DescriptorHandle& /*source*/, const DescriptorHandle& /*destination*/) override { }
    [[nodiscard]] auto GetNativeHandle(const DescriptorHandle& /*handle*/) const
        -> oxygen::graphics::NativeObject override
    {
        return { 0ULL, oxygen::kInvalidTypeId };
    }
    void PrepareForRendering(const oxygen::graphics::NativeObject& /*command_list*/) override { }
};

NOLINT_TEST_F(BaseDescriptorAllocatorGrowthTest, GrowthCapacityClampedToMaxIndexT)
{
    using oxygen::graphics::DescriptorAllocationStrategy;
    using oxygen::graphics::HeapDescription;
    using oxygen::graphics::detail::BaseDescriptorAllocatorConfig;

    // Custom heap strategy with a huge growth factor to force overflow
    struct HugeGrowthStrategy : DescriptorAllocationStrategy {
        [[nodiscard]] auto GetHeapKey(ResourceViewType /*type*/, DescriptorVisibility /*vis*/) const
            -> std::string override
        {
            return "Texture_SRV:gpu";
        }

        [[nodiscard]] auto GetHeapDescription(const std::string& /*key*/) const
            -> const HeapDescription& override
        {
            static HeapDescription desc {
                .cpu_visible_capacity = 10,
                .shader_visible_capacity = 10,
                .allow_growth = true,
                .growth_factor = static_cast<float>(std::numeric_limits<IndexT>::max()),
                .max_growth_iterations = 3
            };
            return desc;
        }

        [[nodiscard]] auto GetHeapBaseIndex(ResourceViewType /*type*/, DescriptorVisibility /*vis*/) const
            -> DescriptorHandle::IndexT override
        {
            return 0;
        }
    };

    const BaseDescriptorAllocatorConfig config {
        .heap_strategy = std::make_shared<HugeGrowthStrategy>(),
    };
    GrowthTestAllocator allocator(config);

    // Patch the internal state to simulate a segment with large capacity
    // so that growth will overflow
    auto key = config.heap_strategy->GetHeapKey(
        ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);

    // Allocate once to create the initial segment
    allocator.Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);

    // Now, simulate a segment with a capacity that will overflow on growth
    // (the allocator will use the growth factor on the last segment's capacity)
    // We need to trigger growth, so we force allocation again
    allocator.Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);

    // The last_requested_capacity should be clamped to max(IndexT)
    EXPECT_EQ(allocator.last_requested_capacity_, std::numeric_limits<IndexT>::max());
}
