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
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::ResourceViewType;

using oxygen::graphics::bindless::testing::BaseDescriptorAllocatorTest;
using oxygen::graphics::bindless::testing::MockDescriptorAllocator;
using oxygen::graphics::bindless::testing::MockDescriptorHeapSegment;
using oxygen::graphics::bindless::testing::ZeroCapacityDescriptorAllocationStrategy;

class BaseDescriptorAllocatorInitTest : public BaseDescriptorAllocatorTest {
};

NOLINT_TEST_F(BaseDescriptorAllocatorInitTest, DefaultStrategyFallback)
{
    // Tests that the allocator uses a default allocation strategy when none is provided

    // Setup: Create a config with null heap strategy
    std::shared_ptr<const DescriptorAllocationStrategy> heap_strategy {};

    // Create an allocator with null strategy config
    const auto n_allocator = std::make_unique<MockDescriptorAllocator>(heap_strategy);

    // This should NOT cause the process to die
    EXPECT_NO_FATAL_FAILURE({
        (void)n_allocator->GetAllocationStrategy();
    });
}

NOLINT_TEST_F(BaseDescriptorAllocatorInitTest, ZeroInitialCapacityFailsAllocation)
{
    // Tests that setting an initial capacity of zero causes allocation to fail

    // Setup: Create a config with zero capacity for a specific type
    const auto z_allocator = std::make_unique<::testing::NiceMock<MockDescriptorAllocator>>(
        std::make_unique<ZeroCapacityDescriptorAllocationStrategy>());

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
