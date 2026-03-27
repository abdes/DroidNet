//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmPageRequestGeneratorPass.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPageRequestGeneration.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmProjectionRouting.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmProjectionTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpaceTypes.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>

#include "VirtualShadowLiveSceneHarness.h"

namespace {

using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::renderer::vsm::BuildPageRequests;
using oxygen::renderer::vsm::HasAnyRequestFlag;
using oxygen::renderer::vsm::TryComputePageTableIndex;
using oxygen::renderer::vsm::VsmPageRequestProjection;
using oxygen::renderer::vsm::VsmProjectionLightType;
using oxygen::renderer::vsm::VsmShaderPageRequestFlagBits;
using oxygen::renderer::vsm::VsmShaderPageRequestFlags;
using oxygen::renderer::vsm::VsmShadowRenderer;
using oxygen::renderer::vsm::VsmVirtualMapLayout;
using oxygen::renderer::vsm::VsmVisiblePixelSample;
using oxygen::renderer::vsm::testing::VsmLiveSceneHarness;

struct AxisAlignedBox {
  glm::vec3 min {};
  glm::vec3 max {};
};

auto DescribeNonZeroRequestFlags(const VsmShaderPageRequestFlags* flags,
  const std::uint32_t count) -> std::string
{
  auto result = std::string {};
  for (std::uint32_t index = 0U; index < count; ++index) {
    if (flags[index].bits == 0U) {
      continue;
    }
    if (!result.empty()) {
      result += ", ";
    }
    result += std::to_string(index);
    result += "=";
    result += std::to_string(flags[index].bits);
  }
  return result.empty() ? std::string { "<none>" } : result;
}

auto BuildExpectedRequestFlags(
  const std::vector<VsmPageRequestProjection>& projections,
  const std::vector<VsmVisiblePixelSample>& samples,
  const std::uint32_t virtual_page_count)
  -> std::vector<VsmShaderPageRequestFlags>
{
  auto flags = std::vector<VsmShaderPageRequestFlags>(virtual_page_count);
  const auto requests = BuildPageRequests(projections, samples);
  for (const auto& request : requests) {
    auto index = std::optional<std::uint32_t> {};
    const VsmPageRequestProjection* matched_projection = nullptr;
    for (const auto& projection : projections) {
      if (projection.map_id != request.map_id) {
        continue;
      }

      index = TryComputePageTableIndex(projection, request.page);
      if (index.has_value()) {
        matched_projection = &projection;
        break;
      }
    }
    CHECK_F(index.has_value(), "Missing projection route for map_id {}",
      request.map_id);
    CHECK_NOTNULL_F(matched_projection, "Missing matched projection");
    CHECK_F(*index < virtual_page_count,
      "Expected request index {} exceeds virtual page count {}", *index,
      virtual_page_count);

    flags[*index].bits
      |= static_cast<std::uint32_t>(VsmShaderPageRequestFlagBits::kRequired);
    const auto is_directional = matched_projection->projection.light_type
      == static_cast<std::uint32_t>(VsmProjectionLightType::kDirectional);
    if (!is_directional && request.page.level != 0U) {
      flags[*index].bits
        |= static_cast<std::uint32_t>(VsmShaderPageRequestFlagBits::kCoarse);
    }
  }
  return flags;
}

auto RaycastDistanceToAabb(const glm::vec3 origin, const glm::vec3 direction,
  const AxisAlignedBox& box) -> std::optional<float>
{
  auto t_min = 0.0F;
  auto t_max = std::numeric_limits<float>::infinity();

  for (auto axis = 0; axis < 3; ++axis) {
    const auto dir = direction[axis];
    if (std::abs(dir) < 1.0e-6F) {
      if (origin[axis] < box.min[axis] || origin[axis] > box.max[axis]) {
        return std::nullopt;
      }
      continue;
    }

    auto t1 = (box.min[axis] - origin[axis]) / dir;
    auto t2 = (box.max[axis] - origin[axis]) / dir;
    if (t1 > t2) {
      std::swap(t1, t2);
    }
    t_min = std::max(t_min, t1);
    t_max = std::min(t_max, t2);
    if (t_min > t_max) {
      return std::nullopt;
    }
  }

  return t_max >= std::max(0.0F, t_min) ? std::optional<float> { t_min }
                                        : std::nullopt;
}

auto RaycastDistanceToFloor(const glm::vec3 origin, const glm::vec3 direction,
  const float half_extent = 4.5F) -> std::optional<float>
{
  if (std::abs(direction.y) < 1.0e-6F) {
    return std::nullopt;
  }

  const auto distance = -origin.y / direction.y;
  if (distance <= 0.0F) {
    return std::nullopt;
  }

  const auto hit = origin + direction * distance;
  if (std::abs(hit.x) > half_extent || std::abs(hit.z) > half_extent) {
    return std::nullopt;
  }
  return distance;
}

auto BuildTwoBoxVisibleSamples(const oxygen::ResolvedView& resolved_view,
  const std::uint32_t width, const std::uint32_t height)
  -> std::vector<VsmVisiblePixelSample>
{
  constexpr auto tall_box = AxisAlignedBox {
    .min = glm::vec3 { 0.55F, 0.0F, -0.65F },
    .max = glm::vec3 { 1.35F, 3.2F, 0.15F },
  };
  constexpr auto short_box = AxisAlignedBox {
    .min = glm::vec3 { -0.95F, 0.0F, 0.25F },
    .max = glm::vec3 { -0.15F, 1.1F, 1.05F },
  };

  auto visible_samples = std::vector<VsmVisiblePixelSample> {};
  visible_samples.reserve(static_cast<std::size_t>(width) * height);

  const auto inverse_view_projection = glm::inverse(
    resolved_view.ProjectionMatrix() * resolved_view.ViewMatrix());
  auto pixel_center_ray = [&](const std::uint32_t x, const std::uint32_t y) {
    const auto ndc_x
      = (2.0F * (static_cast<float>(x) + 0.5F) / static_cast<float>(width))
      - 1.0F;
    const auto ndc_y = 1.0F
      - (2.0F * (static_cast<float>(y) + 0.5F) / static_cast<float>(height));
    auto near_point
      = inverse_view_projection * glm::vec4 { ndc_x, ndc_y, 0.0F, 1.0F };
    auto far_point
      = inverse_view_projection * glm::vec4 { ndc_x, ndc_y, 1.0F, 1.0F };
    near_point /= near_point.w;
    far_point /= far_point.w;
    const auto origin = glm::vec3 { near_point };
    const auto direction = glm::normalize(glm::vec3 { far_point - near_point });
    return std::pair { origin, direction };
  };

  for (std::uint32_t y = 0U; y < height; ++y) {
    for (std::uint32_t x = 0U; x < width; ++x) {
      const auto [origin, direction] = pixel_center_ray(x, y);
      auto nearest_distance = std::numeric_limits<float>::infinity();
      auto hit_point = std::optional<glm::vec3> {};

      if (const auto floor_distance = RaycastDistanceToFloor(origin, direction);
        floor_distance.has_value() && *floor_distance < nearest_distance) {
        nearest_distance = *floor_distance;
        hit_point = origin + direction * *floor_distance;
      }
      if (const auto tall_distance
        = RaycastDistanceToAabb(origin, direction, tall_box);
        tall_distance.has_value() && *tall_distance < nearest_distance) {
        nearest_distance = *tall_distance;
        hit_point = origin + direction * *tall_distance;
      }
      if (const auto short_distance
        = RaycastDistanceToAabb(origin, direction, short_box);
        short_distance.has_value() && *short_distance < nearest_distance) {
        nearest_distance = *short_distance;
        hit_point = origin + direction * *short_distance;
      }

      if (hit_point.has_value()) {
        visible_samples.push_back(
          VsmVisiblePixelSample { .world_position_ws = *hit_point });
      }
    }
  }

  return visible_samples;
}

class VsmPageRequestLiveSceneTest : public VsmLiveSceneHarness {
protected:
  auto ReadUploadedProjectionRecords(VsmShadowRenderer& renderer,
    const std::size_t projection_count, std::string_view debug_name)
    -> std::vector<VsmPageRequestProjection>
  {
    const auto request_pass = renderer.GetPageRequestGeneratorPass();
    EXPECT_NE(request_pass, nullptr);
    if (request_pass == nullptr) {
      return {};
    }
    const auto buffer = request_pass->GetProjectionBuffer();
    EXPECT_NE(buffer, nullptr);
    if (buffer == nullptr) {
      return {};
    }
    return ReadBufferAs<VsmPageRequestProjection>(
      *buffer, projection_count, debug_name);
  }

  auto ReadRequestFlags(VsmShadowRenderer& renderer,
    const std::uint32_t virtual_page_count, std::string_view debug_name)
    -> std::vector<VsmShaderPageRequestFlags>
  {
    const auto request_pass = renderer.GetPageRequestGeneratorPass();
    EXPECT_NE(request_pass, nullptr);
    if (request_pass == nullptr) {
      return {};
    }
    const auto buffer = request_pass->GetPageRequestFlagsBuffer();
    EXPECT_NE(buffer, nullptr);
    if (buffer == nullptr) {
      return {};
    }
    return ReadBufferAs<VsmShaderPageRequestFlags>(
      *buffer, virtual_page_count, debug_name);
  }

  static auto ExpectFlagsEqual(
    const std::vector<VsmShaderPageRequestFlags>& actual,
    const std::vector<VsmShaderPageRequestFlags>& expected) -> void
  {
    ASSERT_EQ(actual.size(), expected.size());
    if (!std::equal(actual.begin(), actual.end(), expected.begin(),
          expected.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.bits == rhs.bits;
          })) {
      ADD_FAILURE() << "actual non-zero flags: "
                    << DescribeNonZeroRequestFlags(actual.data(),
                         static_cast<std::uint32_t>(actual.size()))
                    << ", expected non-zero flags: "
                    << DescribeNonZeroRequestFlags(expected.data(),
                         static_cast<std::uint32_t>(expected.size()));
    }
    for (std::size_t index = 0; index < actual.size(); ++index) {
      EXPECT_EQ(actual[index].bits, expected[index].bits) << index;
    }
  }

  static auto FindLocalProjectionAtOrigin(
    const std::span<const VsmPageRequestProjection> projections,
    const glm::vec3& position_ws) -> const VsmPageRequestProjection*
  {
    const auto it = std::find_if(
      projections.begin(), projections.end(), [&](const auto& projection) {
        if (projection.projection.light_type
          != static_cast<std::uint32_t>(VsmProjectionLightType::kLocal)) {
          return false;
        }
        return glm::distance(
                 glm::vec3 { projection.projection.view_origin_ws_pad },
                 position_ws)
          < 1.0e-3F;
      });
    return it != projections.end() ? &(*it) : nullptr;
  }

  static auto FindLocalLayout(
    const std::span<const VsmVirtualMapLayout> layouts,
    const std::uint32_t map_id) -> const VsmVirtualMapLayout*
  {
    const auto it = std::find_if(layouts.begin(), layouts.end(),
      [&](const auto& layout) { return layout.id == map_id; });
    return it != layouts.end() ? &(*it) : nullptr;
  }

  static auto CountNonZeroFlagsInRange(
    const std::span<const VsmShaderPageRequestFlags> flags,
    const std::uint32_t first_entry, const std::uint32_t entry_count)
    -> std::size_t
  {
    return static_cast<std::size_t>(std::count_if(flags.begin() + first_entry,
      flags.begin() + first_entry + entry_count,
      [](const auto& flag) { return flag.bits != 0U; }));
  }

  [[nodiscard]] static auto MakeResolvedView(const std::uint32_t width = 1U,
    const std::uint32_t height = 1U) -> oxygen::ResolvedView
  {
    auto view_config = oxygen::View {};
    view_config.viewport = oxygen::ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(width),
      .height = static_cast<float>(height),
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    view_config.scissor = oxygen::Scissors {
      .left = 0,
      .top = 0,
      .right = static_cast<std::int32_t>(width),
      .bottom = static_cast<std::int32_t>(height),
    };

    return oxygen::ResolvedView(oxygen::ResolvedView::Params {
      .view_config = view_config,
      .view_matrix = glm::lookAtRH(glm::vec3 { 0.0F, 0.0F, 0.0F },
        glm::vec3 { 0.0F, 0.0F, -1.0F }, glm::vec3 { 0.0F, 1.0F, 0.0F }),
      .proj_matrix = glm::perspectiveRH_ZO(glm::radians(90.0F),
        static_cast<float>(width) / static_cast<float>(height), 0.1F, 100.0F),
      .camera_position = glm::vec3 { 0.0F, 0.0F, 0.0F },
      .depth_range = oxygen::NdcDepthRange::ZeroToOne,
      .near_plane = 0.1F,
      .far_plane = 100.0F,
    });
  }

  [[nodiscard]] static auto MakeLocalProjection(const std::uint32_t map_id,
    const std::uint32_t first_page_table_entry,
    const std::uint32_t light_index
    = oxygen::renderer::vsm::kVsmInvalidLightIndex,
    const std::uint32_t level_count = 1U, const std::uint32_t coarse_level = 0U,
    const std::uint32_t map_pages_x = 4U, const std::uint32_t map_pages_y = 4U)
    -> VsmPageRequestProjection
  {
    const auto resolved_view = MakeResolvedView();
    return VsmPageRequestProjection {
      .projection = oxygen::renderer::vsm::VsmProjectionData {
        .view_matrix = resolved_view.ViewMatrix(),
        .projection_matrix = resolved_view.ProjectionMatrix(),
        .view_origin_ws_pad = glm::vec4 { 0.0F, 0.0F, 0.0F, 0.0F },
        .clipmap_corner_offset = { 0, 0 },
        .clipmap_level = 0U,
        .light_type
        = static_cast<std::uint32_t>(VsmProjectionLightType::kLocal),
      },
      .map_id = map_id,
      .first_page_table_entry = first_page_table_entry,
      .map_pages_x = map_pages_x,
      .map_pages_y = map_pages_y,
      .pages_x = map_pages_x,
      .pages_y = map_pages_y,
      .page_offset_x = 0U,
      .page_offset_y = 0U,
      .level_count = level_count,
      .coarse_level = coarse_level,
      .light_index = light_index,
      .cube_face_index = oxygen::renderer::vsm::kVsmInvalidCubeFaceIndex,
    };
  }

  [[nodiscard]] static auto MakeDirectionalProjection(
    const std::uint32_t map_id, const std::uint32_t first_page_table_entry,
    const std::uint32_t clipmap_level, const std::uint32_t level_count,
    const std::uint32_t map_pages_x = 4U, const std::uint32_t map_pages_y = 4U)
    -> VsmPageRequestProjection
  {
    const auto resolved_view = MakeResolvedView();
    return VsmPageRequestProjection {
      .projection = oxygen::renderer::vsm::VsmProjectionData {
        .view_matrix = resolved_view.ViewMatrix(),
        .projection_matrix = resolved_view.ProjectionMatrix(),
        .view_origin_ws_pad = glm::vec4 { 0.0F, 0.0F, 0.0F, 0.0F },
        .clipmap_corner_offset = { 0, 0 },
        .clipmap_level = clipmap_level,
        .light_type
        = static_cast<std::uint32_t>(VsmProjectionLightType::kDirectional),
      },
      .map_id = map_id,
      .first_page_table_entry = first_page_table_entry,
      .map_pages_x = map_pages_x,
      .map_pages_y = map_pages_y,
      .pages_x = map_pages_x,
      .pages_y = map_pages_y,
      .page_offset_x = 0U,
      .page_offset_y = 0U,
      .level_count = level_count,
      .coarse_level = 0U,
      .light_index = oxygen::renderer::vsm::kVsmInvalidLightIndex,
      .cube_face_index = oxygen::renderer::vsm::kVsmInvalidCubeFaceIndex,
    };
  }

  [[nodiscard]] static auto ComputeDepthForWorldPoint(
    const oxygen::ResolvedView& resolved_view, const glm::vec3& world_position)
    -> float
  {
    const auto clip = resolved_view.ProjectionMatrix()
      * resolved_view.ViewMatrix() * glm::vec4(world_position, 1.0F);
    return clip.z / clip.w;
  }

  [[nodiscard]] static auto ComputeWorldPointForPixel(
    const oxygen::ResolvedView& view, const std::uint32_t width,
    const std::uint32_t height, const std::uint32_t pixel_x,
    const std::uint32_t pixel_y, const float depth) -> glm::vec3
  {
    const auto uv = glm::vec2 {
      (static_cast<float>(pixel_x) + 0.5F) / static_cast<float>(width),
      (static_cast<float>(pixel_y) + 0.5F) / static_cast<float>(height),
    };
    const auto ndc_xy = glm::vec2 { uv.x * 2.0F - 1.0F, 1.0F - uv.y * 2.0F };
    const auto clip = glm::vec4 { ndc_xy, depth, 1.0F };
    const auto inverse_view_projection
      = glm::inverse(view.ProjectionMatrix() * view.ViewMatrix());
    const auto world = inverse_view_projection * clip;
    return glm::vec3(world) / std::max(world.w, 1.0e-6F);
  }

  static auto CollectSinglePointLight(
    oxygen::engine::Renderer& renderer, const glm::vec3& world_position) -> void
  {
    auto scene = std::make_shared<oxygen::scene::Scene>("VsmLightScene", 16U);
    const auto flags
      = oxygen::scene::SceneNode::Flags {}
          .SetFlag(oxygen::scene::SceneNodeFlags::kVisible,
            oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true))
          .SetFlag(oxygen::scene::SceneNodeFlags::kCastsShadows,
            oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true));
    auto node = scene->CreateNode("point-light", flags);
    ASSERT_TRUE(node.IsValid());
    ASSERT_TRUE(node.GetTransform().SetLocalPosition(world_position));
    scene->Update();

    auto impl = node.GetImpl();
    ASSERT_TRUE(impl.has_value());
    auto& point_light = impl->get().AddComponent<oxygen::scene::PointLight>();
    point_light.Common().casts_shadows = true;
    point_light.SetRange(12.0F);

    auto light_manager = renderer.GetLightManager();
    ASSERT_NE(light_manager, nullptr);
    if (light_manager == nullptr) {
      return;
    }
    light_manager->CollectFromNode(node.GetHandle(), impl->get());
  }
};

NOLINT_TEST_F(VsmPageRequestLiveSceneTest,
  DirectionalTwoBoxScenePublishesUploadedProjectionRecordsAndExpectedRequestFlags)
{
  constexpr auto kWidth = 256U;
  constexpr auto kHeight = 256U;
  constexpr auto kSlot = Slot { 0U };
  constexpr auto kSequence = SequenceNumber { 71U };

  const auto camera_eye = glm::vec3 { -3.2F, 3.4F, 5.8F };
  const auto camera_target = glm::vec3 { 0.2F, 0.8F, 0.0F };
  const auto sun_direction
    = glm::normalize(glm::vec3 { -0.40558F, 0.40558F, 0.819152F });
  const auto resolved_view
    = MakeLookAtResolvedView(camera_eye, camera_target, kWidth, kHeight);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene = CreateTwoBoxShadowScene(sun_direction, 4U);
  auto vsm_renderer
    = VsmShadowRenderer(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      oxygen::observer_ptr { &renderer->GetStagingProvider() },
      oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
      oxygen::ShadowQualityTier::kHigh);

  const auto result = RunTwoBoxLiveShellFrame(*renderer, scene, vsm_renderer,
    resolved_view, kWidth, kHeight, kSequence, kSlot, 0x5001ULL);
  ASSERT_NE(result.extracted_frame, nullptr);
  ASSERT_EQ(result.virtual_frame.directional_layouts.size(), 1U);
  ASSERT_TRUE(result.virtual_frame.local_light_layouts.empty());

  const auto uploaded_projection_records = ReadUploadedProjectionRecords(
    vsm_renderer, result.extracted_frame->projection_records.size(),
    "vsm-stage-five.directional.projections");
  EXPECT_EQ(
    uploaded_projection_records, result.extracted_frame->projection_records);

  const auto virtual_page_count
    = static_cast<std::uint32_t>(result.extracted_frame->page_table.size());
  const auto actual_flags = ReadRequestFlags(
    vsm_renderer, virtual_page_count, "vsm-stage-five.directional.flags");
  const auto visible_samples
    = BuildTwoBoxVisibleSamples(resolved_view, kWidth, kHeight);
  ASSERT_FALSE(visible_samples.empty());

  const auto expected_flags
    = BuildExpectedRequestFlags(result.extracted_frame->projection_records,
      visible_samples, virtual_page_count);
  ExpectFlagsEqual(actual_flags, expected_flags);

  const auto expected_requests = BuildPageRequests(
    result.extracted_frame->projection_records, visible_samples);
  auto requested_map_ids = std::vector<std::uint32_t> {};
  requested_map_ids.reserve(expected_requests.size());
  for (const auto& request : expected_requests) {
    requested_map_ids.push_back(request.map_id);
  }
  std::sort(requested_map_ids.begin(), requested_map_ids.end());
  requested_map_ids.erase(
    std::unique(requested_map_ids.begin(), requested_map_ids.end()),
    requested_map_ids.end());
  EXPECT_GE(requested_map_ids.size(), 2U);
  EXPECT_GT(std::count_if(actual_flags.begin(), actual_flags.end(),
              [](const auto& flag) { return flag.bits != 0U; }),
    4);
}

NOLINT_TEST_F(VsmPageRequestLiveSceneTest,
  MultiLevelLocalProjectionPublishesFineAndCoarseFlagsForRealDepthField)
{
  constexpr auto kWidth = 4U;
  constexpr auto kHeight = 4U;
  constexpr float kDepth = 0.5F;
  constexpr std::uint32_t kPagesX = 2U;
  constexpr std::uint32_t kPagesY = 2U;
  constexpr std::uint32_t kVirtualPageCount = 64U;

  const auto resolved_view = MakeResolvedView(kWidth, kHeight);
  auto depth_texture = CreateUploadedFloatTexture2D(
    std::vector<float>(static_cast<std::size_t>(kWidth) * kHeight, kDepth),
    kWidth, kHeight, oxygen::Format::kR32Float, "vsm-stage-five.local-depth");

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto depth_pass = oxygen::engine::DepthPrePass(
    std::make_shared<oxygen::engine::DepthPrePass::Config>(
      oxygen::engine::DepthPrePass::Config { .depth_texture = depth_texture }));
  auto request_pass = oxygen::engine::VsmPageRequestGeneratorPass(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    std::make_shared<oxygen::engine::VsmPageRequestGeneratorPassConfig>(
      oxygen::engine::VsmPageRequestGeneratorPassConfig {
        .max_projection_count = 1U,
        .max_virtual_page_count = kVirtualPageCount,
        .enable_coarse_pages = true,
        .enable_light_grid_pruning = false,
        .debug_name = "VsmPageRequestLocalCoarse",
      }));
  const auto projections = std::vector<VsmPageRequestProjection> {
    MakeLocalProjection(31U, 0U, oxygen::renderer::vsm::kVsmInvalidLightIndex,
      2U, 1U, kPagesX, kPagesY),
  };
  request_pass.SetFrameInputs(projections, kVirtualPageCount);

  auto prepared_frame = oxygen::engine::PreparedSceneFrame {};
  auto offscreen = renderer->BeginOffscreenFrame(
    { .frame_slot = Slot { 0U }, .frame_sequence = SequenceNumber { 72U } });
  offscreen.SetCurrentView(kTestViewId, resolved_view, prepared_frame);
  auto& render_context = offscreen.GetRenderContext();
  render_context.RegisterPass(&depth_pass);

  {
    auto recorder = AcquireRecorder("vsm-stage-five.local-coarse.execute");
    CHECK_NOTNULL_F(recorder.get());
    RunPass(request_pass, render_context, *recorder);
  }
  WaitForQueueIdle();

  const auto request_flags_buffer = request_pass.GetPageRequestFlagsBuffer();
  ASSERT_NE(request_flags_buffer, nullptr);
  const auto actual_flags = ReadBufferAs<VsmShaderPageRequestFlags>(
    *request_flags_buffer, kVirtualPageCount, "vsm-stage-five.local-coarse");

  auto visible_samples = std::vector<VsmVisiblePixelSample> {};
  visible_samples.reserve(static_cast<std::size_t>(kWidth) * kHeight);
  for (std::uint32_t y = 0U; y < kHeight; ++y) {
    for (std::uint32_t x = 0U; x < kWidth; ++x) {
      visible_samples.push_back(VsmVisiblePixelSample {
        .world_position_ws = ComputeWorldPointForPixel(
          resolved_view, kWidth, kHeight, x, y, kDepth),
      });
    }
  }

  const auto expected_flags = BuildExpectedRequestFlags(
    projections, visible_samples, kVirtualPageCount);
  ExpectFlagsEqual(actual_flags, expected_flags);

  auto required_only_count = std::size_t { 0U };
  auto coarse_count = std::size_t { 0U };
  for (const auto& flag : actual_flags) {
    if (!HasAnyRequestFlag(flag, VsmShaderPageRequestFlagBits::kRequired)) {
      continue;
    }
    if (HasAnyRequestFlag(flag, VsmShaderPageRequestFlagBits::kCoarse)) {
      ++coarse_count;
    } else {
      ++required_only_count;
    }
  }
  EXPECT_GT(required_only_count, 0U);
  EXPECT_GT(coarse_count, 0U);
}

NOLINT_TEST_F(VsmPageRequestLiveSceneTest,
  DirectionalClipLevelProjectionPublishesExpectedFlagsForRealDepthField)
{
  constexpr auto kWidth = 4U;
  constexpr auto kHeight = 4U;
  constexpr float kDepth = 0.5F;
  constexpr std::uint32_t kPagesX = 2U;
  constexpr std::uint32_t kPagesY = 2U;
  constexpr std::uint32_t kClipLevel = 1U;
  constexpr std::uint32_t kLevelCount = 2U;
  constexpr std::uint32_t kPagesPerLevel = kPagesX * kPagesY;
  constexpr std::uint32_t kVirtualPageCount = 64U;

  const auto resolved_view = MakeResolvedView(kWidth, kHeight);
  auto depth_texture = CreateUploadedFloatTexture2D(
    std::vector<float>(static_cast<std::size_t>(kWidth) * kHeight, kDepth),
    kWidth, kHeight, oxygen::Format::kR32Float,
    "vsm-stage-five.directional-depth");

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto depth_pass = oxygen::engine::DepthPrePass(
    std::make_shared<oxygen::engine::DepthPrePass::Config>(
      oxygen::engine::DepthPrePass::Config { .depth_texture = depth_texture }));
  auto request_pass = oxygen::engine::VsmPageRequestGeneratorPass(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    std::make_shared<oxygen::engine::VsmPageRequestGeneratorPassConfig>(
      oxygen::engine::VsmPageRequestGeneratorPassConfig {
        .max_projection_count = 1U,
        .max_virtual_page_count = kVirtualPageCount,
        .enable_coarse_pages = false,
        .enable_light_grid_pruning = false,
        .debug_name = "VsmPageRequestDirectionalClipLevel",
      }));
  const auto projections = std::vector<VsmPageRequestProjection> {
    MakeDirectionalProjection(
      41U, 0U, kClipLevel, kLevelCount, kPagesX, kPagesY),
  };
  request_pass.SetFrameInputs(projections, kVirtualPageCount);

  auto prepared_frame = oxygen::engine::PreparedSceneFrame {};
  auto offscreen = renderer->BeginOffscreenFrame(
    { .frame_slot = Slot { 0U }, .frame_sequence = SequenceNumber { 73U } });
  offscreen.SetCurrentView(kTestViewId, resolved_view, prepared_frame);
  auto& render_context = offscreen.GetRenderContext();
  render_context.RegisterPass(&depth_pass);

  {
    auto recorder = AcquireRecorder("vsm-stage-five.directional-clip.execute");
    CHECK_NOTNULL_F(recorder.get());
    RunPass(request_pass, render_context, *recorder);
  }
  WaitForQueueIdle();

  const auto request_flags_buffer = request_pass.GetPageRequestFlagsBuffer();
  ASSERT_NE(request_flags_buffer, nullptr);
  const auto actual_flags
    = ReadBufferAs<VsmShaderPageRequestFlags>(*request_flags_buffer,
      kVirtualPageCount, "vsm-stage-five.directional-clip.flags");

  auto visible_samples = std::vector<VsmVisiblePixelSample> {};
  visible_samples.reserve(static_cast<std::size_t>(kWidth) * kHeight);
  for (std::uint32_t y = 0U; y < kHeight; ++y) {
    for (std::uint32_t x = 0U; x < kWidth; ++x) {
      visible_samples.push_back(VsmVisiblePixelSample {
        .world_position_ws = ComputeWorldPointForPixel(
          resolved_view, kWidth, kHeight, x, y, kDepth),
      });
    }
  }

  const auto expected_flags = BuildExpectedRequestFlags(
    projections, visible_samples, kVirtualPageCount);
  ExpectFlagsEqual(actual_flags, expected_flags);

  const auto required_bit
    = static_cast<std::uint32_t>(VsmShaderPageRequestFlagBits::kRequired);
  for (std::uint32_t index = 0U; index < kPagesPerLevel; ++index) {
    EXPECT_EQ(actual_flags[index].bits, 0U) << index;
  }
  for (std::uint32_t index = 0U; index < kPagesPerLevel; ++index) {
    EXPECT_EQ(actual_flags[kPagesPerLevel + index].bits, required_bit)
      << (kPagesPerLevel + index);
  }
  for (std::uint32_t index = kPagesPerLevel * kLevelCount;
    index < kVirtualPageCount; ++index) {
    EXPECT_EQ(actual_flags[index].bits, 0U) << index;
  }
}

} // namespace
