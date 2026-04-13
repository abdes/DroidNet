//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <memory>

#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Vortex/Internal/ViewConstantsManager.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>
#include <Oxygen/Vortex/Types/ViewConstants.h>

namespace {

using oxygen::ViewId;
using oxygen::frame::Slot;
using oxygen::graphics::Buffer;
using oxygen::vortex::ViewConstants;
using oxygen::vortex::internal::ViewConstantsManager;
using oxygen::vortex::testing::FakeGraphics;

class ViewConstantsManagerTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    graphics_ = std::make_shared<FakeGraphics>();
    manager_ = std::make_unique<ViewConstantsManager>(
      oxygen::observer_ptr { graphics_.get() },
      static_cast<std::uint32_t>(sizeof(ViewConstants::GpuData)));
  }

  [[nodiscard]] auto WriteSnapshot(ViewId view_id, Slot slot)
    -> std::shared_ptr<Buffer>
  {
    manager_->OnFrameStart(slot);

    ViewConstants constants {};
    const auto snapshot = constants.GetSnapshot();
    const auto info
      = manager_->WriteViewConstants(view_id, &snapshot, sizeof(snapshot));

    EXPECT_NE(info.buffer, nullptr);
    EXPECT_NE(info.mapped_ptr, nullptr);
    return info.buffer;
  }

  std::shared_ptr<FakeGraphics> graphics_ {};
  std::unique_ptr<ViewConstantsManager> manager_ {};
};

NOLINT_TEST_F(ViewConstantsManagerTest,
  RemoveViewReleasesTrackedBuffersAcrossSlotsAndPreservesOtherViews)
{
  const auto view_a = ViewId { 101U };
  const auto view_b = ViewId { 202U };

  const auto buffer_a_slot0 = WriteSnapshot(view_a, Slot { 0U });
  const auto buffer_b_slot0 = WriteSnapshot(view_b, Slot { 0U });
  const auto buffer_a_slot1 = WriteSnapshot(view_a, Slot { 1U });

  ASSERT_NE(buffer_a_slot0, nullptr);
  ASSERT_NE(buffer_b_slot0, nullptr);
  ASSERT_NE(buffer_a_slot1, nullptr);
  EXPECT_EQ(manager_->GetTrackedBufferCount(), 3U);

  manager_->RemoveView(view_a);

  EXPECT_EQ(manager_->GetTrackedBufferCount(), 1U);

  const auto buffer_b_slot0_again = WriteSnapshot(view_b, Slot { 0U });
  ASSERT_NE(buffer_b_slot0_again, nullptr);
  EXPECT_EQ(buffer_b_slot0_again.get(), buffer_b_slot0.get());
  EXPECT_EQ(manager_->GetTrackedBufferCount(), 1U);

  const auto buffer_a_slot0_again = WriteSnapshot(view_a, Slot { 0U });
  ASSERT_NE(buffer_a_slot0_again, nullptr);
  EXPECT_NE(buffer_a_slot0_again.get(), buffer_a_slot0.get());
  EXPECT_EQ(manager_->GetTrackedBufferCount(), 2U);
}

NOLINT_TEST_F(
  ViewConstantsManagerTest, RemoveViewIgnoresUnknownViewWithoutChangingState)
{
  const auto tracked_view = ViewId { 303U };
  const auto buffer = WriteSnapshot(tracked_view, Slot { 0U });
  ASSERT_NE(buffer, nullptr);
  ASSERT_EQ(manager_->GetTrackedBufferCount(), 1U);

  manager_->RemoveView(ViewId { 404U });

  EXPECT_EQ(manager_->GetTrackedBufferCount(), 1U);
  const auto buffer_again = WriteSnapshot(tracked_view, Slot { 0U });
  ASSERT_NE(buffer_again, nullptr);
  EXPECT_EQ(buffer_again.get(), buffer.get());
}

} // namespace
