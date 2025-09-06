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

#include "./Mocks/MockDescriptorAllocator.h"
#include "./Mocks/MockDescriptorSegment.h"
#include "./Mocks/TestDescriptorHandle.h"

using oxygen::kInvalidBindlessHandle;
using oxygen::graphics::DescriptorHandle;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::bindless::testing::MockDescriptorAllocator;
using oxygen::graphics::bindless::testing::MockDescriptorSegment;
using oxygen::graphics::bindless::testing::TestDescriptorHandle;
using oxygen::graphics::detail::DescriptorSegment;
namespace b = oxygen::bindless;

namespace {

// Base fixture for descriptor handle tests
class UnitTests : public ::testing::Test {
protected:
  MockDescriptorAllocator allocator_ {};

  static constexpr auto view_type_ = ResourceViewType::kTexture_SRV;
  static constexpr auto visibility_ = DescriptorVisibility::kShaderVisible;

  // Helper to create a segment for testing with configurable parameters
  static auto CreateSegment(const b::Handle::UnderlyingType base_index = 0)
    -> std::unique_ptr<MockDescriptorSegment>
  {
    auto segment
      = std::make_unique<::testing::NiceMock<MockDescriptorSegment>>();

    ON_CALL(*segment, GetViewType())
      .WillByDefault(::testing::Return(view_type_));
    ON_CALL(*segment, GetVisibility())
      .WillByDefault(::testing::Return(visibility_));
    ON_CALL(*segment, GetCapacity())
      .WillByDefault(::testing::Return(b::Capacity { 100 }));
    ON_CALL(*segment, GetAvailableCount())
      .WillByDefault(::testing::Return(b::Count { 100 }));
    ON_CALL(*segment, GetBaseIndex())
      .WillByDefault(::testing::Return(b::Handle { base_index }));

    // Return fixed index (for simple test cases)
    ON_CALL(*segment, Allocate())
      .WillByDefault(::testing::Return(b::Handle { base_index }));

    // Release should always succeed in tests
    ON_CALL(*segment, Release(::testing::_))
      .WillByDefault(::testing::Return(true));

    return segment;
  }

  // ReSharper disable once CppMemberFunctionMayBeConst
  void SetSegmentFactory(MockDescriptorAllocator::SegmentFactory&& factory)
  {
    allocator_.segment_factory_ = std::move(factory);
  }
};

// Test Group: Construction and Validity
NOLINT_TEST_F(UnitTests, DefaultConstructedHandleIsInvalid)
{
  const DescriptorHandle handle;
  EXPECT_FALSE(handle.IsValid());
  EXPECT_EQ(handle.GetBindlessHandle(), kInvalidBindlessHandle);
}

NOLINT_TEST_F(UnitTests, InvalidateDoesNotRelease)
{
  auto mock_segment = CreateSegment(42);

  EXPECT_CALL(*mock_segment, Allocate()).Times(1);
  EXPECT_CALL(*mock_segment, Release(b::Handle { 42 })).Times(0);

  SetSegmentFactory(
    [&mock_segment](auto, auto) { return std::move(mock_segment); });

  auto handle = allocator_.Allocate(
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);

  handle.Invalidate();

  EXPECT_FALSE(handle.IsValid());
  EXPECT_EQ(handle.GetBindlessHandle(), kInvalidBindlessHandle);

  // No release should be called here, as we just invalidated the handle
}

NOLINT_TEST_F(UnitTests, ReleasingInvalidHandleIsNoop)
{
  // Invalid handles shouldn't trigger any segment operations
  SetSegmentFactory([](auto, auto) {
    ADD_FAILURE() << "Invalid handle should not try to create segments";
    return std::unique_ptr<DescriptorSegment> {};
  });

  DescriptorHandle handle;
  handle.Release();

  EXPECT_FALSE(handle.IsValid());
}

NOLINT_TEST_F(UnitTests, ExplicitReleaseInvalidatesHandle)
{
  auto mock_segment = CreateSegment(42);

  EXPECT_CALL(*mock_segment, Allocate()).Times(1);
  EXPECT_CALL(*mock_segment, Release(b::Handle { 42 })).Times(1);

  SetSegmentFactory(
    [&mock_segment](auto, auto) { return std::move(mock_segment); });

  auto handle = allocator_.Allocate(
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);

  EXPECT_TRUE(handle.IsValid());

  handle.Release();

  EXPECT_FALSE(handle.IsValid());
  EXPECT_EQ(handle.GetBindlessHandle(), kInvalidBindlessHandle);
}

NOLINT_TEST_F(UnitTests, DestructorReleasesHandle)
{
  auto mock_segment = CreateSegment(42);

  EXPECT_CALL(*mock_segment, Allocate()).Times(1);
  EXPECT_CALL(*mock_segment, Release(b::Handle { 42 })).Times(1);

  SetSegmentFactory(
    [&mock_segment](auto, auto) { return std::move(mock_segment); });

  {
    const auto handle = allocator_.Allocate(
      ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    EXPECT_TRUE(handle.IsValid());
    EXPECT_EQ(handle.GetBindlessHandle(), b::Handle { 42U });
  } // Handle goes out of scope, destructor should release
}

NOLINT_TEST_F(UnitTests, MoveConstructorDestinationEquivalentToSource)
{
  TestDescriptorHandle src(&allocator_, b::Handle { 77 },
    ResourceViewType::kSampler, DescriptorVisibility::kCpuOnly);

  DescriptorHandle dst(std::move(src));

  // Destination should have the same properties as the original source
  EXPECT_TRUE(dst.IsValid());
  EXPECT_EQ(dst.GetBindlessHandle(), b::Handle { 77U });
  EXPECT_EQ(dst.GetViewType(), ResourceViewType::kSampler);
  EXPECT_EQ(dst.GetVisibility(), DescriptorVisibility::kCpuOnly);

  // Invalidate to avoid release in destructor, as we have not allocated the
  // handle via the allocator.
  dst.Invalidate();
}

NOLINT_TEST_F(UnitTests, MoveConstructorInvalidatesSource)
{
  TestDescriptorHandle src(&allocator_, b::Handle { 77 },
    ResourceViewType::kSampler, DescriptorVisibility::kCpuOnly);

  auto dst(std::move(src));

  // Source should be invalidated
  EXPECT_FALSE(src.IsValid()); // NOLINT(bugprone-use-after-move) - testing
  EXPECT_EQ(src.GetBindlessHandle(), kInvalidBindlessHandle);

  // Invalidate to avoid release in destructor, as we have not allocated the
  // handle via the allocator.
  dst.Invalidate();
}

NOLINT_TEST_F(UnitTests, MoveAssignmentDestinationEquivalentToSource)
{
  auto* allocator_ptr = &allocator_;
  TestDescriptorHandle src(allocator_ptr, b::Handle { 33 },
    ResourceViewType::kTexture_UAV, DescriptorVisibility::kShaderVisible);

  auto dst = std::move(src);

  EXPECT_TRUE(dst.IsValid());
  EXPECT_EQ(dst.GetBindlessHandle(), b::Handle { 33U });
  EXPECT_EQ(dst.GetViewType(), ResourceViewType::kTexture_UAV);
  EXPECT_EQ(dst.GetVisibility(), DescriptorVisibility::kShaderVisible);

  // Invalidate to avoid release in destructor, as we have not allocated the
  // handle via the allocator.
  dst.Invalidate();
}

NOLINT_TEST_F(UnitTests, MoveAssignmentReleasesDestinationBeforeAssign)
{
  // Set up a mock segment to verify release is called
  auto mock_segment = CreateSegment(55);

  // The handle will own index 55, and we expect Release(55) to be called when
  // dst is overwritten.
  EXPECT_CALL(*mock_segment, Release(b::Handle { 55 }))
    .Times(1); // Release the moved handle upon destruction
  EXPECT_CALL(*mock_segment, Release(b::Handle { 99 }))
    .Times(1); // Release the old handle

  // Assign the allocator segment factory to always return our mock
  SetSegmentFactory(
    [&mock_segment](auto, auto) { return std::move(mock_segment); });

  // Allocate a handle so that it owns index 55
  auto dst = allocator_.Allocate(
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);

  // Hand made source so we do not need to manage its release
  TestDescriptorHandle src(&allocator_, b::Handle { 99 },
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);

  // Now move-assign a new handle to dst, which should release the old one
  dst = std::move(src);

  // dst should now have src properties
  EXPECT_TRUE(dst.IsValid());
  EXPECT_EQ(dst.GetBindlessHandle(), b::Handle { 99U });
  EXPECT_EQ(dst.GetViewType(), ResourceViewType::kTexture_SRV);
  EXPECT_EQ(dst.GetVisibility(), DescriptorVisibility::kShaderVisible);
}

NOLINT_TEST_F(UnitTests, MoveAssignmentInvalidatesSource)
{
  TestDescriptorHandle src(&allocator_, b::Handle { 77 },
    ResourceViewType::kSampler, DescriptorVisibility::kCpuOnly);
  EXPECT_TRUE(src.IsValid());

  auto dst = std::move(src);

  // Source should be invalidated
  EXPECT_FALSE(src.IsValid()); // NOLINT(bugprone-use-after-move) - testing

  // Invalidate to avoid release in destructor, as we have not allocated the
  // handle via the allocator.
  dst.Invalidate();
}

} // anonymous namespace
