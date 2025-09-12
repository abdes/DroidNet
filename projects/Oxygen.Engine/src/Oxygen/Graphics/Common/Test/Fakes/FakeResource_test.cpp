//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/Test/Fakes/FakeResource.h>

using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::NativeView;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::testing::FakeResource;
using oxygen::graphics::testing::TestViewDesc;

namespace {

NOLINT_TEST(FakeResource_basic_test, DefaultReturnsValidView)
{
  // Arrange
  auto fake = FakeResource {}.WithDefaultView();
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 1ULL,
  };

  // Act
  const oxygen::graphics::DescriptorHandle
    dh; // invalid-but-typed handle for test
  const NativeView view = fake.GetNativeView(dh, desc);

  // Assert
  EXPECT_TRUE(view.get().IsValid());
}

NOLINT_TEST(FakeResource_basic_test, InvalidViewReturnsInvalid)
{
  // Arrange
  auto fake = FakeResource {}.WithInvalidView();
  constexpr TestViewDesc desc { .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 2ULL };

  // Act
  const oxygen::graphics::DescriptorHandle
    dh; // invalid-but-typed handle for test
  const NativeView view = fake.GetNativeView(dh, desc);

  // Assert
  EXPECT_FALSE(view.get().IsValid());
}

NOLINT_TEST(FakeResource_basic_test, ThrowingViewThrowsForConfiguredId)
{
  // Arrange
  auto fake = FakeResource {}.WithThrowingView(3ULL);
  TestViewDesc good_desc { .id = 1ULL };
  TestViewDesc bad_desc { .id = 3ULL };

  // Act / Assert
  oxygen::graphics::DescriptorHandle dh; // invalid-but-typed handle for test
  EXPECT_NO_THROW((void)fake.GetNativeView(dh, good_desc));
  EXPECT_THROW((void)fake.GetNativeView(dh, bad_desc), std::runtime_error);
}

NOLINT_TEST(FakeResource_basic_test, CustomBehaviorIsInvoked)
{
  // Arrange
  int calls = 0;
  auto fake = FakeResource {}.WithViewBehavior(
    [&calls](const oxygen::graphics::DescriptorHandle&,
      const TestViewDesc& desc) -> NativeView {
      ++calls;
      // Return invalid view when id == 99 to check return path
      if (desc.id == 99ULL) {
        return {};
      }
      const uint64_t v = desc.id | 0x10000ULL;
      return { v, /*type id*/ 1 };
    });

  constexpr TestViewDesc a { .id = 7ULL };
  constexpr TestViewDesc b { .id = 99ULL };

  // Act
  const oxygen::graphics::DescriptorHandle
    dh; // invalid-but-typed handle for test
  const NativeView va = fake.GetNativeView(dh, a);
  const NativeView vb = fake.GetNativeView(dh, b);

  // Assert
  EXPECT_EQ(2, calls);
  EXPECT_TRUE(va.get().IsValid());
  EXPECT_FALSE(vb.get().IsValid());
}

} // namespace
