//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/LightCullingPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmPageRequestGeneratorPass.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPageRequestGeneration.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNode.h>

#include "VirtualShadowGpuTestFixtures.h"

namespace {

using oxygen::Format;
using oxygen::NdcDepthRange;
using oxygen::ResolvedView;
using oxygen::Scissors;
using oxygen::TextureType;
using oxygen::View;
using oxygen::ViewId;
using oxygen::ViewPort;
using oxygen::engine::DepthPrePass;
using oxygen::engine::LightCullingPass;
using oxygen::engine::LightCullingPassConfig;
using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::Renderer;
using oxygen::engine::VsmPageRequestGeneratorPass;
using oxygen::engine::VsmPageRequestGeneratorPassConfig;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::QueueRole;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::Texture;
using oxygen::graphics::TextureDesc;
using oxygen::graphics::TextureUploadRegion;
using oxygen::renderer::vsm::BuildPageRequests;
using oxygen::renderer::vsm::HasAnyRequestFlag;
using oxygen::renderer::vsm::kVsmInvalidLightIndex;
using oxygen::renderer::vsm::TryComputePageTableIndex;
using oxygen::renderer::vsm::VsmPageRequestProjection;
using oxygen::renderer::vsm::VsmProjectionData;
using oxygen::renderer::vsm::VsmProjectionLightType;
using oxygen::renderer::vsm::VsmShaderPageRequestFlagBits;
using oxygen::renderer::vsm::VsmShaderPageRequestFlags;
using oxygen::renderer::vsm::VsmVisiblePixelSample;
using oxygen::renderer::vsm::testing::VirtualShadowGpuTest;
using oxygen::scene::Scene;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;

constexpr std::uint32_t kTextureUploadRowPitch = 256U;
constexpr std::uint32_t kTestVirtualPageCount = 64U;
constexpr ViewId kTestViewId { 1U };

auto EncodeFloatTexel(const float value) -> std::vector<std::byte>
{
  auto bytes = std::vector<std::byte>(kTextureUploadRowPitch, std::byte { 0 });
  std::memcpy(bytes.data(), &value, sizeof(value));
  return bytes;
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
    for (const auto& projection : projections) {
      if (projection.map_id != request.map_id) {
        continue;
      }

      index = TryComputePageTableIndex(projection, request.page);
      if (index.has_value()) {
        break;
      }
    }
    CHECK_F(index.has_value(), "Missing projection route for map_id {}",
      request.map_id);
    CHECK_F(*index < virtual_page_count,
      "Expected request index {} exceeds virtual page count {}", *index,
      virtual_page_count);

    flags[*index].bits
      |= static_cast<std::uint32_t>(VsmShaderPageRequestFlagBits::kRequired);
    if (request.page.level != 0U) {
      flags[*index].bits
        |= static_cast<std::uint32_t>(VsmShaderPageRequestFlagBits::kCoarse);
    }
  }
  return flags;
}

auto DescribeNonZeroFlags(const VsmShaderPageRequestFlags* flags,
  const std::uint32_t count) -> std::string
{
  auto result = std::string {};
  for (std::uint32_t index = 0; index < count; ++index) {
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

class VsmPageRequestGeneratorGpuTest : public VirtualShadowGpuTest {
protected:
  auto UploadSingleChannelTexture(
    const float value, std::string_view debug_name) -> std::shared_ptr<Texture>
  {
    TextureDesc texture_desc {};
    texture_desc.width = 1U;
    texture_desc.height = 1U;
    texture_desc.format = Format::kR32Float;
    texture_desc.texture_type = TextureType::kTexture2D;
    texture_desc.is_shader_resource = true;
    texture_desc.debug_name = std::string(debug_name);

    auto texture = CreateRegisteredTexture(texture_desc);
    auto upload = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = kTextureUploadRowPitch,
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kUpload,
      .debug_name = std::string(debug_name) + "_Upload",
    });
    const auto upload_bytes = EncodeFloatTexel(value);
    upload->Update(upload_bytes.data(), upload_bytes.size(), 0U);

    const TextureUploadRegion upload_region {
      .buffer_offset = 0U,
      .buffer_row_pitch = kTextureUploadRowPitch,
      .buffer_slice_pitch = kTextureUploadRowPitch,
      .dst_slice = {
        .x = 0U,
        .y = 0U,
        .z = 0U,
        .width = 1U,
        .height = 1U,
        .depth = 1U,
        .mip_level = 0U,
        .array_slice = 0U,
      },
    };

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + "_Init");
      CHECK_NOTNULL_F(recorder.get());
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      EnsureTracked(*recorder, texture, ResourceStates::kCommon);
      recorder->RequireResourceState(*upload, ResourceStates::kCopySource);
      recorder->RequireResourceState(*texture, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBufferToTexture(*upload, upload_region, *texture);
      recorder->RequireResourceStateFinal(*texture, ResourceStates::kCommon);
    }
    WaitForQueueIdle();
    return texture;
  }

  auto ReadSingleChannelTexture(const std::shared_ptr<Texture>& texture) const
    -> float
  {
    EXPECT_NE(texture, nullptr);
    if (texture == nullptr) {
      return 0.0F;
    }
    const auto readback
      = GetReadbackManager()->ReadTextureNow(*texture, {}, true);
    EXPECT_TRUE(readback.has_value());
    if (!readback.has_value()) {
      return 0.0F;
    }
    EXPECT_GE(readback->bytes.size(), sizeof(float));
    if (readback->bytes.size() < sizeof(float)) {
      return 0.0F;
    }

    float value = 0.0F;
    std::memcpy(&value, readback->bytes.data(), sizeof(value));
    return value;
  }

  [[nodiscard]] static auto MakeResolvedView() -> ResolvedView
  {
    const auto view_matrix = glm::lookAtRH(glm::vec3 { 0.0F, 0.0F, 0.0F },
      glm::vec3 { 0.0F, 0.0F, -1.0F }, glm::vec3 { 0.0F, 1.0F, 0.0F });
    const auto projection_matrix
      = glm::perspectiveRH_ZO(glm::radians(90.0F), 1.0F, 0.1F, 100.0F);
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
      .view_matrix = view_matrix,
      .proj_matrix = projection_matrix,
      .camera_position = glm::vec3 { 0.0F, 0.0F, 0.0F },
      .depth_range = NdcDepthRange::ZeroToOne,
      .near_plane = 0.1F,
      .far_plane = 100.0F,
    });
  }

  [[nodiscard]] static auto MakeProjection(const std::uint32_t map_id,
    const std::uint32_t first_page_table_entry,
    const std::uint32_t light_index = kVsmInvalidLightIndex,
    const std::uint32_t level_count = 1U, const std::uint32_t coarse_level = 0U,
    const std::uint32_t map_pages_x = 4U, const std::uint32_t map_pages_y = 4U,
    const std::uint32_t pages_x = 4U, const std::uint32_t pages_y = 4U,
    const std::uint32_t page_offset_x = 0U,
    const std::uint32_t page_offset_y = 0U,
    const std::uint32_t cube_face_index
    = oxygen::renderer::vsm::kVsmInvalidCubeFaceIndex)
    -> VsmPageRequestProjection
  {
    const auto resolved_view = MakeResolvedView();
    return VsmPageRequestProjection {
      .projection = VsmProjectionData {
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
      .pages_x = pages_x,
      .pages_y = pages_y,
      .page_offset_x = page_offset_x,
      .page_offset_y = page_offset_y,
      .level_count = level_count,
      .coarse_level = coarse_level,
      .light_index = light_index,
      .cube_face_index = cube_face_index,
    };
  }

  [[nodiscard]] static auto ComputeDepthForWorldPoint(
    const ResolvedView& resolved_view, const glm::vec3 world_position)
  {
    const auto clip = resolved_view.ProjectionMatrix()
      * resolved_view.ViewMatrix() * glm::vec4(world_position, 1.0F);
    return clip.z / clip.w;
  }

  static auto CollectSinglePointLight(
    Renderer& renderer, const glm::vec3 world_position) -> void
  {
    constexpr std::size_t kSceneCapacity = 16U;
    auto scene = std::make_shared<Scene>("VsmLightScene", kSceneCapacity);

    const auto flags = SceneNode::Flags {}
                         .SetFlag(SceneNodeFlags::kVisible,
                           SceneFlag {}.SetEffectiveValueBit(true))
                         .SetFlag(SceneNodeFlags::kCastsShadows,
                           SceneFlag {}.SetEffectiveValueBit(true));
    auto node = scene->CreateNode("point-light", flags);
    ASSERT_TRUE(node.IsValid());
    ASSERT_TRUE(node.GetTransform().SetLocalPosition(world_position));
    scene->Update();

    auto impl = node.GetImpl();
    ASSERT_TRUE(impl.has_value());
    impl->get().AddComponent<oxygen::scene::PointLight>();

    auto light_manager = renderer.GetLightManager();
    ASSERT_NE(light_manager, nullptr);
    light_manager->CollectFromNode(node.GetHandle(), impl->get());
  }
};

NOLINT_TEST_F(VsmPageRequestGeneratorGpuTest,
  ExecutePublishesRequestFlagsForVisibleDepthSample)
{
  const auto resolved_view = MakeResolvedView();
  const auto world_position = glm::vec3 { 0.0F, 0.0F, -5.0F };
  const auto depth = ComputeDepthForWorldPoint(resolved_view, world_position);
  auto depth_texture = UploadSingleChannelTexture(depth, "vsm-request-depth");
  EXPECT_NEAR(ReadSingleChannelTexture(depth_texture), depth, 1.0e-5F);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto depth_pass = DepthPrePass(std::make_shared<DepthPrePass::Config>(
    DepthPrePass::Config { .depth_texture = depth_texture }));
  auto request_pass = VsmPageRequestGeneratorPass(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    std::make_shared<VsmPageRequestGeneratorPassConfig>(
      VsmPageRequestGeneratorPassConfig {
        .max_projection_count = 8U,
        .max_virtual_page_count = kTestVirtualPageCount,
        .enable_coarse_pages = true,
        .enable_light_grid_pruning = false,
        .debug_name = "VsmPageRequestGeneratorGpuTest",
      }));

  const auto projections = std::vector<VsmPageRequestProjection> {
    MakeProjection(7U, 0U, kVsmInvalidLightIndex, 4U, 3U)
  };
  request_pass.SetFrameInputs(projections, kTestVirtualPageCount);

  auto prepared_frame = PreparedSceneFrame {};
  auto offscreen = renderer->BeginOffscreenFrame(
    { .frame_slot = Slot { 0U }, .frame_sequence = SequenceNumber { 1U } });
  offscreen.SetCurrentView(kTestViewId, resolved_view, prepared_frame);
  auto& render_context = offscreen.GetRenderContext();
  render_context.RegisterPass(&depth_pass);

  {
    auto recorder = AcquireRecorder("vsm-page-request-execute");
    CHECK_NOTNULL_F(recorder.get());
    RunPass(request_pass, render_context, *recorder);
  }
  WaitForQueueIdle();

  const auto request_flags_buffer
    = std::const_pointer_cast<oxygen::graphics::Buffer>(
      request_pass.GetPageRequestFlagsBuffer());
  ASSERT_NE(request_flags_buffer, nullptr);

  const auto bytes = ReadBufferBytes(request_flags_buffer,
    kTestVirtualPageCount * sizeof(VsmShaderPageRequestFlags),
    "vsm-page-request-readback");
  ASSERT_EQ(
    bytes.size(), kTestVirtualPageCount * sizeof(VsmShaderPageRequestFlags));

  const auto* actual_flags
    = reinterpret_cast<const VsmShaderPageRequestFlags*>(bytes.data());
  const auto expected_flags = BuildExpectedRequestFlags(projections,
    std::vector<VsmVisiblePixelSample> {
      { .world_position_ws = world_position } },
    kTestVirtualPageCount);
  if (!std::equal(actual_flags, actual_flags + kTestVirtualPageCount,
        expected_flags.begin(), expected_flags.end(),
        [](
          const auto& lhs, const auto& rhs) { return lhs.bits == rhs.bits; })) {
    ADD_FAILURE() << "actual non-zero flags: "
                  << DescribeNonZeroFlags(actual_flags, kTestVirtualPageCount)
                  << ", expected non-zero flags: "
                  << DescribeNonZeroFlags(expected_flags.data(),
                       static_cast<std::uint32_t>(expected_flags.size()));
  }

  for (std::uint32_t index = 0; index < kTestVirtualPageCount; ++index) {
    EXPECT_EQ(actual_flags[index].bits, expected_flags[index].bits) << index;
  }
}

NOLINT_TEST_F(VsmPageRequestGeneratorGpuTest,
  LightGridPruningKeepsOnlyRequestsForClusterVisibleLocalLights)
{
  const auto resolved_view = MakeResolvedView();
  const auto world_position = glm::vec3 { 0.0F, 0.0F, -5.0F };
  const auto depth = ComputeDepthForWorldPoint(resolved_view, world_position);
  auto depth_texture = UploadSingleChannelTexture(depth, "vsm-pruning-depth");
  EXPECT_NEAR(ReadSingleChannelTexture(depth_texture), depth, 1.0e-5F);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto offscreen = renderer->BeginOffscreenFrame(
    { .frame_slot = Slot { 0U }, .frame_sequence = SequenceNumber { 1U } });
  CollectSinglePointLight(*renderer, world_position);

  auto depth_pass = DepthPrePass(std::make_shared<DepthPrePass::Config>(
    DepthPrePass::Config { .depth_texture = depth_texture }));
  auto light_culling_pass
    = LightCullingPass(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<LightCullingPassConfig>(LightCullingPassConfig {
        .cluster =
          [] {
            auto config = oxygen::engine::LightCullingConfig::Default();
            config.cluster_dim_z = 1U;
            config.tile_size_px = 16U;
            return config;
          }(),
        .debug_name = "VsmPageRequestLightGrid",
      }));
  auto request_pass = VsmPageRequestGeneratorPass(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    std::make_shared<VsmPageRequestGeneratorPassConfig>(
      VsmPageRequestGeneratorPassConfig {
        .max_projection_count = 8U,
        .max_virtual_page_count = kTestVirtualPageCount,
        .enable_coarse_pages = false,
        .enable_light_grid_pruning = true,
        .debug_name = "VsmPageRequestGeneratorLightGrid",
      }));

  const auto projections = std::vector<VsmPageRequestProjection> {
    MakeProjection(21U, 0U, 0U),
    MakeProjection(22U, 16U, 1U),
  };
  request_pass.SetFrameInputs(projections, kTestVirtualPageCount);

  auto prepared_frame = PreparedSceneFrame {};
  offscreen.SetCurrentView(kTestViewId, resolved_view, prepared_frame);
  auto& render_context = offscreen.GetRenderContext();
  render_context.RegisterPass(&depth_pass);

  {
    auto recorder = AcquireRecorder("vsm-light-grid-pass");
    CHECK_NOTNULL_F(recorder.get());
    RunPass(light_culling_pass, render_context, *recorder);
  }
  WaitForQueueIdle();
  render_context.RegisterPass(&light_culling_pass);

  EXPECT_TRUE(light_culling_pass.GetClusterGridSrvIndex().IsValid());
  EXPECT_TRUE(light_culling_pass.GetLightIndexListSrvIndex().IsValid());

  const auto cluster_grid_buffer
    = std::const_pointer_cast<oxygen::graphics::Buffer>(
      light_culling_pass.GetClusterGridBuffer());
  ASSERT_NE(cluster_grid_buffer, nullptr);
  const auto light_list_buffer
    = std::const_pointer_cast<oxygen::graphics::Buffer>(
      light_culling_pass.GetLightIndexListBuffer());
  ASSERT_NE(light_list_buffer, nullptr);

  // Seed a deterministic light-grid payload for the VSM pruning contract.
  // This keeps the VSM test focused on request filtering instead of coupling
  // it to LightCullingPass frustum math.
  const std::uint32_t cluster_grid_seed[2] = { 0U, 1U };
  const std::uint32_t light_list_seed[4] = { 0U, 0U, 0U, 0U };
  UploadBufferBytes(cluster_grid_buffer, cluster_grid_seed,
    sizeof(cluster_grid_seed), "vsm-light-grid-cluster-seed",
    oxygen::graphics::ResourceStates::kShaderResource,
    oxygen::graphics::ResourceStates::kShaderResource);
  UploadBufferBytes(light_list_buffer, light_list_seed, sizeof(light_list_seed),
    "vsm-light-grid-list-seed",
    oxygen::graphics::ResourceStates::kShaderResource,
    oxygen::graphics::ResourceStates::kShaderResource);

  {
    auto recorder = AcquireRecorder("vsm-pruning-pass");
    CHECK_NOTNULL_F(recorder.get());
    RunPass(request_pass, render_context, *recorder);
  }
  WaitForQueueIdle();

  const auto request_flags_buffer
    = std::const_pointer_cast<oxygen::graphics::Buffer>(
      request_pass.GetPageRequestFlagsBuffer());
  ASSERT_NE(request_flags_buffer, nullptr);

  const auto bytes = ReadBufferBytes(request_flags_buffer,
    kTestVirtualPageCount * sizeof(VsmShaderPageRequestFlags),
    "vsm-pruning-readback");
  ASSERT_EQ(
    bytes.size(), kTestVirtualPageCount * sizeof(VsmShaderPageRequestFlags));

  const auto* actual_flags
    = reinterpret_cast<const VsmShaderPageRequestFlags*>(bytes.data());
  const auto expected_flags = BuildExpectedRequestFlags(projections,
    std::vector<VsmVisiblePixelSample> { {
      .world_position_ws = world_position,
      .affecting_local_light_indices = { 0U },
    } },
    kTestVirtualPageCount);
  if (!std::equal(actual_flags, actual_flags + kTestVirtualPageCount,
        expected_flags.begin(), expected_flags.end(),
        [](
          const auto& lhs, const auto& rhs) { return lhs.bits == rhs.bits; })) {
    ADD_FAILURE() << "actual non-zero flags: "
                  << DescribeNonZeroFlags(actual_flags, kTestVirtualPageCount)
                  << ", expected non-zero flags: "
                  << DescribeNonZeroFlags(expected_flags.data(),
                       static_cast<std::uint32_t>(expected_flags.size()));
  }

  for (std::uint32_t index = 0; index < kTestVirtualPageCount; ++index) {
    EXPECT_EQ(actual_flags[index].bits, expected_flags[index].bits) << index;
  }

  EXPECT_TRUE(HasAnyRequestFlag(
    actual_flags[10], VsmShaderPageRequestFlagBits::kRequired));
  EXPECT_FALSE(HasAnyRequestFlag(
    actual_flags[26], VsmShaderPageRequestFlagBits::kRequired));
}

NOLINT_TEST_F(VsmPageRequestGeneratorGpuTest,
  ExecuteReallocatesProjectionAndRequestBuffersWhenSceneCapacityGrows)
{
  constexpr std::uint32_t kSmallProjectionCount = 1U;
  constexpr std::uint32_t kLargeProjectionCount = 32U;
  constexpr std::uint32_t kSmallVirtualPageCount = 64U;
  constexpr std::uint32_t kLargeVirtualPageCount = 14080U;
  constexpr std::uint32_t kPagesPerProjection = 16U;

  const auto resolved_view = MakeResolvedView();
  const auto world_position = glm::vec3 { 0.0F, 0.0F, -5.0F };
  const auto depth = ComputeDepthForWorldPoint(resolved_view, world_position);
  auto depth_texture
    = UploadSingleChannelTexture(depth, "vsm-request-growth-depth");
  EXPECT_NEAR(ReadSingleChannelTexture(depth_texture), depth, 1.0e-5F);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto depth_pass = DepthPrePass(std::make_shared<DepthPrePass::Config>(
    DepthPrePass::Config { .depth_texture = depth_texture }));
  auto config = std::make_shared<VsmPageRequestGeneratorPassConfig>(
    VsmPageRequestGeneratorPassConfig {
      .max_projection_count = kSmallProjectionCount,
      .max_virtual_page_count = kSmallVirtualPageCount,
      .enable_coarse_pages = false,
      .enable_light_grid_pruning = false,
      .debug_name = "VsmPageRequestGeneratorGrowth",
    });
  auto request_pass = VsmPageRequestGeneratorPass(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()), config);

  const auto run_pass
    = [&](const std::vector<VsmPageRequestProjection>& projections,
        const std::uint32_t virtual_page_count, const char* recorder_name,
        const SequenceNumber frame_sequence) {
        request_pass.SetFrameInputs(projections, virtual_page_count);

        auto prepared_frame = PreparedSceneFrame {};
        auto offscreen = renderer->BeginOffscreenFrame(
          { .frame_slot = Slot { 0U }, .frame_sequence = frame_sequence });
        offscreen.SetCurrentView(kTestViewId, resolved_view, prepared_frame);
        auto& render_context = offscreen.GetRenderContext();
        render_context.RegisterPass(&depth_pass);

        auto recorder = AcquireRecorder(recorder_name);
        CHECK_NOTNULL_F(recorder.get());
        RunPass(request_pass, render_context, *recorder);
        WaitForQueueIdle();
      };

  const auto small_projections
    = std::vector<VsmPageRequestProjection> { MakeProjection(7U, 0U) };
  run_pass(small_projections, kSmallVirtualPageCount,
    "vsm-request-growth-small", SequenceNumber { 1U });

  auto small_projection_buffer
    = std::const_pointer_cast<oxygen::graphics::Buffer>(
      request_pass.GetProjectionBuffer());
  auto small_request_flags_buffer
    = std::const_pointer_cast<oxygen::graphics::Buffer>(
      request_pass.GetPageRequestFlagsBuffer());
  ASSERT_NE(small_projection_buffer, nullptr);
  ASSERT_NE(small_request_flags_buffer, nullptr);
  EXPECT_EQ(small_projection_buffer->GetDescriptor().size_bytes,
    static_cast<std::uint64_t>(kSmallProjectionCount)
      * sizeof(VsmPageRequestProjection));
  EXPECT_EQ(small_request_flags_buffer->GetDescriptor().size_bytes,
    static_cast<std::uint64_t>(kSmallVirtualPageCount)
      * sizeof(VsmShaderPageRequestFlags));

  config->max_projection_count = kLargeProjectionCount;
  config->max_virtual_page_count = kLargeVirtualPageCount;

  auto large_projections = std::vector<VsmPageRequestProjection> {};
  large_projections.reserve(kLargeProjectionCount);
  for (std::uint32_t i = 0U; i < kLargeProjectionCount; ++i) {
    large_projections.push_back(
      MakeProjection(100U + i, i * kPagesPerProjection));
  }

  run_pass(large_projections, kLargeVirtualPageCount,
    "vsm-request-growth-large", SequenceNumber { 2U });

  auto large_projection_buffer
    = std::const_pointer_cast<oxygen::graphics::Buffer>(
      request_pass.GetProjectionBuffer());
  auto large_request_flags_buffer
    = std::const_pointer_cast<oxygen::graphics::Buffer>(
      request_pass.GetPageRequestFlagsBuffer());
  ASSERT_NE(large_projection_buffer, nullptr);
  ASSERT_NE(large_request_flags_buffer, nullptr);
  EXPECT_EQ(large_projection_buffer->GetDescriptor().size_bytes,
    static_cast<std::uint64_t>(kLargeProjectionCount)
      * sizeof(VsmPageRequestProjection));
  EXPECT_EQ(large_request_flags_buffer->GetDescriptor().size_bytes,
    static_cast<std::uint64_t>(kLargeVirtualPageCount)
      * sizeof(VsmShaderPageRequestFlags));

  const auto bytes = ReadBufferBytes(large_request_flags_buffer,
    kLargeVirtualPageCount * sizeof(VsmShaderPageRequestFlags),
    "vsm-request-growth-large-readback");
  ASSERT_EQ(
    bytes.size(), kLargeVirtualPageCount * sizeof(VsmShaderPageRequestFlags));

  const auto* actual_flags
    = reinterpret_cast<const VsmShaderPageRequestFlags*>(bytes.data());
  const auto expected_flags = BuildExpectedRequestFlags(large_projections,
    std::vector<VsmVisiblePixelSample> {
      { .world_position_ws = world_position } },
    kLargeVirtualPageCount);

  if (!std::equal(actual_flags, actual_flags + kLargeVirtualPageCount,
        expected_flags.begin(), expected_flags.end(),
        [](
          const auto& lhs, const auto& rhs) { return lhs.bits == rhs.bits; })) {
    ADD_FAILURE() << "actual non-zero flags: "
                  << DescribeNonZeroFlags(actual_flags, kLargeVirtualPageCount)
                  << ", expected non-zero flags: "
                  << DescribeNonZeroFlags(expected_flags.data(),
                       static_cast<std::uint32_t>(expected_flags.size()));
  }
}

} // namespace
