//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

/**
 * @file BaseDescriptorAllocator_Config_test.cpp
 *
 * Unit tests for the BaseDescriptorAllocator class covering configuration,
 * initialization, and related logic.
 */

#include <memory>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/Detail/BaseDescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

#include "./BaseDescriptorAllocatorTest.h"
#include "./Mocks/MockDescriptorAllocator.h"
#include "./Mocks/MockDescriptorHeapSegment.h"

using oxygen::graphics::DescriptorAllocationStrategy;
using oxygen::graphics::DescriptorHandle;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::bindless::testing::BaseDescriptorAllocatorTest;
using oxygen::graphics::bindless::testing::MockDescriptorAllocator;
using oxygen::graphics::bindless::testing::MockDescriptorHeapSegment;
using oxygen::graphics::bindless::testing::ZeroCapacityDescriptorAllocationStrategy;
using oxygen::graphics::detail::BaseDescriptorAllocatorConfig;
using oxygen::graphics::detail::DescriptorHeapSegment;

// -------------------- Test Fixture --------------------
class BaseDescriptorAllocatorInitTest : public BaseDescriptorAllocatorTest {
};

// -------------------- Configuration Tests --------------------

NOLINT_TEST_F(BaseDescriptorAllocatorInitTest, DefaultStrategyFallback)
{
    // Tests that the allocator uses a default allocation strategy when none is provided
    // This tests the fallback mechanism in the constructor

    // Setup: Create a config with null heap strategy
    BaseDescriptorAllocatorConfig nullStrategyConfig;
    nullStrategyConfig.heap_strategy = nullptr;

    // Setup: Create a segment for the allocator to use
    auto mockSegment = std::make_unique<::testing::NiceMock<MockDescriptorHeapSegment>>();
    EXPECT_CALL(*mockSegment, GetAvailableCount()).WillRepeatedly(::testing::Return(1));
    EXPECT_CALL(*mockSegment, Release(0)).WillRepeatedly(::testing::Return(true));
    EXPECT_CALL(*mockSegment, GetViewType()).WillRepeatedly(::testing::Return(ResourceViewType::kTexture_SRV));
    EXPECT_CALL(*mockSegment, GetVisibility()).WillRepeatedly(::testing::Return(DescriptorVisibility::kShaderVisible));

    // Create an allocator with null strategy config
    auto nullStrategyAllocator = std::make_unique<MockDescriptorAllocator>(nullStrategyConfig);

    // Set the segment factory (must take only two arguments)
    nullStrategyAllocator->segment_factory_ = [&mockSegment](auto, auto) {
        return std::move(mockSegment);
    };

    // Action & Verify: Allocation should throw due to zero default capacity in default strategy
    EXPECT_THROW({
        auto handle = nullStrategyAllocator->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
        (void)handle;
    }, std::runtime_error);
}

NOLINT_TEST_F(BaseDescriptorAllocatorInitTest, ZeroInitialCapacityFailsAllocation)
{
    // Tests that setting an initial capacity of zero causes allocation to fail

    // Setup: Create a config with zero capacity for a specific type
    auto z_allocator = std::make_unique<MockDescriptorAllocator>(BaseDescriptorAllocatorConfig {
        .heap_strategy = std::make_unique<ZeroCapacityDescriptorAllocationStrategy>(),
    });

    // This factory should never be called since we'll fail on capacity check
    z_allocator->segment_factory_ = [](auto, auto) {
        ADD_FAILURE() << "Segment factory called despite zero capacity";
        return std::unique_ptr<MockDescriptorHeapSegment> {};
    };

    // Action & Verify: Allocation should throw
    EXPECT_THROW(z_allocator->Allocate(
                     ResourceViewType::kTexture_SRV,
                     DescriptorVisibility::kShaderVisible),
        std::runtime_error);
}
