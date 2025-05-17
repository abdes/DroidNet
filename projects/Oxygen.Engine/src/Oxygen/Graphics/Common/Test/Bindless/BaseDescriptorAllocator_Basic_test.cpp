//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

/**
 * @file BaseDescriptorAllocator_Basic_test.cpp
 *
 * Unit tests for the BaseDescriptorAllocator class covering basic allocation
 * and release functionality.
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
#include "./Mocks/TestDescriptorHandle.h"

using oxygen::graphics::DescriptorHandle;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::detail::BaseDescriptorAllocatorConfig;
using oxygen::graphics::detail::DescriptorHeapSegment;

using oxygen::graphics::bindless::testing::BaseDescriptorAllocatorTest;
using oxygen::graphics::bindless::testing::MockDescriptorAllocator;
using oxygen::graphics::bindless::testing::MockDescriptorHeapSegment;
using oxygen::graphics::bindless::testing::TestDescriptorHandle;

// -------------------- Test Fixture --------------------
class BaseDescriptorAllocatorBasicTest : public BaseDescriptorAllocatorTest {
};

// -------------------- Basic Allocation Tests --------------------

// Tests that the allocator creates a new heap segment when allocating from an
// empty heap (i.e., first allocation).
NOLINT_TEST_F(BaseDescriptorAllocatorBasicTest, AllocatesFromEmptyHeapCreatesSegment)
{
    bool called = false;
    allocator->segment_factory_ = [&called](auto, auto) -> std::unique_ptr<DescriptorHeapSegment> {
        if (called) {
            ADD_FAILURE() << "Segment factory called more than once";
            return nullptr;
        }

        called = true;
        auto seg = std::make_unique<::testing::NiceMock<MockDescriptorHeapSegment>>();
        EXPECT_CALL(*seg, Allocate()).WillOnce(::testing::Return(0));
        EXPECT_CALL(*seg, Release(0)).WillOnce(::testing::Return(true));
        EXPECT_CALL(*seg, GetAvailableCount()).WillRepeatedly(::testing::Return(1));
        EXPECT_CALL(*seg, GetViewType()).WillRepeatedly(::testing::Return(ResourceViewType::kTexture_SRV));
        EXPECT_CALL(*seg, GetVisibility()).WillRepeatedly(::testing::Return(DescriptorVisibility::kShaderVisible));
        EXPECT_CALL(*seg, GetBaseIndex()).WillRepeatedly(::testing::Return(0));
        EXPECT_CALL(*seg, GetCapacity()).WillRepeatedly(::testing::Return(1));
        return seg;
    };

    // Action: Allocate a descriptor
    auto handle = allocator->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);

    // Verify: Handle should be valid
    EXPECT_TRUE(handle.IsValid());
    EXPECT_EQ(handle.GetIndex(), 0);

    // Explicitly release the handle to ensure the Release method gets called
    allocator->Release(handle);
    EXPECT_FALSE(handle.IsValid());
}

NOLINT_TEST_F(BaseDescriptorAllocatorBasicTest, AllocatesFromNonEmptyHeapNoNewSegment)
{
    // Tests that subsequent allocations reuse the existing heap segment
    // rather than creating new ones

    int allocCount = 0;
    allocator->segment_factory_ = [this, &allocCount](auto, auto) {
        auto testSegment = std::make_unique<::testing::NiceMock<MockDescriptorHeapSegment>>();
        EXPECT_CALL(*testSegment, Allocate())
            .WillOnce([&]() { allocCount++; return 0; })
            .WillOnce([&]() { allocCount++; return 1; });
        EXPECT_CALL(*testSegment, GetAvailableCount()).WillRepeatedly(::testing::Return(1));
        EXPECT_CALL(*testSegment, Release(0)).WillOnce(::testing::Return(true));
        EXPECT_CALL(*testSegment, Release(1)).WillOnce(::testing::Return(true));
        EXPECT_CALL(*testSegment, GetViewType()).WillRepeatedly(::testing::Return(ResourceViewType::kTexture_SRV));
        EXPECT_CALL(*testSegment, GetVisibility()).WillRepeatedly(::testing::Return(DescriptorVisibility::kShaderVisible));
        EXPECT_CALL(*testSegment, GetBaseIndex()).WillRepeatedly(::testing::Return(0));
        EXPECT_CALL(*testSegment, GetCapacity()).WillRepeatedly(::testing::Return(2));
        return std::move(testSegment);
    };

    // Action: Perform two allocations from the same heap
    auto h1 = allocator->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    auto h2 = allocator->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);

    // Verify: Both allocations were made and handles are valid
    EXPECT_EQ(allocCount, 2);
    EXPECT_TRUE(h1.IsValid());
    EXPECT_TRUE(h2.IsValid());
}

NOLINT_TEST_F(BaseDescriptorAllocatorBasicTest, ReleaseMakesDescriptorAvailable)
{
    allocator->segment_factory_ = [this](auto, auto) {
    auto testSegment = std::make_unique<::testing::NiceMock<MockDescriptorHeapSegment>>();
    EXPECT_CALL(*testSegment, Allocate()).WillOnce(::testing::Return(0));
    EXPECT_CALL(*testSegment, Release(0)).WillOnce(::testing::Return(true));
    EXPECT_CALL(*testSegment, GetAvailableCount()).WillRepeatedly(::testing::Return(1));
    EXPECT_CALL(*testSegment, GetViewType()).WillRepeatedly(::testing::Return(ResourceViewType::kTexture_SRV));
    EXPECT_CALL(*testSegment, GetVisibility()).WillRepeatedly(::testing::Return(DescriptorVisibility::kShaderVisible));
    EXPECT_CALL(*testSegment, GetBaseIndex()).WillRepeatedly(::testing::Return(0));
    EXPECT_CALL(*testSegment, GetCapacity()).WillRepeatedly(::testing::Return(1));
        return std::move(testSegment);
    };

    auto handle = allocator->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    EXPECT_TRUE(handle.IsValid());
    allocator->Release(handle);
    EXPECT_FALSE(handle.IsValid());
}

NOLINT_TEST_F(BaseDescriptorAllocatorBasicTest, ReleasingInvalidHandleIsNoOp)
{
    DescriptorHandle invalid;
    EXPECT_NO_THROW(allocator->Release(invalid));
}

NOLINT_TEST_F(BaseDescriptorAllocatorBasicTest, GetRemainingDescriptorsReturnsCorrectCount)
{
    const uint32_t kAvailableCount = 42;
    allocator->segment_factory_ = [this](auto, auto) {
        auto testSegment = std::make_unique<::testing::NiceMock<MockDescriptorHeapSegment>>();
        EXPECT_CALL(*testSegment, GetAvailableCount()).WillRepeatedly(::testing::Return(kAvailableCount));
        EXPECT_CALL(*testSegment, GetViewType()).WillRepeatedly(::testing::Return(ResourceViewType::kTexture_SRV));
        EXPECT_CALL(*testSegment, GetVisibility()).WillRepeatedly(::testing::Return(DescriptorVisibility::kShaderVisible));
        EXPECT_CALL(*testSegment, GetBaseIndex()).WillRepeatedly(::testing::Return(0));
        EXPECT_CALL(*testSegment, GetCapacity()).WillRepeatedly(::testing::Return(1));
        EXPECT_CALL(*testSegment, Release(0)).WillOnce(::testing::Return(true));
        EXPECT_CALL(*testSegment, Allocate()).WillOnce(::testing::Return(0));
        return std::move(testSegment);
    };

    auto handle = allocator->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);

    auto remainingCount = allocator->GetRemainingDescriptorsCount(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    EXPECT_EQ(remainingCount, kAvailableCount);

    allocator->Release(handle);
    EXPECT_FALSE(handle.IsValid());
}

NOLINT_TEST_F(BaseDescriptorAllocatorBasicTest, HandleRecyclingReusesSameIndex)
{
    allocator->segment_factory_ = [this](auto, auto) {
        auto testSegment = std::make_unique<::testing::NiceMock<MockDescriptorHeapSegment>>();
        EXPECT_CALL(*testSegment, Allocate()).WillRepeatedly(::testing::Return(42));
        EXPECT_CALL(*testSegment, Release(42)).WillRepeatedly(::testing::Return(true));
        EXPECT_CALL(*testSegment, GetAvailableCount()).WillRepeatedly(::testing::Return(1));
        EXPECT_CALL(*testSegment, GetViewType()).WillRepeatedly(::testing::Return(ResourceViewType::kTexture_SRV));
        EXPECT_CALL(*testSegment, GetVisibility()).WillRepeatedly(::testing::Return(DescriptorVisibility::kShaderVisible));
        EXPECT_CALL(*testSegment, GetBaseIndex()).WillRepeatedly(::testing::Return(0));
        EXPECT_CALL(*testSegment, GetCapacity()).WillRepeatedly(::testing::Return(43));
        return std::move(testSegment);
    };

    auto handle1 = allocator->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    uint32_t index1 = handle1.GetIndex();
    allocator->Release(handle1);
    auto handle2 = allocator->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    uint32_t index2 = handle2.GetIndex();

    EXPECT_EQ(index1, index2);

    allocator->Release(handle2);
    EXPECT_FALSE(handle2.IsValid());
}

NOLINT_TEST_F(BaseDescriptorAllocatorBasicTest, HandlesMultipleTypesAndVisibilities)
{
    std::map<std::pair<ResourceViewType, DescriptorVisibility>, bool> segmentCreated;

    allocator->segment_factory_ = [&segmentCreated](auto type, auto vis) {
        segmentCreated[{ type, vis }] = true;
        auto seg = std::make_unique<::testing::NiceMock<MockDescriptorHeapSegment>>();
        EXPECT_CALL(*seg, Allocate()).WillOnce(::testing::Return(0));
        EXPECT_CALL(*seg, Release(0)).WillOnce(::testing::Return(true));
        EXPECT_CALL(*seg, GetAvailableCount()).WillRepeatedly(::testing::Return(1));
        EXPECT_CALL(*seg, GetViewType()).WillRepeatedly(::testing::Return(type));
        EXPECT_CALL(*seg, GetVisibility()).WillRepeatedly(::testing::Return(vis));
        EXPECT_CALL(*seg, GetBaseIndex()).WillRepeatedly(::testing::Return(0));
        EXPECT_CALL(*seg, GetCapacity()).WillRepeatedly(::testing::Return(1));
        return seg;
    };

    auto h1 = allocator->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    auto h2 = allocator->Allocate(ResourceViewType::kTexture_UAV, DescriptorVisibility::kShaderVisible);
    auto h3 = allocator->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kCpuOnly);

    auto created = segmentCreated.find({ ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible });
    EXPECT_TRUE(created != segmentCreated.end());
    created = segmentCreated.find({ ResourceViewType::kTexture_UAV, DescriptorVisibility::kShaderVisible });
    EXPECT_TRUE(created != segmentCreated.end());
    created = segmentCreated.find({ ResourceViewType::kTexture_SRV, DescriptorVisibility::kCpuOnly });
    EXPECT_TRUE(created != segmentCreated.end());

    allocator->Release(h1);
    allocator->Release(h2);
    allocator->Release(h3);

    EXPECT_FALSE(h1.IsValid());
    EXPECT_FALSE(h2.IsValid());
    EXPECT_FALSE(h3.IsValid());
}

TEST_F(BaseDescriptorAllocatorBasicTest, CopyDescriptorBetweenSpaces)
{
    TestDescriptorHandle src_handle(allocator.get(), 5,
        ResourceViewType::kTexture_SRV, DescriptorVisibility::kCpuOnly);
    TestDescriptorHandle dst_handle(allocator.get(), 10,
        ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);

    EXPECT_CALL(*allocator, CopyDescriptor(::testing::_, ::testing::_)).Times(1);

    // Both handles should be valid before copy
    EXPECT_TRUE(src_handle.IsValid());
    EXPECT_TRUE(dst_handle.IsValid());

    allocator->CopyDescriptor(src_handle, dst_handle);

    // Both handles should remain valid after copy
    EXPECT_TRUE(src_handle.IsValid());
    EXPECT_TRUE(dst_handle.IsValid());

    src_handle.Invalidate();
    dst_handle.Invalidate();
}
