//===----------------------------------------------------------------------===//
// Simple unit test for HeadlessDescriptorAllocator and heap segment
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Detail/BaseDescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

#include <Oxygen/Graphics/Headless/Bindless/DescriptorAllocator.h>

namespace {

using namespace oxygen::graphics;

NOLINT_TEST(HeadlessAllocator, AllocateRelease)
{
  // Arrange
  auto alloc = std::make_unique<headless::bindless::DescriptorAllocator>();
  ASSERT_TRUE(alloc);

  constexpr auto type = ResourceViewType::kTexture_SRV;
  constexpr auto vis = DescriptorVisibility::kShaderVisible;

  // Verify initial allocated count is zero
  EXPECT_EQ(alloc->GetAllocatedDescriptorsCount(type, vis).get(), 0);

  // Act: allocate handles
  std::vector<DescriptorHandle> handles;
  for (int i = 0; i < 10; ++i) {
    auto h = alloc->Allocate(type, vis);
    EXPECT_TRUE(h.IsValid());
    handles.push_back(std::move(h));
  }

  // Expect allocated count updated
  EXPECT_EQ(alloc->GetAllocatedDescriptorsCount(type, vis).get(), 10);

  // Assert: release and validate
  for (auto& h : handles) {
    alloc->Release(h);
    EXPECT_FALSE(h.IsValid());
  }

  // Expect allocated count returns to zero
  EXPECT_EQ(alloc->GetAllocatedDescriptorsCount(type, vis).get(), 0);

  const auto remaining = alloc->GetRemainingDescriptorsCount(type, vis);
  EXPECT_GE(remaining.get(), 0);
}

} // namespace
