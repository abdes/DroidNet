//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#include <array>
#include <cstring>
#include <memory>
#include <vector>

#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Lighting/Internal/DeferredLightConstantsPublisher.h>
#include <Oxygen/Vortex/Test/Fixtures/UploadCoordinatorTest.h>

namespace oxygen::vortex::testing {
namespace {
  using Publisher = lighting::internal::DeferredLightConstantsPublisher;

  class DeferredConstantCacheTest
    : public upload::testing::UploadCoordinatorTest {
  protected:
    void SetUp() override
    {
      UploadCoordinatorTest::SetUp();
      SetStagingProvider(
        Uploader().CreateRingBufferStaging(frame::SlotCount { 3U }, 256U));
      publisher = std::make_unique<Publisher>(
        Gfx().weak_from_this(), Staging(), nullptr);
    }
    void NextFrame()
    {
      if (sequence != 0U) {
        Gfx().EndFrame(frame::SequenceNumber { sequence }, frame::Slot { 0U });
      }
      Gfx().BeginFrame(
        frame::SequenceNumber { ++sequence }, frame::Slot { 0U });
      SimulateFrameStart(frame::Slot { 0U });
      publisher->OnFrameStart(
        frame::SequenceNumber { sequence }, frame::Slot { 0U });
    }
    std::unique_ptr<Publisher> publisher;
    unsigned sequence {};
  };

  NOLINT_TEST_F(
    DeferredConstantCacheTest, ReusesDescriptionsButWritesCurrentValues)
  {
    NextFrame();
    auto prefix = Staging().Allocate(SizeBytes { 16U }, "prefix");
    ASSERT_TRUE(prefix);
    auto records = std::array<DeferredLightConstants, 2> {};
    records[0].selection_index = LightSelectionIndex { 7U };
    const auto first = publisher->Publish(records);
    ASSERT_TRUE(first);
    const auto description = graphics::BufferViewDescription { .view_type
      = graphics::ResourceViewType::kConstantBuffer,
      .range = { 256U, 256U } };
    NextFrame();
    EXPECT_TRUE(
      Gfx().GetResourceRegistry().Contains(prefix->Buffer(), description));
    auto next_prefix = Staging().Allocate(SizeBytes { 16U }, "prefix");
    ASSERT_TRUE(next_prefix);
    records[0].selection_index = LightSelectionIndex { 19U };
    const auto second = publisher->Publish(records);
    ASSERT_TRUE(second);
    EXPECT_EQ(*first, *second);
    auto observed = DeferredLightConstants {};
    std::memcpy(&observed, next_prefix->Ptr() + 256U, sizeof(observed));
    EXPECT_EQ(observed.selection_index, LightSelectionIndex { 19U });
    // A second publication in the same frame cannot overwrite queued readers.
    records[0].selection_index = LightSelectionIndex { 23U };
    const auto third = publisher->Publish(records);
    ASSERT_TRUE(third);
    EXPECT_NE(second->front(), third->front());
    std::memcpy(&observed, next_prefix->Ptr() + 256U, sizeof(observed));
    EXPECT_EQ(observed.selection_index, LightSelectionIndex { 19U });
  }

  NOLINT_TEST_F(
    DeferredConstantCacheTest, GrowthCanOverlapSeveralRetiredBatchRanges)
  {
    NextFrame();
    auto records = std::vector<DeferredLightConstants>(2U);
    ASSERT_TRUE(publisher->Publish(records));
    ASSERT_TRUE(publisher->Publish(records));
    NextFrame();
    records.resize(6U);
    const auto larger = publisher->Publish(records);
    ASSERT_TRUE(larger);
    EXPECT_EQ(larger->size(), 6U);
    records.resize(1U);
    const auto following = publisher->Publish(records);
    ASSERT_TRUE(following);
    EXPECT_NE(larger->front(), following->front());
  }

  NOLINT_TEST_F(
    DeferredConstantCacheTest, UnusedBatchesRetireOnTheNextSlotReuse)
  {
    NextFrame();
    auto prefix = Staging().Allocate(SizeBytes { 16U }, "prefix");
    ASSERT_TRUE(prefix);
    const auto records = std::array<DeferredLightConstants, 2> {};
    ASSERT_TRUE(publisher->Publish(records));
    const auto description = graphics::BufferViewDescription { .view_type
      = graphics::ResourceViewType::kConstantBuffer,
      .range = { 256U, 256U } };
    NextFrame();
    ASSERT_TRUE(publisher->Publish({}));
    NextFrame();
    EXPECT_FALSE(
      Gfx().GetResourceRegistry().Contains(prefix->Buffer(), description));
  }
} // namespace
} // namespace oxygen::vortex::testing
