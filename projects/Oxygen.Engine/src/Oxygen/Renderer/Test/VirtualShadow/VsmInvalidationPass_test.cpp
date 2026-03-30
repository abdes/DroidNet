//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmInvalidationPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>

#include "VirtualShadowGpuTestFixtures.h"

namespace {

using oxygen::NdcDepthRange;
using oxygen::ResolvedView;
using oxygen::Scissors;
using oxygen::View;
using oxygen::ViewId;
using oxygen::ViewPort;
using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::VsmInvalidationPass;
using oxygen::engine::VsmInvalidationPassConfig;
using oxygen::engine::VsmInvalidationPassInput;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::Buffer;
using oxygen::renderer::vsm::MakeMappedShaderPageTableEntry;
using oxygen::renderer::vsm::MakeUnmappedShaderPageTableEntry;
using oxygen::renderer::vsm::VsmCacheInvalidationScope;
using oxygen::renderer::vsm::VsmInvalidationWorkItem;
using oxygen::renderer::vsm::VsmPageRequestProjection;
using oxygen::renderer::vsm::VsmPhysicalPageMeta;
using oxygen::renderer::vsm::VsmPrimitiveIdentity;
using oxygen::renderer::vsm::VsmProjectionData;
using oxygen::renderer::vsm::VsmProjectionLightType;
using oxygen::renderer::vsm::VsmShaderPageTableEntry;
using oxygen::renderer::vsm::testing::VirtualShadowGpuTest;

constexpr ViewId kTestViewId { 29U };

class VsmInvalidationPassGpuTest : public VirtualShadowGpuTest {
protected:
  [[nodiscard]] static auto MakeResolvedView() -> ResolvedView
  {
    auto view_config = View {};
    view_config.viewport = ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = 1.0F,
      .height = 1.0F,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    view_config.scissor = Scissors {
      .left = 0,
      .top = 0,
      .right = 1,
      .bottom = 1,
    };

    return ResolvedView(ResolvedView::Params {
      .view_config = view_config,
      .view_matrix = glm::mat4 { 1.0F },
      .proj_matrix = glm::mat4 { 1.0F },
      .camera_position = glm::vec3 { 0.0F, 0.0F, 0.0F },
      .depth_range = NdcDepthRange::ZeroToOne,
      .near_plane = 0.1F,
      .far_plane = 100.0F,
    });
  }

  [[nodiscard]] static auto MakeProjectionRecord(const std::uint32_t map_id,
    const std::uint32_t first_page_table_entry, const std::uint32_t pages_x,
    const std::uint32_t pages_y = 1U, const std::uint32_t clipmap_level = 0U)
    -> VsmPageRequestProjection
  {
    return VsmPageRequestProjection {
      .projection = VsmProjectionData {
        .view_matrix = glm::mat4 { 1.0F },
        .projection_matrix = glm::mat4 { 1.0F },
        .view_origin_ws_pad = { 0.0F, 0.0F, 0.0F, 0.0F },
        .clipmap_corner_offset = { 0, 0 },
        .clipmap_level = clipmap_level,
        .light_type
        = static_cast<std::uint32_t>(VsmProjectionLightType::kLocal),
      },
      .map_id = map_id,
      .first_page_table_entry = first_page_table_entry,
      .map_pages_x = pages_x,
      .map_pages_y = pages_y,
      .pages_x = pages_x,
      .pages_y = pages_y,
      .page_offset_x = 0U,
      .page_offset_y = 0U,
      .level_count = clipmap_level + 1U,
      .coarse_level = 0U,
      .light_index = 0U,
    };
  }

  [[nodiscard]] static auto MakePhysicalMeta(std::uint32_t owner_id)
    -> VsmPhysicalPageMeta
  {
    return VsmPhysicalPageMeta {
      .is_allocated = true,
      .owner_id = owner_id,
      .owner_mip_level = 0U,
      .owner_page = {},
    };
  }

  auto ExecutePass(VsmInvalidationPass& pass, std::string_view debug_name)
    -> void
  {
    auto renderer = MakeRenderer();
    CHECK_NOTNULL_F(renderer.get());

    auto prepared_frame = PreparedSceneFrame {};
    auto offscreen = renderer->BeginOffscreenFrame(
      { .frame_slot = Slot { 0U }, .frame_sequence = SequenceNumber { 1U } });
    offscreen.SetCurrentView(kTestViewId, MakeResolvedView(), prepared_frame);
    auto& render_context = offscreen.GetRenderContext();

    {
      auto recorder = AcquireRecorder(std::string(debug_name));
      CHECK_NOTNULL_F(recorder.get());
      RunPass(pass, render_context, *recorder);
    }
    WaitForQueueIdle();
  }

  template <typename T>
  auto ReadBufferAs(const std::shared_ptr<const Buffer>& buffer,
    const std::size_t element_count, std::string_view debug_name)
    -> std::vector<T>
  {
    const auto bytes = ReadBufferBytes(std::const_pointer_cast<Buffer>(buffer),
      element_count * sizeof(T), debug_name);
    auto result = std::vector<T>(element_count);
    std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
  }
};

NOLINT_TEST_F(VsmInvalidationPassGpuTest,
  MarksOnlyOverlappedMappedPagesAndLeavesOtherPagesUntouched)
{
  auto pass
    = VsmInvalidationPass(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<VsmInvalidationPassConfig>(VsmInvalidationPassConfig {
        .debug_name = "phase-j-invalidation-overlap",
      }));

  auto input = VsmInvalidationPassInput {};
  input.previous_projection_records
    = { MakeProjectionRecord(11U, 0U, 2U, 1U, 0U) };
  input.previous_page_table_entries = {
    MakeMappedShaderPageTableEntry({ 0U }),
    MakeMappedShaderPageTableEntry({ 1U }),
  };
  input.previous_physical_page_metadata
    = { MakePhysicalMeta(11U), MakePhysicalMeta(11U) };
  input.invalidation_work_items = {
    VsmInvalidationWorkItem {
      .primitive = VsmPrimitiveIdentity {
        .transform_index = 7U,
        .transform_generation = 2U,
        .submesh_index = 0U,
      },
      .world_bounding_sphere = glm::vec4 { -0.75F, 0.0F, 0.0F, 0.10F },
      .projection_index = 0U,
      .scope = VsmCacheInvalidationScope::kDynamicOnly,
    },
  };
  pass.SetInput(std::move(input));

  ExecutePass(pass, "phase-j-invalidation-overlap");

  const auto metadata = ReadBufferAs<VsmPhysicalPageMeta>(
    pass.GetCurrentOutputPhysicalMetadataBuffer(), 2U,
    "phase-j-invalidation-overlap.readback");

  ASSERT_EQ(metadata.size(), 2U);
  EXPECT_FALSE(static_cast<bool>(metadata[0].static_invalidated));
  EXPECT_TRUE(static_cast<bool>(metadata[0].dynamic_invalidated));
  EXPECT_FALSE(static_cast<bool>(metadata[1].static_invalidated));
  EXPECT_FALSE(static_cast<bool>(metadata[1].dynamic_invalidated));
}

NOLINT_TEST_F(VsmInvalidationPassGpuTest,
  RespectsScopeFlagsAndIgnoresUnmappedOrOutOfRangeWorkItems)
{
  auto pass
    = VsmInvalidationPass(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<VsmInvalidationPassConfig>(VsmInvalidationPassConfig {
        .debug_name = "phase-j-invalidation-scope",
      }));

  auto input = VsmInvalidationPassInput {};
  input.previous_projection_records
    = { MakeProjectionRecord(17U, 0U, 2U, 1U, 0U) };
  input.previous_page_table_entries = {
    MakeUnmappedShaderPageTableEntry(),
    MakeMappedShaderPageTableEntry({ 1U }),
  };
  input.previous_physical_page_metadata
    = { MakePhysicalMeta(17U), MakePhysicalMeta(17U) };
  input.invalidation_work_items = {
    VsmInvalidationWorkItem {
      .primitive = VsmPrimitiveIdentity {
        .transform_index = 9U,
        .transform_generation = 1U,
        .submesh_index = 0U,
      },
      .world_bounding_sphere = glm::vec4 { 0.75F, 0.0F, 0.0F, 0.10F },
      .projection_index = 0U,
      .scope = VsmCacheInvalidationScope::kStaticOnly,
      .matched_static_feedback = true,
    },
    VsmInvalidationWorkItem {
      .primitive = VsmPrimitiveIdentity {
        .transform_index = 10U,
        .transform_generation = 1U,
        .submesh_index = 0U,
      },
      .world_bounding_sphere = glm::vec4 { -0.75F, 0.0F, 0.0F, 0.10F },
      .projection_index = 0U,
      .scope = VsmCacheInvalidationScope::kStaticAndDynamic,
    },
    VsmInvalidationWorkItem {
      .primitive = VsmPrimitiveIdentity {
        .transform_index = 11U,
        .transform_generation = 1U,
        .submesh_index = 0U,
      },
      .world_bounding_sphere = glm::vec4 { 0.75F, 0.0F, 0.0F, 0.10F },
      .projection_index = 4U,
      .scope = VsmCacheInvalidationScope::kStaticAndDynamic,
    },
  };
  pass.SetInput(std::move(input));

  ExecutePass(pass, "phase-j-invalidation-scope");

  const auto metadata = ReadBufferAs<VsmPhysicalPageMeta>(
    pass.GetCurrentOutputPhysicalMetadataBuffer(), 2U,
    "phase-j-invalidation-scope.readback");

  ASSERT_EQ(metadata.size(), 2U);
  EXPECT_FALSE(static_cast<bool>(metadata[0].static_invalidated));
  EXPECT_FALSE(static_cast<bool>(metadata[0].dynamic_invalidated));
  EXPECT_TRUE(static_cast<bool>(metadata[1].static_invalidated));
  EXPECT_FALSE(static_cast<bool>(metadata[1].dynamic_invalidated));
}

} // namespace
