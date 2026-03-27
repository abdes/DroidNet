//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPageAddressing.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpaceTypes.h>

#include "VirtualShadowLiveSceneHarness.h"

namespace {

using oxygen::engine::DrawPrimitiveFlagBits;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::renderer::vsm::DecodePhysicalPageIndex;
using oxygen::renderer::vsm::IsMapped;
using oxygen::renderer::vsm::TryConvertToCoord;
using oxygen::renderer::vsm::VsmCacheInvalidationReason;
using oxygen::renderer::vsm::VsmCacheInvalidationScope;
using oxygen::renderer::vsm::VsmPageAllocationFrame;
using oxygen::renderer::vsm::VsmPageInitializationAction;
using oxygen::renderer::vsm::VsmPhysicalPageIndex;
using oxygen::renderer::vsm::VsmPhysicalPageMeta;
using oxygen::renderer::vsm::VsmPhysicalPoolSliceRole;
using oxygen::renderer::vsm::VsmShaderPageFlags;
using oxygen::renderer::vsm::VsmShaderPageTableEntry;
using oxygen::renderer::vsm::testing::TwoBoxPageFlagPropagationResult;
using oxygen::renderer::vsm::testing::TwoBoxShadowSceneData;
using oxygen::renderer::vsm::testing::VsmLiveSceneHarness;

struct StageBuffers {
  std::vector<VsmShaderPageTableEntry> page_table {};
  std::vector<VsmShaderPageFlags> page_flags {};
  std::vector<VsmPhysicalPageMeta> physical_metadata {};
};

struct PageSample {
  VsmPhysicalPageIndex physical_page {};
  std::uint32_t x { 0U };
  std::uint32_t y { 0U };
  float value { 1.0F };
};

struct CopySample {
  VsmPhysicalPageIndex physical_page {};
  std::uint32_t x { 0U };
  std::uint32_t y { 0U };
  float dynamic_before { 1.0F };
  float static_before { 1.0F };
};

struct ShadowSliceSnapshot {
  std::uint32_t width { 0U };
  std::uint32_t height { 0U };
  std::vector<float> values {};

  [[nodiscard]] auto At(const std::uint32_t x, const std::uint32_t y) const
    -> float
  {
    return values.at(static_cast<std::size_t>(y) * width + x);
  }
};

class VsmSelectivePageInitializationLiveSceneTest : public VsmLiveSceneHarness {
protected:
  static constexpr auto kShadowThreshold = 0.99F;
  static constexpr auto kCopyEpsilon = 1.0e-4F;

  [[nodiscard]] static auto FindSliceIndex(
    const oxygen::renderer::vsm::VsmPhysicalPoolSnapshot& pool,
    const VsmPhysicalPoolSliceRole role) -> std::optional<std::uint32_t>
  {
    for (std::uint32_t i = 0U; i < pool.slice_roles.size(); ++i) {
      if (pool.slice_roles[i] == role) {
        return i;
      }
    }
    return std::nullopt;
  }

  static auto MoveBox(TwoBoxShadowSceneData& scene_data,
    const std::size_t index, oxygen::scene::SceneNode& node,
    const glm::vec3& translation, const glm::vec3& scale) -> void
  {
    scene_data.world_matrices[index]
      = glm::translate(glm::mat4 { 1.0F }, translation)
      * glm::scale(glm::mat4 { 1.0F }, scale);

    const auto radius = scale.y > 2.0F ? 1.75F : 0.95F;
    scene_data.draw_bounds[index]
      = glm::vec4 { translation.x, scale.y * 0.5F, translation.z, radius };
    scene_data.shadow_caster_bounds[index - 1U] = scene_data.draw_bounds[index];
    scene_data.rendered_items[index].world_bounding_sphere
      = scene_data.draw_bounds[index];

    ASSERT_TRUE(node.GetTransform().SetLocalPosition(translation));
    auto impl = node.GetImpl();
    ASSERT_TRUE(impl.has_value());
    impl->get().UpdateTransforms(*scene_data.scene);
    scene_data.scene->Update();
  }

  static auto MoveTallBox(
    TwoBoxShadowSceneData& scene_data, const glm::vec3& translation) -> void
  {
    MoveBox(scene_data, 1U, scene_data.tall_box_node, translation,
      glm::vec3 { 0.8F, 3.2F, 0.8F });
  }

  static auto MoveShortBox(
    TwoBoxShadowSceneData& scene_data, const glm::vec3& translation) -> void
  {
    MoveBox(scene_data, 2U, scene_data.short_box_node, translation,
      glm::vec3 { 0.8F, 1.1F, 0.8F });
  }

  static auto MarkTallBoxStaticShadowCaster(TwoBoxShadowSceneData& scene_data)
    -> void
  {
    scene_data.rendered_items[1].static_shadow_caster = true;
    scene_data.draw_records[1].primitive_flags
      |= static_cast<std::uint32_t>(DrawPrimitiveFlagBits::kStaticShadowCaster);
  }

  auto CaptureStageBuffers(const VsmPageAllocationFrame& frame,
    std::string_view debug_name) -> StageBuffers
  {
    return StageBuffers {
      .page_table = ReadBufferAs<VsmShaderPageTableEntry>(
        *frame.page_table_buffer, frame.snapshot.page_table.size(),
        std::string(debug_name) + ".page-table"),
      .page_flags = ReadBufferAs<VsmShaderPageFlags>(*frame.page_flags_buffer,
        frame.snapshot.page_table.size(),
        std::string(debug_name) + ".page-flags"),
      .physical_metadata = ReadBufferAs<VsmPhysicalPageMeta>(
        *frame.physical_page_meta_buffer, frame.snapshot.physical_pages.size(),
        std::string(debug_name) + ".physical-meta"),
    };
  }

  auto ExpectStageBuffersUnchanged(const VsmPageAllocationFrame& frame,
    const StageBuffers& before, std::string_view debug_name) -> void
  {
    const auto after = CaptureStageBuffers(frame, debug_name);
    EXPECT_EQ(after.page_table, before.page_table);
    EXPECT_EQ(after.page_flags, before.page_flags);
    EXPECT_EQ(after.physical_metadata, before.physical_metadata);
  }

  auto ReadShadowSlice(
    const std::shared_ptr<const oxygen::graphics::Texture>& texture,
    const std::uint32_t slice, std::string_view debug_name)
    -> ShadowSliceSnapshot
  {
    CHECK_NOTNULL_F(texture.get(), "Cannot read from a null shadow texture");

    const auto& texture_desc = texture->GetDescriptor();
    auto float_texture
      = CreateSingleChannelTexture2D(texture_desc.width, texture_desc.height,
        oxygen::Format::kR32Float, std::string(debug_name) + ".slice-copy");
    CHECK_NOTNULL_F(float_texture.get(),
      "Failed to create float slice copy for `{}`", debug_name);

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".copy");
      CHECK_NOTNULL_F(recorder.get(),
        "Failed to acquire shadow-slice copy recorder for `{}`", debug_name);
      EnsureTracked(*recorder,
        std::const_pointer_cast<oxygen::graphics::Texture>(texture),
        oxygen::graphics::ResourceStates::kCommon);
      EnsureTracked(
        *recorder, float_texture, oxygen::graphics::ResourceStates::kCommon);
      recorder->RequireResourceState(
        *texture, oxygen::graphics::ResourceStates::kCopySource);
      recorder->RequireResourceState(
        *float_texture, oxygen::graphics::ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyTexture(*texture,
        oxygen::graphics::TextureSlice {
          .x = 0U,
          .y = 0U,
          .z = 0U,
          .width = texture_desc.width,
          .height = texture_desc.height,
          .depth = 1U,
          .mip_level = 0U,
          .array_slice = slice,
        },
        oxygen::graphics::TextureSubResourceSet {
          .base_mip_level = 0U,
          .num_mip_levels = 1U,
          .base_array_slice = slice,
          .num_array_slices = 1U,
        },
        *float_texture,
        oxygen::graphics::TextureSlice {
          .x = 0U,
          .y = 0U,
          .z = 0U,
          .width = texture_desc.width,
          .height = texture_desc.height,
          .depth = 1U,
          .mip_level = 0U,
          .array_slice = 0U,
        },
        oxygen::graphics::TextureSubResourceSet::EntireTexture());
      recorder->RequireResourceStateFinal(
        *float_texture, oxygen::graphics::ResourceStates::kCommon);
    }
    WaitForQueueIdle();

    const auto readback = GetReadbackManager()->ReadTextureNow(*float_texture,
      oxygen::graphics::TextureReadbackRequest {
        .src_slice = {},
        .aspects = oxygen::graphics::ClearFlags::kColor,
      },
      true);
    CHECK_F(readback.has_value(),
      "Failed to read back shadow slice copy for `{}`", debug_name);

    auto values
      = std::vector<float>(texture_desc.width * texture_desc.height, 1.0F);
    for (std::uint32_t y = 0U; y < texture_desc.height; ++y) {
      const auto* row = readback->bytes.data()
        + static_cast<std::size_t>(y) * readback->layout.row_pitch.get();
      for (std::uint32_t x = 0U; x < texture_desc.width; ++x) {
        std::memcpy(
          &values[static_cast<std::size_t>(y) * texture_desc.width + x],
          row + static_cast<std::size_t>(x) * sizeof(float), sizeof(float));
      }
    }

    return ShadowSliceSnapshot {
      .width = texture_desc.width,
      .height = texture_desc.height,
      .values = std::move(values),
    };
  }

  template <typename Predicate>
  auto FindPageSampleIf(
    const oxygen::renderer::vsm::VsmPhysicalPoolSnapshot& pool,
    const ShadowSliceSnapshot& slice_snapshot,
    const VsmPhysicalPageIndex physical_page, std::string_view debug_name,
    Predicate&& predicate, const std::uint32_t grid_size = 5U)
    -> std::optional<PageSample>
  {
    static_cast<void>(debug_name);

    const auto coord = TryConvertToCoord(
      physical_page, pool.tile_capacity, pool.tiles_per_axis, pool.slice_count);
    EXPECT_TRUE(coord.has_value());
    if (!coord.has_value()) {
      return std::nullopt;
    }

    if (grid_size == 0U) {
      const auto base_x = coord->tile_x * pool.page_size_texels;
      const auto base_y = coord->tile_y * pool.page_size_texels;
      for (std::uint32_t local_y = 0U; local_y < pool.page_size_texels;
        ++local_y) {
        for (std::uint32_t local_x = 0U; local_x < pool.page_size_texels;
          ++local_x) {
          const auto x = base_x + local_x;
          const auto y = base_y + local_y;
          const auto value = slice_snapshot.At(x, y);
          if (predicate(value)) {
            return PageSample {
              .physical_page = physical_page,
              .x = x,
              .y = y,
              .value = value,
            };
          }
        }
      }
      return std::nullopt;
    }

    for (std::uint32_t grid_y = 0U; grid_y < grid_size; ++grid_y) {
      for (std::uint32_t grid_x = 0U; grid_x < grid_size; ++grid_x) {
        const auto x = coord->tile_x * pool.page_size_texels
          + ((2U * grid_x + 1U) * pool.page_size_texels) / (2U * grid_size);
        const auto y = coord->tile_y * pool.page_size_texels
          + ((2U * grid_y + 1U) * pool.page_size_texels) / (2U * grid_size);
        const auto value = slice_snapshot.At(x, y);
        if (predicate(value)) {
          return PageSample {
            .physical_page = physical_page,
            .x = x,
            .y = y,
            .value = value,
          };
        }
      }
    }

    return std::nullopt;
  }

  auto FindShadowedMappedPageSample(
    const std::vector<VsmShaderPageTableEntry>& page_table,
    const oxygen::renderer::vsm::VsmPhysicalPoolSnapshot& pool,
    const ShadowSliceSnapshot& dynamic_slice,
    const std::vector<std::uint32_t>& excluded_physical_pages,
    std::string_view debug_name) -> std::optional<PageSample>
  {
    for (const auto& entry : page_table) {
      if (!IsMapped(entry)) {
        continue;
      }
      const auto physical_page = DecodePhysicalPageIndex(entry).value;
      if (std::find(excluded_physical_pages.begin(),
            excluded_physical_pages.end(), physical_page)
        != excluded_physical_pages.end()) {
        continue;
      }

      const auto sample = FindPageSampleIf(pool, dynamic_slice,
        VsmPhysicalPageIndex { .value = physical_page },
        std::string(debug_name) + ".page-" + std::to_string(physical_page),
        [](const float value) { return value < kShadowThreshold; });
      if (sample.has_value()) {
        return sample;
      }
    }

    return std::nullopt;
  }

  auto FindClearTargetSample(const VsmPageAllocationFrame& frame,
    const oxygen::renderer::vsm::VsmPhysicalPoolSnapshot& pool,
    const ShadowSliceSnapshot& dynamic_slice, std::string_view debug_name)
    -> std::optional<PageSample>
  {
    for (const auto& work_item : frame.plan.initialization_work) {
      if (work_item.action != VsmPageInitializationAction::kClearDepth) {
        continue;
      }

      const auto sample = FindPageSampleIf(
        pool, dynamic_slice, work_item.physical_page,
        std::string(debug_name) + ".clear-"
          + std::to_string(work_item.physical_page.value),
        [](const float value) { return value < kShadowThreshold; }, 0U);
      if (sample.has_value()) {
        return sample;
      }
    }

    return std::nullopt;
  }

  auto FindMappedPageSample(
    const std::vector<VsmShaderPageTableEntry>& page_table,
    const oxygen::renderer::vsm::VsmPhysicalPoolSnapshot& pool,
    const ShadowSliceSnapshot& dynamic_slice,
    const std::vector<std::uint32_t>& excluded_physical_pages,
    std::string_view debug_name) -> std::optional<PageSample>
  {
    for (const auto& entry : page_table) {
      if (!IsMapped(entry)) {
        continue;
      }
      const auto physical_page = DecodePhysicalPageIndex(entry).value;
      if (std::find(excluded_physical_pages.begin(),
            excluded_physical_pages.end(), physical_page)
        != excluded_physical_pages.end()) {
        continue;
      }

      const auto sample = FindPageSampleIf(
        pool, dynamic_slice, VsmPhysicalPageIndex { .value = physical_page },
        std::string(debug_name) + ".page-" + std::to_string(physical_page),
        [](const float) { return true; }, 1U);
      if (sample.has_value()) {
        return sample;
      }
    }

    return std::nullopt;
  }

  auto FindCopyTargetSample(const VsmPageAllocationFrame& frame,
    const oxygen::renderer::vsm::VsmPhysicalPoolSnapshot& pool,
    const ShadowSliceSnapshot& dynamic_slice,
    const ShadowSliceSnapshot& static_slice, std::string_view debug_name)
    -> std::optional<CopySample>
  {
    static_cast<void>(debug_name);

    for (const auto& work_item : frame.plan.initialization_work) {
      if (work_item.action != VsmPageInitializationAction::kCopyStaticSlice) {
        continue;
      }

      const auto coord = TryConvertToCoord(work_item.physical_page,
        pool.tile_capacity, pool.tiles_per_axis, pool.slice_count);
      EXPECT_TRUE(coord.has_value());
      if (!coord.has_value()) {
        continue;
      }

      constexpr auto kGrid = 5U;
      for (std::uint32_t grid_y = 0U; grid_y < kGrid; ++grid_y) {
        for (std::uint32_t grid_x = 0U; grid_x < kGrid; ++grid_x) {
          const auto x = coord->tile_x * pool.page_size_texels
            + ((2U * grid_x + 1U) * pool.page_size_texels) / (2U * kGrid);
          const auto y = coord->tile_y * pool.page_size_texels
            + ((2U * grid_y + 1U) * pool.page_size_texels) / (2U * kGrid);
          const auto dynamic_before = dynamic_slice.At(x, y);
          const auto static_before = static_slice.At(x, y);
          if (static_before < kShadowThreshold
            && std::abs(dynamic_before - static_before) > kCopyEpsilon) {
            return CopySample {
              .physical_page = work_item.physical_page,
              .x = x,
              .y = y,
              .dynamic_before = dynamic_before,
              .static_before = static_before,
            };
          }
        }
      }
    }

    return std::nullopt;
  }

  [[nodiscard]] static auto CollectInitializationPages(
    const VsmPageAllocationFrame& frame) -> std::vector<std::uint32_t>
  {
    auto pages = std::vector<std::uint32_t> {};
    pages.reserve(frame.plan.initialization_work.size());
    for (const auto& work_item : frame.plan.initialization_work) {
      pages.push_back(work_item.physical_page.value);
    }
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    return pages;
  }
};

NOLINT_TEST_F(VsmSelectivePageInitializationLiveSceneTest,
  StableDirectionalCachedFrameLeavesStageUntouched)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kFirstSequence = SequenceNumber { 131U };
  constexpr auto kSecondSequence = SequenceNumber { 132U };
  constexpr auto kThirdSequence = SequenceNumber { 133U };

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

  const auto first_frame
    = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer, resolved_view,
      kWidth, kHeight, kFirstSequence, kSlot, 0xB001ULL);
  ASSERT_NE(first_frame.extracted_frame, nullptr);
  ASSERT_EQ(first_frame.virtual_frame.directional_layouts.size(), 1U);
  EXPECT_GT(
    first_frame.virtual_frame.directional_layouts.front().pages_per_axis, 1U);

  const auto second_frame
    = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer, resolved_view,
      kWidth, kHeight, kSecondSequence, kSlot, 0xB001ULL);
  ASSERT_NE(second_frame.extracted_frame, nullptr);

  const auto propagated
    = RunTwoBoxPageFlagPropagationStage(*renderer, scene, vsm_renderer,
      resolved_view, kWidth, kHeight, kThirdSequence, kSlot, 0xB001ULL);
  const auto& frame = propagated.mapping.bridge.committed_frame;
  const auto pool
    = vsm_renderer.GetPhysicalPagePoolManager().GetShadowPoolSnapshot();

  ASSERT_TRUE(frame.plan.initialization_work.empty());
  ASSERT_TRUE(pool.is_available);
  ASSERT_NE(pool.shadow_texture, nullptr);
  const auto dynamic_slice
    = FindSliceIndex(pool, VsmPhysicalPoolSliceRole::kDynamicDepth);
  ASSERT_TRUE(dynamic_slice.has_value());
  const auto dynamic_slice_snapshot = ReadShadowSlice(
    pool.shadow_texture, *dynamic_slice, "stage-eleven.stable.dynamic");

  const auto shadowed_sample
    = FindShadowedMappedPageSample(propagated.page_table, pool,
      dynamic_slice_snapshot, {}, "stage-eleven.stable");
  ASSERT_TRUE(shadowed_sample.has_value());

  const auto buffers_before
    = CaptureStageBuffers(frame, "stage-eleven.stable.before");
  ExecuteInitializationPass(
    oxygen::engine::VsmPageInitializationPassInput {
      .frame = frame,
      .physical_pool = pool,
    },
    "stage-eleven.stable.initialize");

  const auto dynamic_after = ReadShadowSlice(
    pool.shadow_texture, *dynamic_slice, "stage-eleven.stable.after");
  EXPECT_NEAR(dynamic_after.At(shadowed_sample->x, shadowed_sample->y),
    shadowed_sample->value, 1.0e-5F);
  ExpectStageBuffersUnchanged(frame, buffers_before, "stage-eleven.stable");
}

NOLINT_TEST_F(VsmSelectivePageInitializationLiveSceneTest,
  DirectionalClipmapPanClearsFreshPagesWithoutTouchingReusedPages)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kFirstSequence = SequenceNumber { 134U };
  constexpr auto kSecondSequence = SequenceNumber { 135U };
  constexpr auto kThirdSequence = SequenceNumber { 136U };
  constexpr auto kFourthSequence = SequenceNumber { 137U };

  const auto camera_eye = glm::vec3 { -3.2F, 3.4F, 5.8F };
  const auto camera_target = glm::vec3 { 0.2F, 0.8F, 0.0F };
  const auto sun_direction
    = glm::normalize(glm::vec3 { -0.40558F, 0.40558F, 0.819152F });
  const auto first_view
    = MakeLookAtResolvedView(camera_eye, camera_target, kWidth, kHeight);
  const auto candidate_multipliers
    = std::array { 2.0F, 3.0F, 4.0F, -1.0F, -2.0F, -3.0F };
  auto failure_details = std::string {};

  for (const auto multiplier : candidate_multipliers) {
    auto renderer = MakeRenderer();
    ASSERT_NE(renderer, nullptr);
    auto scene = CreateTwoBoxShadowScene(sun_direction, 4U);
    auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
      oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      oxygen::observer_ptr { &renderer->GetStagingProvider() },
      oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
      oxygen::ShadowQualityTier::kHigh);

    const auto first_frame
      = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer, first_view,
        kWidth, kHeight, kFirstSequence, kSlot, 0xB002ULL);
    ASSERT_NE(first_frame.extracted_frame, nullptr);
    ASSERT_EQ(first_frame.virtual_frame.directional_layouts.size(), 1U);

    const auto second_frame
      = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer, first_view,
        kWidth, kHeight, kSecondSequence, kSlot, 0xB002ULL);
    ASSERT_NE(second_frame.extracted_frame, nullptr);
    ASSERT_EQ(second_frame.virtual_frame.directional_layouts.size(), 1U);
    const auto& first_layout
      = second_frame.virtual_frame.directional_layouts.front();

    const auto first_projection
      = std::find_if(second_frame.extracted_frame->projection_records.begin(),
        second_frame.extracted_frame->projection_records.end(),
        [](const auto& record) {
          return record.projection.light_type
            == static_cast<std::uint32_t>(
              oxygen::renderer::vsm::VsmProjectionLightType::kDirectional);
        });
    ASSERT_NE(
      first_projection, second_frame.extracted_frame->projection_records.end());

    const auto light_space_page_shift_ws = glm::vec3(
      glm::inverse(first_projection->projection.view_matrix)
      * glm::vec4 { first_layout.page_world_size[0] * 1.5F, 0.0F, 0.0F, 0.0F });
    const auto precondition_view
      = MakeLookAtResolvedView(camera_eye + light_space_page_shift_ws,
        camera_target + light_space_page_shift_ws, kWidth, kHeight);
    const auto candidate_view = MakeLookAtResolvedView(
      camera_eye + light_space_page_shift_ws * multiplier,
      camera_target + light_space_page_shift_ws * multiplier, kWidth, kHeight);

    const auto precondition_frame
      = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer,
        precondition_view, kWidth, kHeight, kThirdSequence, kSlot, 0xB003ULL);
    ASSERT_NE(precondition_frame.extracted_frame, nullptr);

    const auto propagated
      = RunTwoBoxPageFlagPropagationStage(*renderer, scene, vsm_renderer,
        candidate_view, kWidth, kHeight, kFourthSequence, kSlot, 0xB006ULL);
    const auto& frame = propagated.mapping.bridge.committed_frame;
    const auto& layout = propagated.mapping.bridge.prepared_products
                           .virtual_frame.directional_layouts.front();
    const auto pool
      = vsm_renderer.GetPhysicalPagePoolManager().GetShadowPoolSnapshot();

    ASSERT_TRUE(pool.is_available);
    ASSERT_NE(pool.shadow_texture, nullptr);
    const auto dynamic_slice
      = FindSliceIndex(pool, VsmPhysicalPoolSliceRole::kDynamicDepth);
    ASSERT_TRUE(dynamic_slice.has_value());
    const auto dynamic_before
      = ReadShadowSlice(pool.shadow_texture, *dynamic_slice,
        "stage-eleven.clear.dynamic-before."
          + std::to_string(static_cast<int>(multiplier * 10.0F)));

    const auto clear_count = static_cast<std::size_t>(
      std::count_if(frame.plan.initialization_work.begin(),
        frame.plan.initialization_work.end(), [](const auto& item) {
          return item.action == VsmPageInitializationAction::kClearDepth;
        }));
    if (layout.pages_per_axis <= 1U || frame.plan.allocated_page_count == 0U
      || clear_count <= 1U) {
      failure_details += " multiplier=" + std::to_string(multiplier)
        + " clear_count=" + std::to_string(clear_count)
        + " allocated=" + std::to_string(frame.plan.allocated_page_count)
        + " pages_per_axis=" + std::to_string(layout.pages_per_axis);
      continue;
    }

    const auto clear_target = FindClearTargetSample(frame, pool, dynamic_before,
      "stage-eleven.clear."
        + std::to_string(static_cast<int>(multiplier * 10.0F)));
    if (!clear_target.has_value()) {
      failure_details += " multiplier=" + std::to_string(multiplier)
        + " stale_clear_target=none";
      continue;
    }

    const auto control_sample = FindMappedPageSample(propagated.page_table,
      pool, dynamic_before, CollectInitializationPages(frame),
      "stage-eleven.clear.control."
        + std::to_string(static_cast<int>(multiplier * 10.0F)));
    if (!control_sample.has_value()) {
      failure_details
        += " multiplier=" + std::to_string(multiplier) + " control_sample=none";
      continue;
    }

    const auto buffers_before
      = CaptureStageBuffers(frame, "stage-eleven.clear.before");
    ExecuteInitializationPass(
      oxygen::engine::VsmPageInitializationPassInput {
        .frame = frame,
        .physical_pool = pool,
      },
      "stage-eleven.clear.initialize");

    const auto dynamic_after = ReadShadowSlice(
      pool.shadow_texture, *dynamic_slice, "stage-eleven.clear.dynamic-after");
    EXPECT_NEAR(
      dynamic_after.At(clear_target->x, clear_target->y), 1.0F, 1.0e-5F);
    EXPECT_NEAR(dynamic_after.At(control_sample->x, control_sample->y),
      control_sample->value, 1.0e-5F);
    ExpectStageBuffersUnchanged(frame, buffers_before, "stage-eleven.clear");
    return;
  }

  FAIL() << "No real clear scenario with stale page data found across "
            "candidate page pans."
         << failure_details;
}

NOLINT_TEST_F(VsmSelectivePageInitializationLiveSceneTest,
  DynamicOnlyInvalidationCopiesStaticSliceIntoRealDirectionalPages)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kFirstSequence = SequenceNumber { 137U };
  constexpr auto kSecondSequence = SequenceNumber { 138U };
  constexpr auto kThirdSequence = SequenceNumber { 139U };

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
  MoveShortBox(scene, glm::vec3 { 1.20F, 0.0F, -0.05F });

  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  const auto first_frame
    = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer, resolved_view,
      kWidth, kHeight, kFirstSequence, kSlot, 0xB004ULL);
  ASSERT_NE(first_frame.extracted_frame, nullptr);

  const auto second_frame
    = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer, resolved_view,
      kWidth, kHeight, kSecondSequence, kSlot, 0xB004ULL);
  ASSERT_NE(second_frame.extracted_frame, nullptr);
  ASSERT_EQ(second_frame.virtual_frame.directional_layouts.size(), 1U);
  const auto directional_key
    = second_frame.virtual_frame.directional_layouts.front().remap_key;
  vsm_renderer.GetCacheManager().InvalidateDirectionalClipmaps(
    { directional_key }, VsmCacheInvalidationScope::kDynamicOnly,
    VsmCacheInvalidationReason::kTargetedInvalidate);

  const auto propagated
    = RunTwoBoxPageFlagPropagationStage(*renderer, scene, vsm_renderer,
      resolved_view, kWidth, kHeight, kThirdSequence, kSlot, 0xB005ULL);
  const auto& frame = propagated.mapping.bridge.committed_frame;
  const auto& layout = propagated.mapping.bridge.prepared_products.virtual_frame
                         .directional_layouts.front();
  const auto pool
    = vsm_renderer.GetPhysicalPagePoolManager().GetShadowPoolSnapshot();

  ASSERT_TRUE(pool.is_available);
  ASSERT_NE(pool.shadow_texture, nullptr);
  const auto dynamic_slice
    = FindSliceIndex(pool, VsmPhysicalPoolSliceRole::kDynamicDepth);
  const auto static_slice
    = FindSliceIndex(pool, VsmPhysicalPoolSliceRole::kStaticDepth);
  ASSERT_TRUE(dynamic_slice.has_value());
  ASSERT_TRUE(static_slice.has_value());
  const auto dynamic_before = ReadShadowSlice(
    pool.shadow_texture, *dynamic_slice, "stage-eleven.copy.dynamic-before");
  const auto static_before = ReadShadowSlice(
    pool.shadow_texture, *static_slice, "stage-eleven.copy.static-before");

  const auto copy_count = static_cast<std::size_t>(
    std::count_if(frame.plan.initialization_work.begin(),
      frame.plan.initialization_work.end(), [](const auto& item) {
        return item.action == VsmPageInitializationAction::kCopyStaticSlice;
      }));
  EXPECT_GT(layout.pages_per_axis, 1U);
  EXPECT_GT(copy_count, 0U);

  const auto copy_target = FindCopyTargetSample(
    frame, pool, dynamic_before, static_before, "stage-eleven.copy");
  ASSERT_TRUE(copy_target.has_value());
  EXPECT_GT(std::abs(copy_target->dynamic_before - copy_target->static_before),
    kCopyEpsilon);

  const auto buffers_before
    = CaptureStageBuffers(frame, "stage-eleven.copy.before");
  ExecuteInitializationPass(
    oxygen::engine::VsmPageInitializationPassInput {
      .frame = frame,
      .physical_pool = pool,
    },
    "stage-eleven.copy.initialize");

  const auto static_after = ReadShadowSlice(
    pool.shadow_texture, *static_slice, "stage-eleven.copy.static-after");
  const auto dynamic_after = ReadShadowSlice(
    pool.shadow_texture, *dynamic_slice, "stage-eleven.copy.dynamic-after");
  EXPECT_NEAR(static_after.At(copy_target->x, copy_target->y),
    copy_target->static_before, 1.0e-5F);
  EXPECT_NEAR(dynamic_after.At(copy_target->x, copy_target->y),
    copy_target->static_before, 1.0e-5F);
  ExpectStageBuffersUnchanged(frame, buffers_before, "stage-eleven.copy");
}

} // namespace
