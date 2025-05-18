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
using oxygen::graphics::detail::DescriptorHeapSegment;
using oxygen::graphics::DefaultDescriptorAllocationStrategy;
using oxygen::graphics::detail::BaseDescriptorAllocatorConfig;
using IndexT = DescriptorHandle::IndexT;

using oxygen::graphics::bindless::testing::BaseDescriptorAllocatorTest;
using oxygen::graphics::bindless::testing::MockDescriptorHeapSegment;
using oxygen::graphics::bindless::testing::TestDescriptorHandle;
using oxygen::graphics::bindless::testing::MockDescriptorAllocator;

// -------------------- Test Fixture --------------------
class BaseDescriptorAllocatorBasicTest : public BaseDescriptorAllocatorTest {
};

// -------------------- Basic Allocation Tests --------------------

// Tests that the allocator creates a new heap segment when allocating from an
// empty heap (i.e., first allocation).
NOLINT_TEST_F(BaseDescriptorAllocatorBasicTest, AllocatesFromEmptyHeapCreatesSegment)
{
    bool called = false;
    allocator_->segment_factory_ = [&called](auto, auto) -> std::unique_ptr<DescriptorHeapSegment> {
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
    auto handle = allocator_->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);

    // Verify: Handle should be valid
    EXPECT_TRUE(handle.IsValid());
    EXPECT_EQ(handle.GetIndex(), 0);

    // Explicitly release the handle to ensure the Release method gets called
    allocator_->Release(handle);
    EXPECT_FALSE(handle.IsValid());
}

NOLINT_TEST_F(BaseDescriptorAllocatorBasicTest, AllocatesFromNonEmptyHeapNoNewSegment)
{
    // Tests that subsequent allocations reuse the existing heap segment
    // rather than creating new ones

    int alloc_count = 0;
    allocator_->segment_factory_ = [this, &alloc_count](auto, auto) {
        auto segment = std::make_unique<::testing::NiceMock<MockDescriptorHeapSegment>>();
        EXPECT_CALL(*segment, Allocate())
            .WillOnce([&] { alloc_count++; return 0; })
            .WillOnce([&] { alloc_count++; return 1; });
        EXPECT_CALL(*segment, GetAvailableCount()).WillRepeatedly(::testing::Return(1));
        EXPECT_CALL(*segment, Release(0)).WillOnce(::testing::Return(true));
        EXPECT_CALL(*segment, Release(1)).WillOnce(::testing::Return(true));
        EXPECT_CALL(*segment, GetViewType()).WillRepeatedly(::testing::Return(ResourceViewType::kTexture_SRV));
        EXPECT_CALL(*segment, GetVisibility()).WillRepeatedly(::testing::Return(DescriptorVisibility::kShaderVisible));
        EXPECT_CALL(*segment, GetBaseIndex()).WillRepeatedly(::testing::Return(0));
        EXPECT_CALL(*segment, GetCapacity()).WillRepeatedly(::testing::Return(2));
        return std::move(segment);
    };

    // Action: Perform two allocations from the same heap
    const auto h1 = allocator_->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    const auto h2 = allocator_->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);

    // Verify: Both allocations were made and handles are valid
    EXPECT_EQ(alloc_count, 2);
    EXPECT_TRUE(h1.IsValid());
    EXPECT_TRUE(h2.IsValid());
}

NOLINT_TEST_F(BaseDescriptorAllocatorBasicTest, ReleaseMakesDescriptorAvailable)
{
    allocator_->segment_factory_ = [this](auto, auto) {
        auto segment = std::make_unique<::testing::NiceMock<MockDescriptorHeapSegment>>();
        EXPECT_CALL(*segment, Allocate()).WillOnce(::testing::Return(0));
        EXPECT_CALL(*segment, Release(0)).WillOnce(::testing::Return(true));
        EXPECT_CALL(*segment, GetAvailableCount()).WillRepeatedly(::testing::Return(1));
        EXPECT_CALL(*segment, GetViewType()).WillRepeatedly(::testing::Return(ResourceViewType::kTexture_SRV));
        EXPECT_CALL(*segment, GetVisibility()).WillRepeatedly(::testing::Return(DescriptorVisibility::kShaderVisible));
        EXPECT_CALL(*segment, GetBaseIndex()).WillRepeatedly(::testing::Return(0));
        EXPECT_CALL(*segment, GetCapacity()).WillRepeatedly(::testing::Return(1));
        return std::move(segment);
    };

    auto handle = allocator_->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    EXPECT_TRUE(handle.IsValid());
    allocator_->Release(handle);
    EXPECT_FALSE(handle.IsValid());
}

NOLINT_TEST_F(BaseDescriptorAllocatorBasicTest, ReleasingInvalidHandleIsNoOp)
{
    DescriptorHandle invalid;
    EXPECT_NO_THROW(allocator_->Release(invalid));
}

NOLINT_TEST_F(BaseDescriptorAllocatorBasicTest, GetRemainingDescriptorsReturnsCorrectCount)
{
    constexpr DescriptorHeapSegment::IndexT kAvailableCount = 42;
    allocator_->segment_factory_ = [this](auto, auto) {
        auto segment = std::make_unique<::testing::NiceMock<MockDescriptorHeapSegment>>();
        EXPECT_CALL(*segment, GetAvailableCount()).WillRepeatedly(::testing::Return(kAvailableCount));
        EXPECT_CALL(*segment, GetViewType()).WillRepeatedly(::testing::Return(ResourceViewType::kTexture_SRV));
        EXPECT_CALL(*segment, GetVisibility()).WillRepeatedly(::testing::Return(DescriptorVisibility::kShaderVisible));
        EXPECT_CALL(*segment, GetBaseIndex()).WillRepeatedly(::testing::Return(0));
        EXPECT_CALL(*segment, GetCapacity()).WillRepeatedly(::testing::Return(1));
        EXPECT_CALL(*segment, Release(0)).WillOnce(::testing::Return(true));
        EXPECT_CALL(*segment, Allocate()).WillOnce(::testing::Return(0));
        return std::move(segment);
    };

    auto handle = allocator_->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);

    const auto remaining_count = allocator_->GetRemainingDescriptorsCount(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    EXPECT_EQ(remaining_count, kAvailableCount);

    allocator_->Release(handle);
    EXPECT_FALSE(handle.IsValid());
}

NOLINT_TEST_F(BaseDescriptorAllocatorBasicTest, HandleRecyclingReusesSameIndex)
{
    allocator_->segment_factory_ = [this](auto, auto) {
        auto segment = std::make_unique<::testing::NiceMock<MockDescriptorHeapSegment>>();
        EXPECT_CALL(*segment, Allocate()).WillRepeatedly(::testing::Return(42));
        EXPECT_CALL(*segment, Release(42)).WillRepeatedly(::testing::Return(true));
        EXPECT_CALL(*segment, GetAvailableCount()).WillRepeatedly(::testing::Return(1));
        EXPECT_CALL(*segment, GetViewType()).WillRepeatedly(::testing::Return(ResourceViewType::kTexture_SRV));
        EXPECT_CALL(*segment, GetVisibility()).WillRepeatedly(::testing::Return(DescriptorVisibility::kShaderVisible));
        EXPECT_CALL(*segment, GetBaseIndex()).WillRepeatedly(::testing::Return(0));
        EXPECT_CALL(*segment, GetCapacity()).WillRepeatedly(::testing::Return(43));
        return std::move(segment);
    };

    auto handle1 = allocator_->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    const DescriptorHeapSegment::IndexT index1 = handle1.GetIndex();
    allocator_->Release(handle1);
    auto handle2 = allocator_->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    const DescriptorHeapSegment::IndexT index2 = handle2.GetIndex();

    EXPECT_EQ(index1, index2);

    allocator_->Release(handle2);
    EXPECT_FALSE(handle2.IsValid());
}

NOLINT_TEST_F(BaseDescriptorAllocatorBasicTest, HandlesMultipleTypesAndVisibilities)
{
    std::map<std::pair<ResourceViewType, DescriptorVisibility>, bool> segmentCreated;

    allocator_->segment_factory_ = [&segmentCreated](auto type, auto vis) {
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

    auto h1 = allocator_->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    auto h2 = allocator_->Allocate(ResourceViewType::kTexture_UAV, DescriptorVisibility::kShaderVisible);
    auto h3 = allocator_->Allocate(ResourceViewType::kTexture_SRV, DescriptorVisibility::kCpuOnly);

    auto created = segmentCreated.find({ ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible });
    EXPECT_TRUE(created != segmentCreated.end());
    created = segmentCreated.find({ ResourceViewType::kTexture_UAV, DescriptorVisibility::kShaderVisible });
    EXPECT_TRUE(created != segmentCreated.end());
    created = segmentCreated.find({ ResourceViewType::kTexture_SRV, DescriptorVisibility::kCpuOnly });
    EXPECT_TRUE(created != segmentCreated.end());

    allocator_->Release(h1);
    allocator_->Release(h2);
    allocator_->Release(h3);

    EXPECT_FALSE(h1.IsValid());
    EXPECT_FALSE(h2.IsValid());
    EXPECT_FALSE(h3.IsValid());
}

NOLINT_TEST_F(BaseDescriptorAllocatorBasicTest, FirstSegmentUsesStrategyBaseIndex)
{
    // Set up a custom strategy
    auto strategy = std::make_shared<DefaultDescriptorAllocationStrategy>();
    const ResourceViewType view_type = ResourceViewType::kTexture_SRV;
    const DescriptorVisibility visibility = DescriptorVisibility::kShaderVisible;
    const DescriptorHeapSegment::IndexT expected_base_index = static_cast<DescriptorHeapSegment::IndexT>(strategy->GetHeapBaseIndex(view_type, visibility));

    // Local mock allocator that checks the base index passed to CreateHeapSegment
    class LocalMockAllocator : public oxygen::graphics::detail::BaseDescriptorAllocator {
    public:
        using Base = oxygen::graphics::detail::BaseDescriptorAllocator;
        LocalMockAllocator(const BaseDescriptorAllocatorConfig& cfg, DescriptorHeapSegment::IndexT expected)
            : Base(cfg), expected_base_index_(expected), checked_(false) {}
        // Implement all pure virtuals from DescriptorAllocator
        void CopyDescriptor(const DescriptorHandle&, const DescriptorHandle&) override { throw std::logic_error("Not used in this test"); }
        oxygen::graphics::NativeObject GetNativeHandle(const DescriptorHandle&) const override { return {}; }
        void PrepareForRendering(const oxygen::graphics::NativeObject&) override {}
    protected:
        std::unique_ptr<DescriptorHeapSegment> CreateHeapSegment(
            uint32_t /*capacity*/,
            uint32_t base_index,
            ResourceViewType vt,
            DescriptorVisibility vis) override {
            EXPECT_EQ(static_cast<DescriptorHeapSegment::IndexT>(base_index), expected_base_index_);
            checked_ = true;
            auto seg = std::make_unique<::testing::NiceMock<MockDescriptorHeapSegment>>();
            EXPECT_CALL(*seg, Allocate()).WillOnce(::testing::Return(base_index));
            EXPECT_CALL(*seg, Release(base_index)).WillOnce(::testing::Return(true));
            EXPECT_CALL(*seg, GetAvailableCount()).WillRepeatedly(::testing::Return(1));
            EXPECT_CALL(*seg, GetViewType()).WillRepeatedly(::testing::Return(vt));
            EXPECT_CALL(*seg, GetVisibility()).WillRepeatedly(::testing::Return(vis));
            EXPECT_CALL(*seg, GetBaseIndex()).WillRepeatedly(::testing::Return(base_index));
            EXPECT_CALL(*seg, GetCapacity()).WillRepeatedly(::testing::Return(1));
            return seg;
        }
    public:
        bool checked() const { return checked_; }
    private:
        DescriptorHeapSegment::IndexT expected_base_index_;
        bool checked_;
    };

    // Use the local mock allocator
    auto allocator = std::make_unique<LocalMockAllocator>(BaseDescriptorAllocatorConfig{ .heap_strategy = strategy }, expected_base_index);

    // Allocate a descriptor and verify the base index was used
    auto handle = allocator->Allocate(view_type, visibility);
    EXPECT_TRUE(handle.IsValid());
    EXPECT_EQ(handle.GetIndex(), expected_base_index);
    EXPECT_TRUE(allocator->checked());
    allocator->Release(handle);
    EXPECT_FALSE(handle.IsValid());
}

TEST_F(BaseDescriptorAllocatorBasicTest, CopyDescriptorBetweenSpaces)
{
    TestDescriptorHandle src_handle(allocator_.get(), 5,
        ResourceViewType::kTexture_SRV, DescriptorVisibility::kCpuOnly);
    TestDescriptorHandle dst_handle(allocator_.get(), 10,
        ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);

    EXPECT_CALL(*allocator_, CopyDescriptor(::testing::_, ::testing::_)).Times(1);

    // Both handles should be valid before copy
    EXPECT_TRUE(src_handle.IsValid());
    EXPECT_TRUE(dst_handle.IsValid());

    allocator_->CopyDescriptor(src_handle, dst_handle);

    // Both handles should remain valid after copy
    EXPECT_TRUE(src_handle.IsValid());
    EXPECT_TRUE(dst_handle.IsValid());

    src_handle.Invalidate();
    dst_handle.Invalidate();
}
