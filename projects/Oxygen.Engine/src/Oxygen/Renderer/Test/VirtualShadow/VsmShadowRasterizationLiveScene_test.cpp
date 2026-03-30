// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>
#include <vector>

#include <glm/geometric.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPageAddressing.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpaceTypes.h>

#include "VirtualShadowLiveSceneHarness.h"

namespace {

using oxygen::engine::DrawPrimitiveFlagBits;
using oxygen::engine::PassMaskBit;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::renderer::vsm::TotalPageCount;
using oxygen::renderer::vsm::TryConvertToCoord;
using oxygen::renderer::vsm::VsmClipmapLayout;
using oxygen::renderer::vsm::VsmPageAllocationFrame;
using oxygen::renderer::vsm::VsmPageRequestProjection;
using oxygen::renderer::vsm::VsmPhysicalPageIndex;
using oxygen::renderer::vsm::VsmProjectionLightType;
using oxygen::renderer::vsm::VsmRenderedPageDirtyFlagBits;
using oxygen::renderer::vsm::VsmShadowRasterPageJob;
using oxygen::renderer::vsm::VsmVirtualMapLayout;
using oxygen::renderer::vsm::testing::TwoBoxShadowRasterizationResult;
using oxygen::renderer::vsm::testing::TwoBoxShadowSceneData;
using oxygen::renderer::vsm::testing::VsmLiveSceneHarness;

constexpr auto kDynamicRasterizedDirtyBit = static_cast<std::uint32_t>(
  VsmRenderedPageDirtyFlagBits::kDynamicRasterized);

class VsmShadowRasterizationLiveSceneTest : public VsmLiveSceneHarness {
protected:
  struct DepthSample {
    VsmPhysicalPageIndex physical_page {};
    std::uint32_t x { 0U };
    std::uint32_t y { 0U };
    float depth { 1.0F };
  };

  static auto DisableDirectionalShadowCasts(TwoBoxShadowSceneData& scene_data)
    -> void
  {
    auto sun_impl = scene_data.sun_node.GetImpl();
    ASSERT_TRUE(sun_impl.has_value());
    auto& sun_light
      = sun_impl->get().GetComponent<oxygen::scene::DirectionalLight>();
    sun_light.Common().casts_shadows = false;
    UpdateTransforms(*scene_data.scene, scene_data.sun_node);
  }

  static auto DisableShortBoxShadowCaster(TwoBoxShadowSceneData& scene_data)
    -> void
  {
    scene_data.rendered_items[2].cast_shadows = false;
    scene_data.draw_records[2].flags.Unset(PassMaskBit::kShadowCaster);
    scene_data.draw_records[2].primitive_flags
      &= ~static_cast<std::uint32_t>(DrawPrimitiveFlagBits::kMainViewVisible);
    scene_data.shadow_caster_bounds[1].w = 0.0F;
  }

  static auto MarkTallBoxStaticShadowCaster(TwoBoxShadowSceneData& scene_data)
    -> void
  {
    scene_data.rendered_items[1].static_shadow_caster = true;
    scene_data.draw_records[1].primitive_flags
      |= static_cast<std::uint32_t>(DrawPrimitiveFlagBits::kStaticShadowCaster);
  }

  [[nodiscard]] static auto FindProjection(
    std::span<const VsmPageRequestProjection> projections,
    const std::uint32_t map_id) -> const VsmPageRequestProjection*
  {
    const auto it = std::find_if(projections.begin(), projections.end(),
      [map_id](const auto& projection) { return projection.map_id == map_id; });
    return it != projections.end() ? &*it : nullptr;
  }

  [[nodiscard]] static auto CollectMappedPageTableIndices(
    const VsmClipmapLayout& layout,
    std::span<const oxygen::renderer::vsm::VsmPageTableEntry> page_table)
    -> std::vector<std::uint32_t>
  {
    auto indices = std::vector<std::uint32_t> {};
    const auto total_page_count = TotalPageCount(layout);
    for (std::uint32_t i = 0U; i < total_page_count; ++i) {
      const auto entry_index = layout.first_page_table_entry + i;
      if (entry_index >= page_table.size()) {
        break;
      }
      if (page_table[entry_index].is_mapped) {
        indices.push_back(entry_index);
      }
    }
    return indices;
  }

  [[nodiscard]] static auto CollectMappedPageTableIndices(
    const VsmVirtualMapLayout& layout,
    std::span<const oxygen::renderer::vsm::VsmPageTableEntry> page_table)
    -> std::vector<std::uint32_t>
  {
    auto indices = std::vector<std::uint32_t> {};
    for (std::uint32_t i = 0U; i < layout.total_page_count; ++i) {
      const auto entry_index = layout.first_page_table_entry + i;
      if (entry_index >= page_table.size()) {
        break;
      }
      if (page_table[entry_index].is_mapped) {
        indices.push_back(entry_index);
      }
    }
    return indices;
  }

  static auto ExpectPreparedJobContract(const VsmShadowRasterPageJob& job,
    const VsmPageAllocationFrame& frame,
    std::span<const VsmPageRequestProjection> projections,
    const oxygen::renderer::vsm::VsmPhysicalPoolSnapshot& pool) -> void
  {
    ASSERT_LT(job.page_table_index, frame.snapshot.page_table.size());
    EXPECT_TRUE(frame.snapshot.page_table[job.page_table_index].is_mapped);
    EXPECT_EQ(frame.snapshot.page_table[job.page_table_index].physical_page,
      job.physical_page);

    const auto physical_coord = TryConvertToCoord(job.physical_page,
      pool.tile_capacity, pool.tiles_per_axis, pool.slice_count);
    ASSERT_TRUE(physical_coord.has_value());
    EXPECT_EQ(*physical_coord, job.physical_coord);

    const auto* projection = FindProjection(projections, job.map_id);
    ASSERT_NE(projection, nullptr);
    EXPECT_LT(job.projection_page.level, projection->level_count);
    EXPECT_LT(job.projection_page.page_x, projection->pages_x);
    EXPECT_LT(job.projection_page.page_y, projection->pages_y);

    const auto page_size = static_cast<std::int32_t>(pool.page_size_texels);
    EXPECT_EQ(job.scissors.right - job.scissors.left, page_size);
    EXPECT_EQ(job.scissors.bottom - job.scissors.top, page_size);
    EXPECT_FLOAT_EQ(
      job.viewport.width, static_cast<float>(pool.page_size_texels));
    EXPECT_FLOAT_EQ(
      job.viewport.height, static_cast<float>(pool.page_size_texels));
    EXPECT_FLOAT_EQ(
      job.viewport.top_left_x, static_cast<float>(job.scissors.left));
    EXPECT_FLOAT_EQ(
      job.viewport.top_left_y, static_cast<float>(job.scissors.top));
  }

  static auto HasRasterizedPreparedPage(
    const TwoBoxShadowRasterizationResult& result,
    const std::uint32_t required_dirty_bit) -> bool
  {
    return std::any_of(result.prepared_pages.begin(),
      result.prepared_pages.end(), [&](const auto& job) {
        const auto index = job.physical_page.value;
        return index < result.dirty_flags.size()
          && index < result.physical_metadata.size()
          && (result.dirty_flags[index] & required_dirty_bit) != 0U
          && static_cast<bool>(result.physical_metadata[index].is_dirty)
          && static_cast<bool>(result.physical_metadata[index].used_this_frame);
      });
  }

  auto FindRasterizedDepthSample(
    std::span<const VsmShadowRasterPageJob> prepared_pages,
    const oxygen::renderer::vsm::VsmPhysicalPoolSnapshot& pool,
    const std::shared_ptr<const oxygen::graphics::Texture>& shadow_texture,
    std::string_view debug_name) -> std::optional<DepthSample>
  {
    CHECK_NOTNULL_F(shadow_texture.get(), "Shadow texture is null");

    constexpr auto kGrid = 5U;
    for (const auto& job : prepared_pages) {
      for (std::uint32_t grid_y = 0U; grid_y < kGrid; ++grid_y) {
        for (std::uint32_t grid_x = 0U; grid_x < kGrid; ++grid_x) {
          const auto x = job.physical_coord.tile_x * pool.page_size_texels
            + ((2U * grid_x + 1U) * pool.page_size_texels) / (2U * kGrid);
          const auto y = job.physical_coord.tile_y * pool.page_size_texels
            + ((2U * grid_y + 1U) * pool.page_size_texels) / (2U * kGrid);
          const auto depth = ReadDepthTexel(
            shadow_texture, job.physical_coord.slice, x, y, debug_name);
          if (depth < 1.0F) {
            return DepthSample {
              .physical_page = job.physical_page,
              .x = x,
              .y = y,
              .depth = depth,
            };
          }
        }
      }
    }
    return std::nullopt;
  }
};

NOLINT_TEST_F(VsmShadowRasterizationLiveSceneTest,
  DirectionalTwoBoxScenePublishesPreparedPagesDirtyOutputsAndRasterizedDepth)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kSequence = SequenceNumber { 251U };

  const auto camera_eye = glm::vec3 { -3.2F, 3.4F, 5.8F };
  const auto camera_target = glm::vec3 { 0.2F, 0.8F, 0.0F };
  const auto sun_direction
    = glm::normalize(glm::vec3 { -0.40558F, 0.40558F, 0.819152F });
  const auto resolved_view
    = MakeLookAtResolvedView(camera_eye, camera_target, kWidth, kHeight);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(sun_direction, 4U);
  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  const auto result = RunTwoBoxShadowRasterizationStage(*renderer, scene,
    vsm_renderer, resolved_view, kWidth, kHeight, kSequence, kSlot, 0xC130ULL);
  const auto& virtual_frame = result.initialization.propagation.mapping.bridge
                                .prepared_products.virtual_frame;
  const auto& frame
    = result.initialization.propagation.mapping.bridge.committed_frame;
  const auto& projections = result.initialization.propagation.mapping.bridge
                              .prepared_products.projection_records;

  ASSERT_EQ(virtual_frame.directional_layouts.size(), 1U);
  ASSERT_TRUE(virtual_frame.local_light_layouts.empty());
  ASSERT_FALSE(result.prepared_pages.empty());
  EXPECT_EQ(result.visible_shadow_primitives.size(), 2U);
  EXPECT_FALSE(result.rendered_primitive_history.empty());

  const auto& layout = virtual_frame.directional_layouts.front();
  ASSERT_GT(layout.pages_per_axis, 1U);
  const auto mapped_indices
    = CollectMappedPageTableIndices(layout, frame.snapshot.page_table);
  ASSERT_FALSE(mapped_indices.empty());

  auto prepared_indices = std::vector<std::uint32_t> {};
  prepared_indices.reserve(result.prepared_pages.size());
  for (const auto& page : result.prepared_pages) {
    prepared_indices.push_back(page.page_table_index);
    EXPECT_GE(page.map_id, layout.first_id);
    EXPECT_LT(page.map_id, layout.first_id + layout.clip_level_count);
    EXPECT_FALSE(page.static_only);
    ExpectPreparedJobContract(
      page, frame, projections, result.initialization.physical_pool);
  }
  EXPECT_EQ(prepared_indices, mapped_indices);
  EXPECT_TRUE(HasRasterizedPreparedPage(result, kDynamicRasterizedDirtyBit));

  const auto& pool = result.initialization.physical_pool;
  ASSERT_TRUE(pool.is_available);
  ASSERT_NE(pool.shadow_texture, nullptr);
  const auto depth_sample = FindRasterizedDepthSample(result.prepared_pages,
    pool, pool.shadow_texture, "stage-twelve-live.directional.depth");
  ASSERT_TRUE(depth_sample.has_value());
  EXPECT_LT(depth_sample->depth, 1.0F);
}

NOLINT_TEST_F(VsmShadowRasterizationLiveSceneTest,
  SpotLightTwoBoxScenePublishesLocalPreparedPagesDirtyOutputsAndRasterizedDepth)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kSequence = SequenceNumber { 252U };

  const auto camera_eye = glm::vec3 { -2.4F, 2.8F, 4.8F };
  const auto camera_target = glm::vec3 { 0.1F, 0.9F, 0.1F };
  const auto sun_direction
    = glm::normalize(glm::vec3 { -0.40558F, 0.40558F, 0.819152F });
  const auto resolved_view
    = MakeLookAtResolvedView(camera_eye, camera_target, kWidth, kHeight);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(sun_direction, 4U);
  DisableDirectionalShadowCasts(scene);
  AttachSpotLightToTwoBoxScene(scene, camera_eye,
    PrimarySpotTargetForTwoBoxScene(scene), 18.0F, glm::radians(30.0F),
    glm::radians(44.0F));

  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  const auto result = RunTwoBoxShadowRasterizationStage(*renderer, scene,
    vsm_renderer, resolved_view, kWidth, kHeight, kSequence, kSlot, 0xC131ULL);
  const auto& virtual_frame = result.initialization.propagation.mapping.bridge
                                .prepared_products.virtual_frame;
  const auto& frame
    = result.initialization.propagation.mapping.bridge.committed_frame;
  const auto& projections = result.initialization.propagation.mapping.bridge
                              .prepared_products.projection_records;

  ASSERT_TRUE(virtual_frame.directional_layouts.empty());
  ASSERT_EQ(virtual_frame.local_light_layouts.size(), 1U);
  ASSERT_FALSE(result.prepared_pages.empty());
  EXPECT_EQ(result.visible_shadow_primitives.size(), 2U);
  EXPECT_FALSE(result.rendered_primitive_history.empty());

  const auto& layout = virtual_frame.local_light_layouts.front();
  ASSERT_GT(layout.total_page_count, 1U);
  const auto mapped_indices
    = CollectMappedPageTableIndices(layout, frame.snapshot.page_table);
  ASSERT_FALSE(mapped_indices.empty());

  auto prepared_indices = std::vector<std::uint32_t> {};
  prepared_indices.reserve(result.prepared_pages.size());
  for (const auto& page : result.prepared_pages) {
    prepared_indices.push_back(page.page_table_index);
    EXPECT_EQ(page.map_id, layout.id);
    EXPECT_EQ(page.projection.projection.light_type,
      static_cast<std::uint32_t>(VsmProjectionLightType::kLocal));
    EXPECT_FALSE(page.static_only);
    ExpectPreparedJobContract(
      page, frame, projections, result.initialization.physical_pool);
  }
  EXPECT_EQ(prepared_indices, mapped_indices);
  EXPECT_TRUE(HasRasterizedPreparedPage(result, kDynamicRasterizedDirtyBit));

  const auto& pool = result.initialization.physical_pool;
  ASSERT_TRUE(pool.is_available);
  ASSERT_NE(pool.shadow_texture, nullptr);
  const auto depth_sample = FindRasterizedDepthSample(result.prepared_pages,
    pool, pool.shadow_texture, "stage-twelve-live.local.depth");
  ASSERT_TRUE(depth_sample.has_value());
  EXPECT_LT(depth_sample->depth, 1.0F);
}

NOLINT_TEST_F(VsmShadowRasterizationLiveSceneTest,
  StaticCasterScenePublishesStaticFeedbackAlongsideRasterizedPreparedPages)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kSequence = SequenceNumber { 253U };

  const auto camera_eye = glm::vec3 { -3.2F, 3.4F, 5.8F };
  const auto camera_target = glm::vec3 { 0.2F, 0.8F, 0.0F };
  const auto sun_direction
    = glm::normalize(glm::vec3 { -0.40558F, 0.40558F, 0.819152F });
  const auto resolved_view
    = MakeLookAtResolvedView(camera_eye, camera_target, kWidth, kHeight);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(sun_direction, 4U);
  MarkTallBoxStaticShadowCaster(scene);
  DisableShortBoxShadowCaster(scene);

  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  const auto result = RunTwoBoxShadowRasterizationStage(*renderer, scene,
    vsm_renderer, resolved_view, kWidth, kHeight, kSequence, kSlot, 0xC132ULL);
  const auto& frame
    = result.initialization.propagation.mapping.bridge.committed_frame;
  const auto& projections = result.initialization.propagation.mapping.bridge
                              .prepared_products.projection_records;

  ASSERT_FALSE(result.prepared_pages.empty());
  EXPECT_EQ(result.visible_shadow_primitives.size(), 1U);
  ASSERT_FALSE(result.static_page_feedback.empty());

  for (const auto& page : result.prepared_pages) {
    ExpectPreparedJobContract(
      page, frame, projections, result.initialization.physical_pool);
  }
  EXPECT_TRUE(HasRasterizedPreparedPage(result, kDynamicRasterizedDirtyBit));

  const auto& feedback = result.static_page_feedback.front();
  EXPECT_EQ(feedback.valid, 1U);
  EXPECT_TRUE(std::any_of(result.prepared_pages.begin(),
    result.prepared_pages.end(), [&](const auto& page) {
      return page.page_table_index == feedback.page_table_index
        && page.physical_page == feedback.physical_page;
    }));

  const auto& pool = result.initialization.physical_pool;
  ASSERT_TRUE(pool.is_available);
  ASSERT_NE(pool.shadow_texture, nullptr);
  const auto depth_sample = FindRasterizedDepthSample(result.prepared_pages,
    pool, pool.shadow_texture, "stage-twelve-live.static-feedback.depth");
  ASSERT_TRUE(depth_sample.has_value());
  EXPECT_LT(depth_sample->depth, 1.0F);
}

} // namespace
