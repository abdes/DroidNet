//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

#include "./BaseDescriptorAllocatorTest.h"
#include "./Mocks/MockDescriptorAllocator.h"
#include "./Mocks/MockDescriptorSegment.h"

using oxygen::graphics::DescriptorHandle;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::detail::DescriptorSegment;

using oxygen::graphics::bindless::testing::BaseDescriptorAllocatorTest;
using oxygen::graphics::bindless::testing::MockDescriptorAllocator;
using oxygen::graphics::bindless::testing::MockDescriptorSegment;
using oxygen::graphics::bindless::testing::
  OneCapacityDescriptorAllocationStrategy;

namespace b = oxygen::bindless;

// -------------------- Test Fixture -------------------------------------------
class BaseDescriptorAllocatorDomainTest : public BaseDescriptorAllocatorTest {
};

// -------------------- Domain Base Index --------------------------------------

namespace {

//! Domain base index must match the allocation strategy's base for the domain.
NOLINT_TEST_F(
  BaseDescriptorAllocatorDomainTest, GetDomainBaseIndexMatchesStrategy)
{
  // Arrange: use a known strategy instance and re-create the allocator
  heap_strategy_ = std::make_shared<OneCapacityDescriptorAllocationStrategy>();
  allocator_ = std::make_unique<testing::NiceMock<MockDescriptorAllocator>>(
    heap_strategy_);

  constexpr std::pair<ResourceViewType, DescriptorVisibility> domains[] = {
    { ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible },
    { ResourceViewType::kTexture_UAV, DescriptorVisibility::kShaderVisible },
    { ResourceViewType::kSampler, DescriptorVisibility::kShaderVisible },
    { ResourceViewType::kTexture_RTV, DescriptorVisibility::kCpuOnly },
  };

  // Act + Assert
  for (const auto& [type, vis] : domains) {
    const auto base_from_allocator = allocator_->GetDomainBaseIndex(type, vis);
    const auto base_from_strategy = heap_strategy_->GetHeapBaseIndex(type, vis);
    EXPECT_EQ(base_from_allocator, base_from_strategy);
  }
}

// -------------------- Reservation Success/Failure ----------------------------

//! Reserve succeeds when count <= capacity; segment creation optional.
NOLINT_TEST_F(
  BaseDescriptorAllocatorDomainTest, ReserveWithinCapacity_NoSegment)
{
  // Arrange: One item capacity per domain; do not create segments in Reserve()
  heap_strategy_ = std::make_shared<OneCapacityDescriptorAllocationStrategy>();
  allocator_ = std::make_unique<testing::NiceMock<MockDescriptorAllocator>>(
    heap_strategy_);

  constexpr auto kType = ResourceViewType::kTexture_SRV;
  constexpr auto kVis = DescriptorVisibility::kShaderVisible;
  const auto expected_base = heap_strategy_->GetHeapBaseIndex(kType, kVis);

  // Act
  const auto reserved = allocator_->Reserve(kType, kVis, b::Count { 1 });

  // Assert
  ASSERT_TRUE(reserved.has_value());
  EXPECT_EQ(reserved.value(), expected_base);
}

//! Reserve may create the initial segment; subsequent Allocate uses that
//! segment.
NOLINT_TEST_F(BaseDescriptorAllocatorDomainTest,
  ReserveWithinCapacity_CreatesSegmentAndAllocates)
{
  // Arrange
  heap_strategy_ = std::make_shared<OneCapacityDescriptorAllocationStrategy>();
  allocator_ = std::make_unique<testing::NiceMock<MockDescriptorAllocator>>(
    heap_strategy_);

  constexpr auto kType = ResourceViewType::kTexture_SRV;
  constexpr auto kVis = DescriptorVisibility::kShaderVisible;

  // Create segment during Reserve() and verify base index and capacity are
  // honored.
  allocator_->ext_segment_factory_ = [](const b::Capacity capacity,
                                       const b::HeapIndex base_index,
                                       const ResourceViewType vt,
                                       const DescriptorVisibility vis) {
    auto seg = std::make_unique<testing::NiceMock<MockDescriptorSegment>>();
    EXPECT_CALL(*seg, GetViewType()).WillRepeatedly(testing::Return(vt));
    EXPECT_CALL(*seg, GetVisibility()).WillRepeatedly(testing::Return(vis));
    EXPECT_CALL(*seg, GetBaseIndex())
      .WillRepeatedly(testing::Return(base_index));
    EXPECT_CALL(*seg, GetCapacity()).WillRepeatedly(testing::Return(capacity));
    EXPECT_CALL(*seg, GetAllocatedCount())
      .WillRepeatedly(testing::Return(b::Count { 0 }));
    EXPECT_CALL(*seg, GetAvailableCount())
      .WillRepeatedly(testing::Return(b::Count { capacity.get() }));
    EXPECT_CALL(*seg, Allocate()).WillOnce(testing::Return(base_index));
    EXPECT_CALL(*seg, Release(base_index)).WillOnce(testing::Return(true));
    return seg;
  };

  // Act: Reserve then Allocate one descriptor
  const auto reserved = allocator_->Reserve(kType, kVis, b::Count { 1 });
  ASSERT_TRUE(reserved.has_value());
  auto handle = allocator_->Allocate(kType, kVis);

  // Assert
  EXPECT_TRUE(handle.IsValid());
  EXPECT_EQ(handle.GetBindlessHandle(), reserved.value());

  allocator_->Release(handle);
  EXPECT_FALSE(handle.IsValid());
}

//! Reserve fails when count exceeds capacity.
NOLINT_TEST_F(BaseDescriptorAllocatorDomainTest, ReserveExceedingCapacityFails)
{
  // Arrange
  heap_strategy_ = std::make_shared<OneCapacityDescriptorAllocationStrategy>();
  allocator_ = std::make_unique<testing::NiceMock<MockDescriptorAllocator>>(
    heap_strategy_);

  // Act
  const auto reserved_gpu = allocator_->Reserve(ResourceViewType::kTexture_SRV,
    DescriptorVisibility::kShaderVisible, b::Count { 2 });
  const auto reserved_cpu = allocator_->Reserve(ResourceViewType::kTexture_SRV,
    DescriptorVisibility::kCpuOnly, b::Count { 2 });

  // Assert
  EXPECT_FALSE(reserved_gpu.has_value());
  EXPECT_FALSE(reserved_cpu.has_value());
}

} // namespace
