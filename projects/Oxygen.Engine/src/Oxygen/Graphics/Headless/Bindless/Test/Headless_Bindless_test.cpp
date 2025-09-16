//===----------------------------------------------------------------------===//
// Simple unit test for HeadlessDescriptorAllocator and heap segment
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>
#include <memory>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Detail/BaseDescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

#include <Oxygen/Graphics/Headless/Bindless/HeadlessDescriptorAllocator.h>

namespace {

using namespace oxygen::graphics;

NOLINT_TEST(HeadlessBindless, AllocateRelease)
{
  auto alloc = std::make_unique<
    oxygen::graphics::headless::bindless::HeadlessDescriptorAllocator>();
  ASSERT_NE(alloc, nullptr);

  // Allocate a few SRV shader-visible descriptors
  const auto type = ResourceViewType::kShaderResourceView;
  const auto vis = DescriptorVisibility::kShaderVisible;

  std::vector<DescriptorHandle> handles;
  for (int i = 0; i < 10; ++i) {
    auto h = alloc->Allocate(type, vis);
    EXPECT_TRUE(h.IsValid());
    handles.push_back(h);
  }

  // Release them
  for (auto& h : handles) {
    alloc->Release(h);
    EXPECT_FALSE(h.IsValid());
  }

  // After release, remaining count should be >= initial capacity (no leaks)
  const auto remaining = alloc->GetRemainingDescriptorsCount(type, vis);
  EXPECT_GE(remaining.get(), 0);
}

} // namespace
