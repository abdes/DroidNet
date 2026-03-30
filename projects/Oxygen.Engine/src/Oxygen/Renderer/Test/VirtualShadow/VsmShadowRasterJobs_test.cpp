// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <glm/geometric.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPageAddressing.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShadowRasterJobs.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpaceTypes.h>

#include "VirtualShadowLiveSceneHarness.h"

namespace {

using oxygen::engine::DrawPrimitiveFlagBits;
using oxygen::engine::PassMaskBit;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::renderer::vsm::BuildShadowRasterPageJobs;
using oxygen::renderer::vsm::TotalPageCount;
using oxygen::renderer::vsm::TryConvertToCoord;
using oxygen::renderer::vsm::VsmClipmapLayout;
using oxygen::renderer::vsm::VsmPageRequestProjection;
using oxygen::renderer::vsm::VsmPhysicalPageCoord;
using oxygen::renderer::vsm::VsmProjectionLightType;
using oxygen::renderer::vsm::VsmShadowRasterPageJob;
using oxygen::renderer::vsm::VsmVirtualMapLayout;
using oxygen::renderer::vsm::testing::TwoBoxPageInitializationResult;
using oxygen::renderer::vsm::testing::TwoBoxShadowSceneData;
using oxygen::renderer::vsm::testing::VsmLiveSceneHarness;

class VsmShadowRasterPageJobsLiveSceneTest : public VsmLiveSceneHarness {
protected:
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

  static auto ExpectJobContract(const VsmShadowRasterPageJob& job,
    const TwoBoxPageInitializationResult& stage_input) -> void
  {
    const auto& frame = stage_input.propagation.mapping.bridge.committed_frame;
    ASSERT_LT(job.page_table_index, frame.snapshot.page_table.size());
    EXPECT_TRUE(frame.snapshot.page_table[job.page_table_index].is_mapped);
    EXPECT_EQ(frame.snapshot.page_table[job.page_table_index].physical_page,
      job.physical_page);

    const auto physical_coord = TryConvertToCoord(job.physical_page,
      stage_input.physical_pool.tile_capacity,
      stage_input.physical_pool.tiles_per_axis,
      stage_input.physical_pool.slice_count);
    ASSERT_TRUE(physical_coord.has_value());
    EXPECT_EQ(*physical_coord, job.physical_coord);

    const auto* projection
      = FindProjection(stage_input.propagation.mapping.bridge.prepared_products
                         .projection_records,
        job.map_id);
    ASSERT_NE(projection, nullptr);
    EXPECT_GE(job.projection_page.level, 0U);
    EXPECT_LT(job.projection_page.level, projection->level_count);
    EXPECT_LT(job.projection_page.page_x, projection->pages_x);
    EXPECT_LT(job.projection_page.page_y, projection->pages_y);

    const auto page_size
      = static_cast<std::int32_t>(stage_input.physical_pool.page_size_texels);
    EXPECT_EQ(job.scissors.right - job.scissors.left, page_size);
    EXPECT_EQ(job.scissors.bottom - job.scissors.top, page_size);
    EXPECT_FLOAT_EQ(job.viewport.width,
      static_cast<float>(stage_input.physical_pool.page_size_texels));
    EXPECT_FLOAT_EQ(job.viewport.height,
      static_cast<float>(stage_input.physical_pool.page_size_texels));
    EXPECT_FLOAT_EQ(
      job.viewport.top_left_x, static_cast<float>(job.scissors.left));
    EXPECT_FLOAT_EQ(
      job.viewport.top_left_y, static_cast<float>(job.scissors.top));
  }
};

NOLINT_TEST_F(VsmShadowRasterPageJobsLiveSceneTest,
  DirectionalTwoBoxSceneBuildsJobsFromRealInitializedFrame)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kSequence = SequenceNumber { 241U };

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

  const auto stage_input = RunTwoBoxPageInitializationStage(*renderer, scene,
    vsm_renderer, resolved_view, kWidth, kHeight, kSequence, kSlot, 0xC120ULL);
  const auto& virtual_frame
    = stage_input.propagation.mapping.bridge.prepared_products.virtual_frame;

  ASSERT_EQ(virtual_frame.directional_layouts.size(), 1U);
  ASSERT_TRUE(virtual_frame.local_light_layouts.empty());
  const auto& layout = virtual_frame.directional_layouts.front();
  ASSERT_GT(layout.pages_per_axis, 1U);

  const auto jobs = BuildShadowRasterPageJobs(
    stage_input.propagation.mapping.bridge.committed_frame,
    stage_input.physical_pool,
    stage_input.propagation.mapping.bridge.prepared_products
      .projection_records);

  const auto mapped_indices = CollectMappedPageTableIndices(layout,
    stage_input.propagation.mapping.bridge.committed_frame.snapshot.page_table);
  ASSERT_FALSE(mapped_indices.empty());
  ASSERT_EQ(jobs.size(), mapped_indices.size());
  EXPECT_TRUE(std::is_sorted(
    jobs.begin(), jobs.end(), [](const auto& lhs, const auto& rhs) {
      return lhs.page_table_index < rhs.page_table_index;
    }));

  auto job_indices = std::vector<std::uint32_t> {};
  job_indices.reserve(jobs.size());
  for (const auto& job : jobs) {
    job_indices.push_back(job.page_table_index);
    EXPECT_EQ(job.map_id, layout.first_id + job.virtual_page.level);
    EXPECT_FALSE(job.static_only);
    ExpectJobContract(job, stage_input);
  }
  EXPECT_EQ(job_indices, mapped_indices);
}

NOLINT_TEST_F(VsmShadowRasterPageJobsLiveSceneTest,
  StaticCasterSceneBuildsDeterministicJobsFromRealInitializedFrame)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kSequence = SequenceNumber { 242U };

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

  const auto stage_input = RunTwoBoxPageInitializationStage(*renderer, scene,
    vsm_renderer, resolved_view, kWidth, kHeight, kSequence, kSlot, 0xC121ULL);
  const auto& virtual_frame
    = stage_input.propagation.mapping.bridge.prepared_products.virtual_frame;
  ASSERT_EQ(virtual_frame.directional_layouts.size(), 1U);

  const auto jobs = BuildShadowRasterPageJobs(
    stage_input.propagation.mapping.bridge.committed_frame,
    stage_input.physical_pool,
    stage_input.propagation.mapping.bridge.prepared_products
      .projection_records);

  ASSERT_FALSE(jobs.empty());
  EXPECT_EQ(jobs.size(),
    CollectMappedPageTableIndices(virtual_frame.directional_layouts.front(),
      stage_input.propagation.mapping.bridge.committed_frame.snapshot
        .page_table)
      .size());

  for (const auto& job : jobs) {
    ExpectJobContract(job, stage_input);
    EXPECT_FALSE(job.static_only);
    EXPECT_EQ(job.physical_coord.slice, 0U);
  }
}

NOLINT_TEST_F(VsmShadowRasterPageJobsLiveSceneTest,
  SpotLightTwoBoxSceneBuildsLocalJobsFromRealInitializedFrame)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kSequence = SequenceNumber { 243U };

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

  const auto stage_input = RunTwoBoxPageInitializationStage(*renderer, scene,
    vsm_renderer, resolved_view, kWidth, kHeight, kSequence, kSlot, 0xC122ULL);
  const auto& virtual_frame
    = stage_input.propagation.mapping.bridge.prepared_products.virtual_frame;

  ASSERT_TRUE(virtual_frame.directional_layouts.empty());
  ASSERT_EQ(virtual_frame.local_light_layouts.size(), 1U);
  const auto& layout = virtual_frame.local_light_layouts.front();
  ASSERT_GT(layout.total_page_count, 1U);

  const auto jobs = BuildShadowRasterPageJobs(
    stage_input.propagation.mapping.bridge.committed_frame,
    stage_input.physical_pool,
    stage_input.propagation.mapping.bridge.prepared_products
      .projection_records);

  const auto mapped_indices = CollectMappedPageTableIndices(layout,
    stage_input.propagation.mapping.bridge.committed_frame.snapshot.page_table);
  ASSERT_FALSE(mapped_indices.empty());
  ASSERT_EQ(jobs.size(), mapped_indices.size());

  auto job_indices = std::vector<std::uint32_t> {};
  job_indices.reserve(jobs.size());
  for (const auto& job : jobs) {
    job_indices.push_back(job.page_table_index);
    EXPECT_EQ(job.map_id, layout.id);
    EXPECT_FALSE(job.static_only);
    EXPECT_EQ(job.projection.projection.light_type,
      static_cast<std::uint32_t>(VsmProjectionLightType::kLocal));
    ExpectJobContract(job, stage_input);
  }
  EXPECT_EQ(job_indices, mapped_indices);
}

} // namespace
