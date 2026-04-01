//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewHelpers.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/Passes/ConventionalShadowReceiverAnalysisPass.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/ScreenHzbBuildPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Renderer/Test/Fixtures/RendererOffscreenGpuTestFixture.h>
#include <Oxygen/Renderer/Types/ConventionalShadowReceiverAnalysis.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

namespace {

using oxygen::Format;
using oxygen::NdcDepthRange;
using oxygen::observer_ptr;
using oxygen::ResolvedView;
using oxygen::Scissors;
using oxygen::TextureType;
using oxygen::View;
using oxygen::ViewId;
using oxygen::ViewPort;
using oxygen::engine::ConventionalShadowReceiverAnalysisPass;
using oxygen::engine::ConventionalShadowReceiverAnalysisPassConfig;
using oxygen::engine::DepthPrePass;
using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::ScreenHzbBuildPass;
using oxygen::engine::ScreenHzbBuildPassConfig;
using oxygen::engine::ViewConstants;
using oxygen::engine::testing::RendererOffscreenGpuTestFixture;
using oxygen::engine::testing::RunPass;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::Texture;
using oxygen::renderer::ConventionalShadowReceiverAnalysis;
using oxygen::renderer::ConventionalShadowReceiverAnalysisPlan;
using oxygen::renderer::LightManager;
using oxygen::scene::DirectionalLight;
using oxygen::scene::Scene;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;

constexpr ViewId kTestViewId { 37U };
constexpr std::uint32_t kWidth = 16U;
constexpr std::uint32_t kHeight = 16U;
constexpr std::uint32_t kUploadRowPitch = 256U;

class ConventionalShadowReceiverAnalysisPassTest
  : public RendererOffscreenGpuTestFixture {
protected:
  [[nodiscard]] static auto MakeResolvedView() -> ResolvedView
  {
    auto view_config = View {};
    view_config.viewport = ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(kWidth),
      .height = static_cast<float>(kHeight),
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    view_config.scissor = Scissors {
      .left = 0,
      .top = 0,
      .right = static_cast<std::int32_t>(kWidth),
      .bottom = static_cast<std::int32_t>(kHeight),
    };

    const auto view_matrix = glm::lookAtRH(glm::vec3 { 0.0F, 0.0F, 0.0F },
      glm::vec3 { 0.0F, 0.0F, -1.0F }, glm::vec3 { 0.0F, 1.0F, 0.0F });
    const auto projection_matrix
      = oxygen::MakeReversedZPerspectiveProjectionRH_ZO(
        glm::radians(75.0F), 1.0F, 0.1F, 100.0F);

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

  [[nodiscard]] static auto MakeViewConstants(const ResolvedView& resolved_view,
    const Slot frame_slot, const SequenceNumber frame_sequence) -> ViewConstants
  {
    auto view_constants = ViewConstants {};
    view_constants.SetFrameSlot(frame_slot, ViewConstants::kRenderer)
      .SetFrameSequenceNumber(frame_sequence, ViewConstants::kRenderer)
      .SetBindlessViewFrameBindingsSlot(
        oxygen::engine::BindlessViewFrameBindingsSlot {
          oxygen::ShaderVisibleIndex { 1U } },
        ViewConstants::kRenderer)
      .SetTimeSeconds(0.0F, ViewConstants::kRenderer)
      .SetViewMatrix(resolved_view.ViewMatrix())
      .SetProjectionMatrix(resolved_view.ProjectionMatrix())
      .SetStableProjectionMatrix(resolved_view.StableProjectionMatrix())
      .SetCameraPosition(resolved_view.CameraPosition());
    return view_constants;
  }

  auto CreateDepthTexture(std::string_view debug_name)
    -> std::shared_ptr<Texture>
  {
    auto texture_desc = oxygen::graphics::TextureDesc {};
    texture_desc.width = kWidth;
    texture_desc.height = kHeight;
    texture_desc.format = Format::kDepth32;
    texture_desc.texture_type = TextureType::kTexture2D;
    texture_desc.is_shader_resource = true;
    texture_desc.is_render_target = true;
    texture_desc.is_typeless = true;
    texture_desc.use_clear_value = true;
    texture_desc.clear_value
      = oxygen::graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F };
    texture_desc.initial_state = ResourceStates::kCommon;
    texture_desc.debug_name = std::string(debug_name);
    return CreateRegisteredTexture(texture_desc);
  }

  [[nodiscard]] static auto DeviceDepthFromLinearViewDepth(
    const float linear_depth, const glm::mat4& projection_matrix) -> float
  {
    const auto clip
      = projection_matrix * glm::vec4(0.0F, 0.0F, -linear_depth, 1.0F);
    return clip.z / std::max(clip.w, 1.0e-6F);
  }

  [[nodiscard]] static auto ReconstructLinearViewDepthFromDeviceDepth(
    const std::uint32_t pixel_x, const std::uint32_t pixel_y,
    const float device_depth, const glm::uvec2 screen_dimensions,
    const glm::mat4& inverse_view_projection, const glm::mat4& view_matrix)
    -> float
  {
    const auto uv
      = (glm::vec2 { static_cast<float>(pixel_x), static_cast<float>(pixel_y) }
          + glm::vec2 { 0.5F, 0.5F })
      / glm::vec2(screen_dimensions);
    const auto ndc_xy = glm::vec2 { uv.x * 2.0F - 1.0F, 1.0F - uv.y * 2.0F };
    const auto clip = glm::vec4 { ndc_xy, device_depth, 1.0F };
    const auto world = inverse_view_projection * clip;
    const auto world_position = glm::vec3(world) / world.w;
    const auto view_position = view_matrix * glm::vec4(world_position, 1.0F);
    return std::max(0.0F, -view_position.z);
  }

  auto SeedDepthPatch(const std::shared_ptr<Texture>& depth_texture,
    const ResolvedView& resolved_view, std::string_view debug_name) -> void
  {
    ASSERT_NE(depth_texture, nullptr);
    auto upload_bytes = std::vector<std::byte>(
      static_cast<std::size_t>(kUploadRowPitch) * kHeight, std::byte { 0 });

    const float near_depth
      = DeviceDepthFromLinearViewDepth(1.25F, resolved_view.ProjectionMatrix());
    const float mid_depth
      = DeviceDepthFromLinearViewDepth(3.50F, resolved_view.ProjectionMatrix());

    for (std::uint32_t y = 5U; y < 11U; ++y) {
      for (std::uint32_t x = 6U; x < 10U; ++x) {
        const float value = y < 8U ? near_depth : mid_depth;
        std::memcpy(upload_bytes.data()
            + static_cast<std::size_t>(y) * kUploadRowPitch
            + static_cast<std::size_t>(x) * sizeof(float),
          &value, sizeof(value));
      }
    }

    auto upload = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(upload_bytes.size()),
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kUpload,
      .debug_name = std::string(debug_name) + ".Upload",
    });
    ASSERT_NE(upload, nullptr);
    upload->Update(upload_bytes.data(), upload_bytes.size(), 0U);

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".SeedDepth");
      ASSERT_NE(recorder, nullptr);
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      EnsureTracked(*recorder, depth_texture, ResourceStates::kCommon);
      recorder->RequireResourceState(*upload, ResourceStates::kCopySource);
      recorder->RequireResourceState(*depth_texture, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBufferToTexture(*upload,
        oxygen::graphics::TextureUploadRegion {
          .buffer_offset = 0U,
          .buffer_row_pitch = kUploadRowPitch,
          .buffer_slice_pitch = kUploadRowPitch * kHeight,
          .dst_slice = {
            .x = 0U,
            .y = 0U,
            .z = 0U,
            .width = kWidth,
            .height = kHeight,
            .depth = 1U,
            .mip_level = 0U,
            .array_slice = 0U,
          },
        },
        *depth_texture);
      recorder->RequireResourceStateFinal(
        *depth_texture, ResourceStates::kCommon);
    }
    WaitForQueueIdle();
  }

  auto ReadTextureTexel(const std::shared_ptr<const Texture>& texture,
    const std::uint32_t mip_level, const std::uint32_t x, const std::uint32_t y,
    std::string_view debug_name) -> float
  {
    CHECK_NOTNULL_F(texture.get(), "Cannot read from a null texture");
    constexpr std::uint32_t kRowPitch = 256U;
    auto readback = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = kRowPitch,
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kReadBack,
      .debug_name = std::string(debug_name) + ".Readback",
    });
    CHECK_NOTNULL_F(readback.get(), "Failed to create texture readback buffer");

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".ProbeCopy");
      CHECK_NOTNULL_F(
        recorder.get(), "Failed to acquire texture probe recorder");
      RegisterResource(std::const_pointer_cast<Texture>(texture));
      recorder->BeginTrackingResourceState(
        *texture, ResourceStates::kShaderResource, true);
      EnsureTracked(*recorder, readback, ResourceStates::kCopyDest);
      recorder->RequireResourceState(*texture, ResourceStates::kCopySource);
      recorder->RequireResourceState(*readback, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyTextureToBuffer(*readback, *texture,
        oxygen::graphics::TextureBufferCopyRegion {
          .buffer_offset = oxygen::OffsetBytes { 0U },
          .buffer_row_pitch = oxygen::SizeBytes { kRowPitch },
          .texture_slice = {
            .x = x,
            .y = y,
            .z = 0U,
            .width = 1U,
            .height = 1U,
            .depth = 1U,
            .mip_level = mip_level,
            .array_slice = 0U,
          },
        });
    }
    WaitForQueueIdle();

    float value = 0.0F;
    const auto* mapped
      = static_cast<const std::byte*>(readback->Map(0U, kRowPitch));
    CHECK_NOTNULL_F(mapped, "Failed to map texture readback buffer");
    std::memcpy(&value, mapped, sizeof(value));
    readback->UnMap();
    return value;
  }

  [[nodiscard]] static auto DescribeJobPlan(
    const ConventionalShadowReceiverAnalysisPlan& plan) -> std::string
  {
    auto stream = std::ostringstream {};
    stream << "job_count=" << plan.jobs.size();
    for (std::size_t i = 0; i < plan.jobs.size(); ++i) {
      const auto& job = plan.jobs[i];
      stream << " | job[" << i << "]"
             << " flags=0x" << std::hex << job.flags << std::dec << " split=["
             << job.split_and_full_depth_range.x << ", "
             << job.split_and_full_depth_range.y << "]"
             << " full_depth=[" << job.split_and_full_depth_range.z << ", "
             << job.split_and_full_depth_range.w << "]"
             << " full_rect_half=[" << job.full_rect_center_half_extent.z
             << ", " << job.full_rect_center_half_extent.w << "]"
             << " legacy_rect_half=[" << job.legacy_rect_center_half_extent.z
             << ", " << job.legacy_rect_center_half_extent.w << "]";
    }
    return stream.str();
  }

  [[nodiscard]] static auto DescribeAnalysisRecords(
    const std::span<const ConventionalShadowReceiverAnalysis> records)
    -> std::string
  {
    auto stream = std::ostringstream {};
    stream << "record_count=" << records.size();
    for (std::size_t i = 0; i < records.size(); ++i) {
      const auto& record = records[i];
      stream << " | record[" << i << "]"
             << " flags=0x" << std::hex << record.flags << std::dec
             << " samples=" << record.sample_count
             << " target_slice=" << record.target_array_slice << " raw_xy=["
             << record.raw_xy_min_max.x << ", " << record.raw_xy_min_max.y
             << ", " << record.raw_xy_min_max.z << ", "
             << record.raw_xy_min_max.w << "]"
             << " raw_depth=[" << record.raw_depth_and_dilation.x << ", "
             << record.raw_depth_and_dilation.y << "]"
             << " full_rect_half=[" << record.full_rect_center_half_extent.z
             << ", " << record.full_rect_center_half_extent.w << "]"
             << " legacy_rect_half=[" << record.legacy_rect_center_half_extent.z
             << ", " << record.legacy_rect_center_half_extent.w << "]"
             << " full_depth=[" << record.full_depth_and_area_ratios.x << ", "
             << record.full_depth_and_area_ratios.y << "]"
             << " full_area_ratio=" << record.full_depth_and_area_ratios.z
             << " full_depth_ratio=" << record.full_depth_ratio;
    }
    return stream.str();
  }

  [[nodiscard]] static auto CreateShadowCastingDirectionalNode(Scene& scene)
    -> SceneNode
  {
    const auto flags = SceneNode::Flags {}
                         .SetFlag(SceneNodeFlags::kVisible,
                           SceneFlag {}.SetEffectiveValueBit(true))
                         .SetFlag(SceneNodeFlags::kCastsShadows,
                           SceneFlag {}.SetEffectiveValueBit(true));

    auto node = scene.CreateNode("receiver-analysis.directional", flags);
    EXPECT_TRUE(node.IsValid());

    auto impl = node.GetImpl();
    EXPECT_TRUE(impl.has_value());
    if (impl.has_value()) {
      impl->get().AddComponent<DirectionalLight>();
      impl->get().GetComponent<DirectionalLight>().Common().casts_shadows
        = true;
      impl->get().UpdateTransforms(scene);
    }

    return node;
  }
};

NOLINT_TEST_F(ConventionalShadowReceiverAnalysisPassTest,
  ProducesSampleDrivenReceiverBoundsTighterThanFullCascadeFit)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto scene = std::make_shared<Scene>("receiver-analysis.scene", 64U);
  auto directional_node = CreateShadowCastingDirectionalNode(*scene);
  auto directional_impl = directional_node.GetImpl();
  ASSERT_TRUE(directional_impl.has_value());

  constexpr auto kFrameSlot = Slot { 0U };
  constexpr auto kFrameSequence = SequenceNumber { 1U };
  const auto resolved_view = MakeResolvedView();
  const auto view_constants
    = MakeViewConstants(resolved_view, kFrameSlot, kFrameSequence);

  auto depth_texture = CreateDepthTexture("receiver-analysis.depth");
  ASSERT_NE(depth_texture, nullptr);
  SeedDepthPatch(depth_texture, resolved_view, "receiver-analysis.depth");

  auto prepared_frame = PreparedSceneFrame {};
  auto offscreen = renderer->BeginOffscreenFrame({ .frame_slot = kFrameSlot,
    .frame_sequence = kFrameSequence,
    .scene = observer_ptr<Scene> { scene.get() } });
  offscreen.SetCurrentView(
    kTestViewId, resolved_view, prepared_frame, view_constants);
  auto& render_context = offscreen.GetRenderContext();

  auto depth_config = std::make_shared<DepthPrePass::Config>();
  depth_config->depth_texture = depth_texture;
  depth_config->debug_name = "receiver-analysis.depth-prepass";
  auto depth_pass = DepthPrePass(depth_config);
  render_context.RegisterPass(&depth_pass);

  auto light_manager = renderer->GetLightManager();
  ASSERT_NE(light_manager.get(), nullptr);
  light_manager->CollectFromNode(
    directional_node.GetHandle(), directional_impl->get());

  auto shadow_manager = renderer->GetShadowManager();
  ASSERT_NE(shadow_manager.get(), nullptr);
  shadow_manager->ReserveFrameResources(1U, *light_manager);

  const auto shadow_caster_bounds
    = std::array { glm::vec4 { 0.0F, 0.0F, -5.0F, 10.0F } };
  shadow_manager->PublishForView(kTestViewId, view_constants, *light_manager,
    observer_ptr<Scene> { scene.get() }, static_cast<float>(kWidth), {},
    shadow_caster_bounds);
  const auto* plan = shadow_manager->TryGetReceiverAnalysisPlan(kTestViewId);
  ASSERT_NE(plan, nullptr);
  ASSERT_FALSE(plan->jobs.empty()) << "receiver-analysis plan is empty";
  const auto covers_seeded_depths
    = std::any_of(plan->jobs.begin(), plan->jobs.end(), [](const auto& job) {
        return job.split_and_full_depth_range.x <= 1.25F
          && job.split_and_full_depth_range.y >= 3.5F;
      });
  ASSERT_TRUE(covers_seeded_depths) << DescribeJobPlan(*plan);

  auto hzb_pass
    = ScreenHzbBuildPass(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<ScreenHzbBuildPassConfig>(
        ScreenHzbBuildPassConfig { .debug_name = "receiver-analysis.hzb" }));

  auto receiver_analysis_pass = ConventionalShadowReceiverAnalysisPass(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    std::make_shared<ConventionalShadowReceiverAnalysisPassConfig>(
      ConventionalShadowReceiverAnalysisPassConfig {
        .debug_name = "receiver-analysis",
      }));

  {
    auto recorder = AcquireRecorder("receiver-analysis.run");
    ASSERT_NE(recorder, nullptr);
    EnsureTracked(*recorder, depth_texture, ResourceStates::kCommon);

    oxygen::co::testing::TestEventLoop loop;
    oxygen::co::Run(loop, [&]() -> oxygen::co::Co<> {
      co_await depth_pass.PrepareResources(render_context, *recorder);
      co_return;
    });

    RunPass(hzb_pass, render_context, *recorder);
    render_context.RegisterPass<ScreenHzbBuildPass>(&hzb_pass);
    RunPass(receiver_analysis_pass, render_context, *recorder);
  }
  WaitForQueueIdle();

  const auto hzb_output = hzb_pass.GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(hzb_output.available);
  const auto near_depth_sample = ReadTextureTexel(
    hzb_output.closest_texture, 0U, 7U, 6U, "receiver-analysis.hzb.near");
  const auto mid_depth_sample = ReadTextureTexel(
    hzb_output.closest_texture, 0U, 7U, 9U, "receiver-analysis.hzb.mid");
  ASSERT_GT(near_depth_sample, 0.0F);
  ASSERT_GT(mid_depth_sample, 0.0F);
  EXPECT_NEAR(near_depth_sample,
    DeviceDepthFromLinearViewDepth(1.25F, resolved_view.ProjectionMatrix()),
    1.0e-5F);
  EXPECT_NEAR(mid_depth_sample,
    DeviceDepthFromLinearViewDepth(3.50F, resolved_view.ProjectionMatrix()),
    1.0e-5F);
  EXPECT_NEAR(
    ReconstructLinearViewDepthFromDeviceDepth(7U, 6U, near_depth_sample,
      glm::uvec2 { kWidth, kHeight }, resolved_view.InverseViewProjection(),
      resolved_view.ViewMatrix()),
    1.25F, 1.0e-3F);
  EXPECT_NEAR(
    ReconstructLinearViewDepthFromDeviceDepth(7U, 9U, mid_depth_sample,
      glm::uvec2 { kWidth, kHeight }, resolved_view.InverseViewProjection(),
      resolved_view.ViewMatrix()),
    3.50F, 1.0e-3F);

  const auto output = receiver_analysis_pass.GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(output.available);
  ASSERT_NE(output.analysis_buffer, nullptr);
  EXPECT_GT(output.job_count, 0U);

  auto readback_manager = Backend().GetReadbackManager();
  ASSERT_NE(readback_manager, nullptr);
  const auto bytes = readback_manager->ReadBufferNow(*output.analysis_buffer);
  ASSERT_TRUE(bytes.has_value());
  ASSERT_EQ(bytes->size() % sizeof(ConventionalShadowReceiverAnalysis), 0U);

  auto records = std::vector<ConventionalShadowReceiverAnalysis>(
    bytes->size() / sizeof(ConventionalShadowReceiverAnalysis));
  std::memcpy(records.data(), bytes->data(), bytes->size());

  const auto sampled_record = std::find_if(records.begin(), records.end(),
    [](const ConventionalShadowReceiverAnalysis& record) {
      return record.sample_count > 0U;
    });
  ASSERT_NE(sampled_record, records.end()) << DescribeAnalysisRecords(records);

  EXPECT_LT(sampled_record->full_depth_and_area_ratios.z, 1.0F);
  EXPECT_LT(sampled_record->full_depth_ratio, 1.0F);
  EXPECT_GT(sampled_record->raw_depth_and_dilation.z, 0.0F);
  EXPECT_GT(sampled_record->raw_depth_and_dilation.w, 0.0F);
}

} // namespace
