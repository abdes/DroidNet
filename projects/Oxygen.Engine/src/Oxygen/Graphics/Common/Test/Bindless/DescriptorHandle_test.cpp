//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/Detail/BaseDescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Test/Bindless/Mocks/MockDescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Test/Bindless/Mocks/MockDescriptorHeapSegment.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

using oxygen::graphics::DescriptorHandle;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::detail::BaseDescriptorAllocatorConfig;
using oxygen::graphics::detail::DescriptorHeapSegment;
using oxygen::graphics::testing::MockDescriptorAllocator;
using oxygen::graphics::testing::MockDescriptorHeapSegment;

namespace {

// Base fixture for descriptor handle tests
class DescriptorHandleTest : public ::testing::Test {
protected:
    MockDescriptorAllocator allocator_ { BaseDescriptorAllocatorConfig {} };

    static constexpr auto view_type_ = ResourceViewType::kTexture_SRV;
    static constexpr auto visibility_ = DescriptorVisibility::kShaderVisible;

    // Helper to create a segment for testing with configurable parameters
    static auto CreateSegment(const uint32_t base_index = 0) -> std::unique_ptr<MockDescriptorHeapSegment>
    {
        auto segment = std::make_unique<::testing::NiceMock<MockDescriptorHeapSegment>>();

        ON_CALL(*segment, GetViewType()).WillByDefault(::testing::Return(view_type_));
        ON_CALL(*segment, GetVisibility()).WillByDefault(::testing::Return(visibility_));
        ON_CALL(*segment, GetCapacity()).WillByDefault(::testing::Return(100));
        ON_CALL(*segment, GetAvailableCount()).WillByDefault(::testing::Return(100));
        ON_CALL(*segment, GetBaseIndex()).WillByDefault(::testing::Return(base_index));

        // Return fixed index (for simple test cases)
        ON_CALL(*segment, Allocate()).WillByDefault(::testing::Return(base_index));

        // Release should always succeed in tests
        ON_CALL(*segment, Release(::testing::_)).WillByDefault(::testing::Return(true));

        return segment;
    }

    // ReSharper disable once CppMemberFunctionMayBeConst
    void SetSegmentFactory(MockDescriptorAllocator::SegmentFactory&& factory)
    {
        allocator_.segmentFactory = std::move(factory);
    }
};

// Test Group: Construction and Validity
TEST_F(DescriptorHandleTest, DefaultConstructedHandleIsInvalid)
{
    const DescriptorHandle handle;
    EXPECT_FALSE(handle.IsValid());
    EXPECT_EQ(handle.GetIndex(), DescriptorHandle::kInvalidIndex);
}

// Test Group: Construction and Validity
TEST_F(DescriptorHandleTest, InvalidateDoesNotRelease)
{
    auto mock_segment = CreateSegment(42);

    EXPECT_CALL(*mock_segment, Allocate()).Times(1);
    EXPECT_CALL(*mock_segment, Release(42)).Times(0);

    SetSegmentFactory([&mock_segment](auto, auto) {
        return std::move(mock_segment);
    });

    auto handle = allocator_.Allocate(
        ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);

    handle.Invalidate();

    EXPECT_FALSE(handle.IsValid());
    EXPECT_EQ(handle.GetIndex(), DescriptorHandle::kInvalidIndex);

    // No release should be called here, as we just invalidated the handle
}

TEST_F(DescriptorHandleTest, ReleasingInvalidHandleIsNoop)
{
    // Invalid handles shouldn't trigger any segment operations
    SetSegmentFactory([](auto, auto) {
        ADD_FAILURE() << "Invalid handle should not try to create segments";
        return std::unique_ptr<DescriptorHeapSegment> {};
    });

    DescriptorHandle handle;
    handle.Release();

    EXPECT_FALSE(handle.IsValid());
}

TEST_F(DescriptorHandleTest, ExplicitReleaseInvalidatesHandle)
{
    auto mock_segment = CreateSegment(42);

    EXPECT_CALL(*mock_segment, Allocate()).Times(1);
    EXPECT_CALL(*mock_segment, Release(42)).Times(1);

    SetSegmentFactory([&mock_segment](auto, auto) {
        return std::move(mock_segment);
    });

    auto handle = allocator_.Allocate(
        ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);

    EXPECT_TRUE(handle.IsValid());

    handle.Release();

    EXPECT_FALSE(handle.IsValid());
    EXPECT_EQ(handle.GetIndex(), DescriptorHandle::kInvalidIndex);
}

TEST_F(DescriptorHandleTest, DestructorReleasesHandle)
{
    auto mock_segment = CreateSegment(42);

    EXPECT_CALL(*mock_segment, Allocate()).Times(1);
    EXPECT_CALL(*mock_segment, Release(42)).Times(1);

    SetSegmentFactory([&mock_segment](auto, auto) {
        return std::move(mock_segment);
    });

    {
        const auto handle = allocator_.Allocate(
            ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
        EXPECT_TRUE(handle.IsValid());
        EXPECT_EQ(handle.GetIndex(), 42U);
    } // Handle goes out of scope, destructor should release
}

TEST_F(DescriptorHandleTest, MoveConstructorDestinationEquivalentToSource)
{
    DescriptorHandle src(&allocator_, 77,
        ResourceViewType::kSampler, DescriptorVisibility::kCpuOnly);

    DescriptorHandle dst(std::move(src));

    // Destination should have the same properties as the original source
    EXPECT_TRUE(dst.IsValid());
    EXPECT_EQ(dst.GetIndex(), 77U);
    EXPECT_EQ(dst.GetViewType(), ResourceViewType::kSampler);
    EXPECT_EQ(dst.GetVisibility(), DescriptorVisibility::kCpuOnly);

    // Invalidate to avoid release in destructor, as we have not allocated the
    // handle via the allocator.
    dst.Invalidate();
}

TEST_F(DescriptorHandleTest, MoveConstructorInvalidatesSource)
{
    DescriptorHandle src(&allocator_, 77,
        ResourceViewType::kSampler, DescriptorVisibility::kCpuOnly);
    DescriptorHandle dst(std::move(src));

    // Source should be invalidated
    EXPECT_FALSE(src.IsValid()); // NOLINT(bugprone-use-after-move) - testing
    EXPECT_EQ(src.GetIndex(), DescriptorHandle::kInvalidIndex);

    // Invalidate to avoid release in destructor, as we have not allocated the
    // handle via the allocator.
    dst.Invalidate();
}

TEST_F(DescriptorHandleTest, MoveAssignmentDestinationEquivalentToSource)
{
    auto* allocator_ptr = &allocator_;
    DescriptorHandle src(allocator_ptr, 33,
        ResourceViewType::kTexture_UAV, DescriptorVisibility::kShaderVisible);

    DescriptorHandle dst = std::move(src);

    EXPECT_TRUE(dst.IsValid());
    EXPECT_EQ(dst.GetIndex(), 33U);
    EXPECT_EQ(dst.GetViewType(), ResourceViewType::kTexture_UAV);
    EXPECT_EQ(dst.GetVisibility(), DescriptorVisibility::kShaderVisible);

    // Invalidate to avoid release in destructor, as we have not allocated the
    // handle via the allocator.
    dst.Invalidate();
}

TEST_F(DescriptorHandleTest, MoveAssignmentReleasesDestinationBeforeAssign)
{
    // Set up a mock segment to verify release is called
    auto mock_segment = CreateSegment(55);

    // The handle will own index 55, and we expect Release(55) to be called when dst is overwritten.
    EXPECT_CALL(*mock_segment, Release(55)).Times(1); // Release the moved handle upon destruction
    EXPECT_CALL(*mock_segment, Release(99)).Times(1); // Release the old handle

    // Assign the allocator segment factory to always return our mock
    SetSegmentFactory([&mock_segment](auto, auto) {
        return std::move(mock_segment);
    });

    // Allocate a handle so that it owns index 55
    DescriptorHandle dst = allocator_.Allocate(
        ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);

    // Hand made source so we do not need to manage its release
    DescriptorHandle src(&allocator_, 99,
        ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);

    // Now move-assign a new handle to dst, which should release the old one
    dst = std::move(src);

    // dst should now have src properties
    EXPECT_TRUE(dst.IsValid());
    EXPECT_EQ(dst.GetIndex(), 99U);
    EXPECT_EQ(dst.GetViewType(), ResourceViewType::kTexture_SRV);
    EXPECT_EQ(dst.GetVisibility(), DescriptorVisibility::kShaderVisible);
}

TEST_F(DescriptorHandleTest, MoveAssignmentInvalidatesSource)
{
    DescriptorHandle src(&allocator_, 77,
        ResourceViewType::kSampler, DescriptorVisibility::kCpuOnly);
    EXPECT_TRUE(src.IsValid());

    DescriptorHandle dst = std::move(src);

    // Source should be invalidated
    EXPECT_FALSE(src.IsValid()); // NOLINT(bugprone-use-after-move) - testing

    // Invalidate to avoid release in destructor, as we have not allocated the
    // handle via the allocator.
    dst.Invalidate();
}

} // anonymous namespace
