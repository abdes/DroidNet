//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/ImGui/GpuTimelinePresentationSmoother.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::engine::imgui::GpuTimelinePresentationFrame;
using oxygen::engine::imgui::GpuTimelinePresentationSmoother;
using oxygen::engine::internal::GpuTimelineFrame;
using oxygen::engine::internal::GpuTimelineScope;

auto MakeScope(const uint32_t scope_id, const uint32_t parent_scope_id,
  const uint64_t name_hash, std::string display_name, const uint16_t depth,
  const float start_ms, const float duration_ms) -> GpuTimelineScope
{
  GpuTimelineScope scope {};
  scope.scope_id = scope_id;
  scope.parent_scope_id = parent_scope_id;
  scope.name_hash = name_hash;
  scope.display_name = std::move(display_name);
  scope.start_ms = start_ms;
  scope.end_ms = start_ms + duration_ms;
  scope.duration_ms = duration_ms;
  scope.depth = depth;
  scope.valid = true;
  return scope;
}

auto MakeFrame(const uint64_t frame_sequence, const float start_ms,
  const float duration_ms) -> GpuTimelineFrame
{
  GpuTimelineFrame frame {};
  frame.frame_sequence = frame_sequence;

  frame.scopes.push_back(MakeScope(
    0U, 0xFFFFFFFFU, 0xA5A5U, "Main Pass", 0U, start_ms, duration_ms));
  return frame;
}

NOLINT_TEST(GpuTimelinePresentationSmootherTest,
  FirstFrameUsesRawBarValuesAndHeadroomSpan)
{
  GpuTimelinePresentationSmoother smoother;
  const auto presentation = smoother.Apply(MakeFrame(1U, 1.0F, 4.0F));

  ASSERT_EQ(presentation.scopes.size(), 1U);
  EXPECT_FLOAT_EQ(presentation.scopes[0].display_start_ms, 1.0F);
  EXPECT_FLOAT_EQ(presentation.scopes[0].display_duration_ms, 4.0F);
  EXPECT_FLOAT_EQ(presentation.display_frame_span_ms,
    5.0F + GpuTimelinePresentationSmoother::kFrameSpanHeadroomMs);
}

NOLINT_TEST(
  GpuTimelinePresentationSmootherTest, SubsequentFramesBlendTowardNewValues)
{
  GpuTimelinePresentationSmoother smoother;
  static_cast<void>(smoother.Apply(MakeFrame(1U, 1.0F, 4.0F)));

  const auto presentation = smoother.Apply(MakeFrame(2U, 2.0F, 6.0F));

  ASSERT_EQ(presentation.scopes.size(), 1U);
  EXPECT_GT(presentation.scopes[0].display_start_ms, 1.0F);
  EXPECT_LT(presentation.scopes[0].display_start_ms, 2.0F);
  EXPECT_GT(presentation.scopes[0].display_duration_ms, 4.0F);
  EXPECT_LT(presentation.scopes[0].display_duration_ms, 6.0F);
  EXPECT_FLOAT_EQ(presentation.scopes[0].raw_start_ms, 2.0F);
  EXPECT_FLOAT_EQ(presentation.scopes[0].raw_duration_ms, 6.0F);
}

NOLINT_TEST(
  GpuTimelinePresentationSmootherTest, RepeatedApplicationConvergesToTarget)
{
  GpuTimelinePresentationSmoother smoother;
  static_cast<void>(smoother.Apply(MakeFrame(1U, 1.0F, 4.0F)));

  GpuTimelinePresentationFrame presentation;
  for (uint64_t i = 2U; i <= 200U; ++i) {
    presentation = smoother.Apply(MakeFrame(i, 3.0F, 8.0F));
  }

  ASSERT_EQ(presentation.scopes.size(), 1U);
  EXPECT_NEAR(presentation.scopes[0].display_start_ms, 3.0F,
    GpuTimelinePresentationSmoother::kSnapThresholdMs);
  EXPECT_NEAR(presentation.scopes[0].display_duration_ms, 8.0F,
    GpuTimelinePresentationSmoother::kSnapThresholdMs);
}

NOLINT_TEST(GpuTimelinePresentationSmootherTest, SmallChangesStillBlend)
{
  GpuTimelinePresentationSmoother smoother;
  static_cast<void>(smoother.Apply(MakeFrame(1U, 1.0F, 4.0F)));

  const auto presentation = smoother.Apply(MakeFrame(2U, 1.4F, 4.3F));

  ASSERT_EQ(presentation.scopes.size(), 1U);
  EXPECT_GT(presentation.scopes[0].display_start_ms, 1.0F);
  EXPECT_LT(presentation.scopes[0].display_start_ms, 1.4F);
  EXPECT_GT(presentation.scopes[0].display_duration_ms, 4.0F);
  EXPECT_LT(presentation.scopes[0].display_duration_ms, 4.3F);
}

NOLINT_TEST(GpuTimelinePresentationSmootherTest,
  TinyJitterInsideDeadbandDoesNotMoveBarsOrRescaleSpan)
{
  GpuTimelinePresentationSmoother smoother;
  const auto first = smoother.Apply(MakeFrame(1U, 1.0F, 4.0F));
  const auto second = smoother.Apply(MakeFrame(2U, 1.03F, 4.02F));

  ASSERT_EQ(first.scopes.size(), 1U);
  ASSERT_EQ(second.scopes.size(), 1U);
  EXPECT_NEAR(second.scopes[0].display_start_ms,
    first.scopes[0].display_start_ms, 0.002F);
  EXPECT_NEAR(second.scopes[0].display_duration_ms,
    first.scopes[0].display_duration_ms, 0.002F);
  EXPECT_NEAR(second.display_frame_span_ms, first.display_frame_span_ms,
    0.002F);
}

NOLINT_TEST(GpuTimelinePresentationSmootherTest,
  RepeatedSiblingNamesGetDistinctStableTracks)
{
  GpuTimelinePresentationSmoother smoother;

  GpuTimelineFrame first {};
  first.frame_sequence = 1U;
  first.scopes = {
    GpuTimelineScope {
      .scope_id = 0U,
      .parent_scope_id = 0xFFFFFFFFU,
      .name_hash = 11U,
      .display_name = "Shadow",
      .end_ms = 1.0F,
      .duration_ms = 1.0F,
      .valid = true,
    },
    GpuTimelineScope {
      .scope_id = 1U,
      .parent_scope_id = 0xFFFFFFFFU,
      .name_hash = 11U,
      .display_name = "Shadow",
      .start_ms = 2.0F,
      .end_ms = 3.0F,
      .duration_ms = 1.0F,
      .valid = true,
    },
  };
  static_cast<void>(smoother.Apply(first));

  auto second = first;
  second.frame_sequence = 2U;
  second.scopes[0].start_ms = 0.6F;
  second.scopes[0].end_ms = 1.6F;
  second.scopes[1].start_ms = 4.0F;
  second.scopes[1].end_ms = 5.0F;

  const auto presentation = smoother.Apply(second);

  ASSERT_EQ(presentation.scopes.size(), 2U);
  EXPECT_LT(presentation.scopes[0].display_start_ms, 0.6F);
  EXPECT_GT(presentation.scopes[0].display_start_ms, 0.0F);
  EXPECT_LT(presentation.scopes[1].display_start_ms, 4.0F);
  EXPECT_GT(presentation.scopes[1].display_start_ms, 2.0F);
}

NOLINT_TEST(GpuTimelinePresentationSmootherTest,
  RepeatedPassScopesAreGroupedIntoOneDisplayRowWithPhaseChildren)
{
  GpuTimelinePresentationSmoother smoother;

  GpuTimelineFrame frame {};
  frame.frame_sequence = 1U;
  frame.scopes = {
    MakeScope(0U, 0xFFFFFFFFU, 100U, "RenderGraph", 0U, 0.0F, 8.0F),
    MakeScope(1U, 0U, 200U, "DepthPrePass", 1U, 0.0F, 1.0F),
    MakeScope(2U, 1U, 201U, "PrepareResources", 2U, 0.0F, 1.0F),
    MakeScope(3U, 0U, 200U, "DepthPrePass", 1U, 1.0F, 3.0F),
    MakeScope(4U, 3U, 202U, "Execute", 2U, 1.0F, 3.0F),
  };

  const auto presentation = smoother.Apply(frame);

  ASSERT_EQ(presentation.scopes.size(), 5U);
  ASSERT_EQ(presentation.rows.size(), 4U);

  EXPECT_EQ(presentation.rows[0].display_name, "RenderGraph");
  EXPECT_EQ(presentation.rows[0].depth, 0U);

  EXPECT_EQ(presentation.rows[1].display_name, "DepthPrePass");
  EXPECT_EQ(presentation.rows[1].depth, 1U);
  EXPECT_TRUE(presentation.rows[1].is_grouped_pass);
  EXPECT_EQ(presentation.rows[1].grouped_scope_count, 2U);
  EXPECT_FLOAT_EQ(presentation.rows[1].raw_start_ms, 0.0F);
  EXPECT_FLOAT_EQ(presentation.rows[1].raw_duration_ms, 4.0F);

  EXPECT_EQ(presentation.rows[2].display_name, "PrepareResources");
  EXPECT_EQ(presentation.rows[2].depth, 2U);
  EXPECT_FALSE(presentation.rows[2].is_grouped_pass);

  EXPECT_EQ(presentation.rows[3].display_name, "Execute");
  EXPECT_EQ(presentation.rows[3].depth, 2U);
  EXPECT_FALSE(presentation.rows[3].is_grouped_pass);
}

NOLINT_TEST(GpuTimelinePresentationSmootherTest,
  RepeatedRootPassScopesAreGroupedIntoOneDisplayRowWithPhaseChildren)
{
  GpuTimelinePresentationSmoother smoother;

  GpuTimelineFrame frame {};
  frame.frame_sequence = 1U;
  frame.scopes = {
    MakeScope(0U, 0xFFFFFFFFU, 300U, "CompositingPass", 0U, 0.0F, 0.3F),
    MakeScope(1U, 0U, 301U, "PrepareResources", 1U, 0.0F, 0.3F),
    MakeScope(2U, 0xFFFFFFFFU, 300U, "CompositingPass", 0U, 0.3F, 0.7F),
    MakeScope(3U, 2U, 302U, "Execute", 1U, 0.3F, 0.7F),
  };

  const auto presentation = smoother.Apply(frame);

  ASSERT_EQ(presentation.rows.size(), 3U);
  EXPECT_EQ(presentation.rows[0].display_name, "CompositingPass");
  EXPECT_EQ(presentation.rows[0].depth, 0U);
  EXPECT_TRUE(presentation.rows[0].is_grouped_pass);
  EXPECT_EQ(presentation.rows[0].grouped_scope_count, 2U);

  EXPECT_EQ(presentation.rows[1].display_name, "PrepareResources");
  EXPECT_EQ(presentation.rows[1].depth, 1U);
  EXPECT_FALSE(presentation.rows[1].is_grouped_pass);

  EXPECT_EQ(presentation.rows[2].display_name, "Execute");
  EXPECT_EQ(presentation.rows[2].depth, 1U);
  EXPECT_FALSE(presentation.rows[2].is_grouped_pass);
}

} // namespace
