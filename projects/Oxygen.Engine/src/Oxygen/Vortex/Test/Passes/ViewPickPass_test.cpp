//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/mat4x4.hpp>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Passes/ViewPickPass.h>
#include <Oxygen/Vortex/Types/ViewPick.h>

namespace {

using oxygen::scene::NodeHandle;
using oxygen::vortex::PreparedSceneFrame;
using oxygen::vortex::ViewPickPass;
using oxygen::vortex::ViewPickRect;
using oxygen::vortex::ViewPickRequest;
using oxygen::vortex::ViewPickResult;
using DrawSource = PreparedSceneFrame::DrawSource;

//! A pick image of `width` x `height` texels with `padding` extra words per
//! row, all empty.
class PickImage {
public:
  PickImage(const std::uint32_t width, const std::uint32_t height,
    const std::size_t padding = 0U)
    : width_(width)
    , height_(height)
    , stride_(static_cast<std::size_t>(width) * 2U + padding)
    , texels_(stride_ * height, 0U)
  {
  }

  auto Set(const std::uint32_t x, const std::uint32_t y,
    const std::uint32_t draw_index, const float depth) -> void
  {
    const auto offset = static_cast<std::size_t>(y) * stride_
      + static_cast<std::size_t>(x) * 2U;
    texels_[offset] = draw_index + 1U;
    texels_[offset + 1U] = std::bit_cast<std::uint32_t>(depth);
  }

  [[nodiscard]] auto View() const -> ViewPickPass::PickImage
  {
    return {
      .texels = texels_,
      .width = width_,
      .height = height_,
      .row_stride = stride_,
    };
  }

private:
  std::uint32_t width_;
  std::uint32_t height_;
  std::size_t stride_;
  std::vector<std::uint32_t> texels_;
};

auto Geometry(const ViewPickRect rect) -> ViewPickPass::PickGeometry
{
  return {
    .rect = rect,
    .viewport = { .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = 100.0F,
      .height = 100.0F,
      .min_depth = 0.0F,
      .max_depth = 1.0F },
    .inv_view_proj = glm::mat4 { 1.0F },
    .reverse_z = true,
  };
}

NOLINT_TEST(ViewPickPassTest, EmptyImageCompletesWithoutHits)
{
  const auto image = PickImage(3U, 3U);
  const auto sources = std::vector<DrawSource> {
    { .node = NodeHandle(1U), .submesh_index = oxygen::data::SubmeshIndex {} },
  };

  const auto result = ViewPickPass::ResolveHits(image.View(), sources,
    Geometry({ .x = 0U, .y = 0U, .width = 3U, .height = 3U }));

  EXPECT_EQ(result.status, ViewPickResult::Status::kCompleted);
  EXPECT_TRUE(result.hits.empty());
  EXPECT_FALSE(result.world_position.has_value());
}

NOLINT_TEST(ViewPickPassTest, HitsOrderByCentreDistanceThenDepth)
{
  // Node B is nearer to the camera, node A is under the cursor.
  auto image = PickImage(5U, 5U);
  image.Set(2U, 2U, 0U, 0.2F);
  image.Set(0U, 0U, 1U, 0.9F);
  const auto sources = std::vector<DrawSource> {
    { .node = NodeHandle(1U), .submesh_index = oxygen::data::SubmeshIndex {} },
    { .node = NodeHandle(2U), .submesh_index = oxygen::data::SubmeshIndex {} },
  };

  const auto result = ViewPickPass::ResolveHits(image.View(), sources,
    Geometry({ .x = 0U, .y = 0U, .width = 5U, .height = 5U }));

  ASSERT_EQ(result.hits.size(), 2U);
  EXPECT_EQ(result.hits[0].node, NodeHandle(1U));
  EXPECT_FLOAT_EQ(result.hits[0].center_distance, 0.0F);
  EXPECT_EQ(result.hits[1].node, NodeHandle(2U));
}

NOLINT_TEST(ViewPickPassTest, EqualDistanceHitsPreferTheNearerSurface)
{
  // Reverse-Z: a larger depth is nearer.
  auto image = PickImage(3U, 1U);
  image.Set(0U, 0U, 0U, 0.3F);
  image.Set(2U, 0U, 1U, 0.6F);
  const auto sources = std::vector<DrawSource> {
    { .node = NodeHandle(1U), .submesh_index = oxygen::data::SubmeshIndex {} },
    { .node = NodeHandle(2U), .submesh_index = oxygen::data::SubmeshIndex {} },
  };

  const auto result = ViewPickPass::ResolveHits(image.View(), sources,
    Geometry({ .x = 0U, .y = 0U, .width = 3U, .height = 1U }));

  ASSERT_EQ(result.hits.size(), 2U);
  EXPECT_EQ(result.hits[0].node, NodeHandle(2U));
}

NOLINT_TEST(ViewPickPassTest, NodeDrawsMergeIntoOneHitWithClosestSlot)
{
  // Two submeshes of one node: the slot comes from the pixel closest to the
  // centre, the depth from the node's nearest pixel.
  auto image = PickImage(3U, 3U);
  image.Set(1U, 1U, 0U, 0.4F);
  image.Set(0U, 0U, 1U, 0.8F);
  const auto sources = std::vector<DrawSource> {
    { .node = NodeHandle(7U),
      .submesh_index = oxygen::data::SubmeshIndex { 3U } },
    { .node = NodeHandle(7U),
      .submesh_index = oxygen::data::SubmeshIndex { 5U } },
  };

  const auto result = ViewPickPass::ResolveHits(image.View(), sources,
    Geometry({ .x = 0U, .y = 0U, .width = 3U, .height = 3U }));

  ASSERT_EQ(result.hits.size(), 1U);
  EXPECT_EQ(result.hits[0].submesh_index, oxygen::data::SubmeshIndex { 3U });
  EXPECT_FLOAT_EQ(result.hits[0].depth, 0.8F);
}

NOLINT_TEST(ViewPickPassTest, UnknownDrawIndicesAndRowPaddingAreIgnored)
{
  auto image = PickImage(2U, 2U, 6U);
  image.Set(1U, 1U, 0U, 0.5F);
  image.Set(0U, 1U, 42U, 0.9F);
  const auto sources = std::vector<DrawSource> {
    { .node = NodeHandle(3U), .submesh_index = oxygen::data::SubmeshIndex {} },
  };

  const auto result = ViewPickPass::ResolveHits(image.View(), sources,
    Geometry({ .x = 0U, .y = 0U, .width = 2U, .height = 2U }));

  ASSERT_EQ(result.hits.size(), 1U);
  EXPECT_EQ(result.hits[0].node, NodeHandle(3U));
}

NOLINT_TEST(ViewPickPassTest, WorldPositionUnprojectsTheFirstHitPixel)
{
  // With an identity inverse view-projection the world position is the NDC
  // position of the pixel centre in the full viewport.
  auto image = PickImage(1U, 1U);
  image.Set(0U, 0U, 0U, 0.25F);
  const auto sources = std::vector<DrawSource> {
    { .node = NodeHandle(1U), .submesh_index = oxygen::data::SubmeshIndex {} },
  };

  const auto result = ViewPickPass::ResolveHits(image.View(), sources,
    Geometry({ .x = 74U, .y = 24U, .width = 1U, .height = 1U }));

  ASSERT_TRUE(result.world_position.has_value());
  EXPECT_NEAR(result.world_position->x, 0.49F, 1.0e-5F);
  EXPECT_NEAR(result.world_position->y, 0.51F, 1.0e-5F);
  EXPECT_NEAR(result.world_position->z, 0.25F, 1.0e-6F);
}

NOLINT_TEST(ViewPickPassTest, RequestCompletesExactlyOnce)
{
  auto calls = 0;
  auto status = ViewPickResult::Status::kCompleted;
  {
    auto request = std::make_shared<ViewPickRequest>(
      ViewPickRect {}, [&](const ViewPickResult& result) -> void {
        ++calls;
        status = result.status;
      });
    request->Complete(ViewPickResult {
      .status = ViewPickResult::Status::kFailed,
      .hits = {},
      .world_position = std::nullopt,
    });
    request->Complete(ViewPickResult {});
  }

  EXPECT_EQ(calls, 1);
  EXPECT_EQ(status, ViewPickResult::Status::kFailed);
}

NOLINT_TEST(ViewPickPassTest, DroppedRequestCompletesCancelled)
{
  auto status = ViewPickResult::Status::kCompleted;
  {
    const auto request = std::make_shared<ViewPickRequest>(ViewPickRect {},
      [&](const ViewPickResult& result) -> void { status = result.status; });
  }

  EXPECT_EQ(status, ViewPickResult::Status::kCancelled);
}

} // namespace
