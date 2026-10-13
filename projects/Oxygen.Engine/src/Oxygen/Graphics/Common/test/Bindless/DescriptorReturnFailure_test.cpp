//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <new>
#include <utility>

#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Graphics/Common/Detail/FixedDescriptorSegment.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Graphics/Common/test/Bindless/Mocks/MockDescriptorAllocator.h>
#include <Oxygen/Testing/GTest.h>

#if defined(_MSC_VER) && defined(_DEBUG)
#  include <Oxygen/Graphics/Common/test/HeapAllocationFailure.h>

namespace {
using oxygen::graphics::testing::HeapAllocationFailure;

NOLINT_TEST(
  DescriptorReturnFailure, RawAndBindlessCleanupAndPreparedCommitDoNotAllocate)
{
  using namespace oxygen::graphics;
  ::testing::NiceMock<bindless::testing::MockDescriptorAllocator> allocator;
  allocator.ext_segment_factory_
    = [](auto capacity, auto base, auto view,
        auto visibility) -> std::unique_ptr<detail::DescriptorSegment> {
    return std::make_unique<detail::FixedDescriptorSegment>(
      capacity, base, view, visibility);
  };
  auto raw = allocator.RealAllocateRawForMock(
    ResourceViewType::kTexture_DSV, DescriptorVisibility::kCpuOnly);
  auto bindless
    = allocator.AllocateBindless(oxygen::bindless::generated::kTexturesDomain,
      ResourceViewType::kTexture_SRV);
  detail::DeferredReclaimer reclaimer;
  bool ran = false;
  auto action = reclaimer.PrepareDeferredAction([&] -> void { ran = true; });
  const auto rejected_before = HeapAllocationFailure::RejectedCount();
  {
    HeapAllocationFailure reject;
    raw.Release();
    bindless.Release();
    reclaimer.CommitDeferredAction(std::move(action));
  }
  EXPECT_EQ(HeapAllocationFailure::RejectedCount(), rejected_before);
  EXPECT_FALSE(raw.IsValid());
  EXPECT_FALSE(bindless.IsValid());
  reclaimer.OnBeginFrame(oxygen::frame::Slot { 0 });
  EXPECT_TRUE(ran);
}

NOLINT_TEST(
  DescriptorReturnFailure, ReclaimerNodePreparationFailsBeforePublication)
{
  oxygen::graphics::detail::DeferredReclaimer reclaimer;
  bool failed = false;
  bool ran = false;
  {
    HeapAllocationFailure reject;
    try {
      auto action
        = reclaimer.PrepareDeferredAction([&] -> void { ran = true; });
    } catch (const std::bad_alloc&) {
      failed = true;
    }
  }
  EXPECT_TRUE(failed);
  reclaimer.OnBeginFrame(oxygen::frame::Slot { 0 });
  EXPECT_FALSE(ran);
}
} // namespace
#endif
