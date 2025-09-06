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
#include "./Mocks/MockDescriptorSegment.h"

using oxygen::kInvalidBindlessHandle;
using oxygen::graphics::DescriptorHandle;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::ResourceViewType;

using oxygen::graphics::bindless::testing::BaseDescriptorAllocatorTest;
using oxygen::graphics::bindless::testing::MockDescriptorAllocator;
using oxygen::graphics::bindless::testing::MockDescriptorSegment;

namespace b = oxygen::bindless;

// -------------------- Test Fixture --------------------
class BaseDescriptorAllocatorErrorTest : public BaseDescriptorAllocatorTest { };

// -------------------- Error Handling Tests --------------------

NOLINT_TEST_F(BaseDescriptorAllocatorErrorTest, ThrowsIfOutOfSpaceAndNoGrowth)
{
  // Tests that the allocator throws when out of space and growth is disabled

  // Setup: Create a segment that succeeds once then fails, with growth disabled

  // Explicitly disable heap growth
  DisableGrowth();

  bool one_segment = false;
  allocator_->segment_factory_ =
    [this, &one_segment](auto, auto) -> std::unique_ptr<MockDescriptorSegment> {
    if (one_segment) {
      ADD_FAILURE() << "Unexpected segment requested";
      return nullptr;
    }
    one_segment = true;
    auto segment = std::make_unique<MockDescriptorSegment>();
    EXPECT_CALL(*segment, Allocate())
      .WillOnce(testing::Return(b::Handle { 0 }))
      .WillRepeatedly(testing::Return(kInvalidBindlessHandle));
    EXPECT_CALL(*segment, GetAvailableCount())
      .WillRepeatedly(testing::Return(b::Count { 0 }));
    EXPECT_CALL(*segment, Release(b::Handle { 0 }))
      .WillRepeatedly(testing::Return(true));
    EXPECT_CALL(*segment, GetViewType())
      .WillRepeatedly(testing::Return(ResourceViewType::kTexture_SRV));
    EXPECT_CALL(*segment, GetVisibility())
      .WillRepeatedly(testing::Return(DescriptorVisibility::kShaderVisible));
    EXPECT_CALL(*segment, GetBaseIndex())
      .WillRepeatedly(testing::Return(b::Handle { 0 }));
    EXPECT_CALL(*segment, GetCapacity())
      .WillRepeatedly(testing::Return(b::Capacity { 1 }));
    EXPECT_CALL(*segment, GetAllocatedCount())
      .WillOnce(testing::Return(b::Count { 0 }))
      .WillRepeatedly(testing::Return(b::Count { 1 }));
    return std::move(segment);
  };

  // Action & Verify: First allocation succeeds, second throws
  const auto h1 = allocator_->Allocate(
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
  EXPECT_TRUE(h1.IsValid());
  EXPECT_THROW(allocator_->Allocate(ResourceViewType::kTexture_SRV,
                 DescriptorVisibility::kShaderVisible),
    std::runtime_error);
}

NOLINT_TEST_F(
  BaseDescriptorAllocatorErrorTest, ReleaseFromDifferentAllocatorThrows)
{
  // Tests that attempting to release a handle from a different allocator throws

  // Setup: Create two allocators and a handle from the first
  const auto allocator1
    = std::make_unique<testing::NiceMock<MockDescriptorAllocator>>();
  const auto allocator2
    = std::make_unique<testing::NiceMock<MockDescriptorAllocator>>();

  allocator1->segment_factory_ = [](auto, auto) {
    // Create test segment for first allocator
    auto segment1
      = std::make_unique<testing::NiceMock<MockDescriptorSegment>>();
    EXPECT_CALL(*segment1, Allocate())
      .WillOnce(testing::Return(b::Handle { 0 }));
    // Add proper release expectation for cleanup
    EXPECT_CALL(*segment1, Release(b::Handle { 0 }))
      .WillOnce(testing::Return(true));
    EXPECT_CALL(*segment1, GetViewType())
      .WillRepeatedly(testing::Return(ResourceViewType::kTexture_SRV));
    EXPECT_CALL(*segment1, GetVisibility())
      .WillRepeatedly(testing::Return(DescriptorVisibility::kShaderVisible));
    EXPECT_CALL(*segment1, GetBaseIndex())
      .WillRepeatedly(testing::Return(b::Handle { 0 }));
    EXPECT_CALL(*segment1, GetCapacity())
      .WillRepeatedly(testing::Return(b::Capacity { 1 }));
    EXPECT_CALL(*segment1, GetAllocatedCount())
      .WillOnce(testing::Return(b::Count { 0 }))
      .WillRepeatedly(testing::Return(b::Count { 1 }));
    return std::move(segment1);
  };

  // Allocate a handle from the first allocator
  auto handle = allocator1->Allocate(
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);

  // Action & Verify: Attempting to release from allocator2 should throw
  EXPECT_THROW(allocator2->Release(handle), std::runtime_error);

  // Clean up properly with the original allocator
  allocator1->Release(handle);
  EXPECT_FALSE(handle.IsValid());
}
