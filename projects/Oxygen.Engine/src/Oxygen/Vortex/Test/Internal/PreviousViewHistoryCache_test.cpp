//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Vortex/Internal/PreviousViewHistoryCache.h>

namespace {

using oxygen::vortex::CompositionView;
using oxygen::vortex::internal::PreviousViewHistoryCache;

auto MakeState(const float marker)
  -> PreviousViewHistoryCache::CurrentState
{
  auto state = PreviousViewHistoryCache::CurrentState {};
  state.view_matrix[3][0] = marker;
  state.projection_matrix[3][1] = marker;
  state.inverse_view_projection_matrix[3][3] = marker;
  state.pixel_jitter = { marker, marker + 1.0F };
  state.viewport = {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = 640.0F,
    .height = 360.0F,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  return state;
}

TEST(PreviousViewHistoryCacheTest, ReusesHistoryForStableProducerHandle)
{
  PreviousViewHistoryCache cache;
  const auto handle = CompositionView::ViewStateHandle { 11U };

  cache.BeginFrame(1U, {});
  const auto first = cache.TouchCurrent(handle, MakeState(1.0F));
  cache.EndFrame();

  cache.BeginFrame(2U, {});
  const auto second = cache.TouchCurrent(handle, MakeState(2.0F));

  EXPECT_FALSE(first.previous_valid);
  EXPECT_TRUE(second.previous_valid);
  EXPECT_FLOAT_EQ(second.previous.view_matrix[3][0], 1.0F);
}

TEST(PreviousViewHistoryCacheTest, DifferentProducerHandleStartsFreshHistory)
{
  PreviousViewHistoryCache cache;

  cache.BeginFrame(1U, {});
  cache.TouchCurrent(CompositionView::ViewStateHandle { 21U }, MakeState(1.0F));
  cache.EndFrame();

  cache.BeginFrame(2U, {});
  const auto recreated = cache.TouchCurrent(
    CompositionView::ViewStateHandle { 22U }, MakeState(2.0F));

  EXPECT_FALSE(recreated.previous_valid);
  EXPECT_FLOAT_EQ(recreated.previous.view_matrix[3][0], 2.0F);
}

TEST(PreviousViewHistoryCacheTest, StatelessViewsNeverPublishPreviousHistory)
{
  PreviousViewHistoryCache cache;

  cache.BeginFrame(1U, {});
  const auto first = cache.TouchStateless(MakeState(1.0F));
  cache.EndFrame();

  cache.BeginFrame(2U, {});
  const auto second = cache.TouchStateless(MakeState(2.0F));

  EXPECT_FALSE(first.previous_valid);
  EXPECT_FALSE(second.previous_valid);
  EXPECT_FLOAT_EQ(second.previous.view_matrix[3][0], 2.0F);
}

TEST(PreviousViewHistoryCacheTest, DescriptorChangeInvalidatesHistory)
{
  PreviousViewHistoryCache cache;
  const auto handle = CompositionView::ViewStateHandle { 31U };

  cache.BeginFrame(1U, {});
  cache.TouchCurrent(handle, MakeState(1.0F));
  cache.EndFrame();

  auto resized = MakeState(2.0F);
  resized.viewport.width = 800.0F;

  cache.BeginFrame(2U, {});
  const auto changed = cache.TouchCurrent(handle, resized);

  EXPECT_FALSE(changed.previous_valid);
  EXPECT_FLOAT_EQ(changed.previous.view_matrix[3][0], 2.0F);
}

} // namespace
