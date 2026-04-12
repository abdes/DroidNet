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
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Renderer/FacadePresets.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/Passes/ConventionalShadowReceiverAnalysisPass.h>
#include <Oxygen/Renderer/Passes/ConventionalShadowReceiverMaskPass.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/ScreenHzbBuildPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Renderer/Test/Fixtures/RendererOffscreenGpuTestFixture.h>
#include <Oxygen/Renderer/Types/ConventionalShadowReceiverMask.h>
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
using oxygen::engine::ConventionalShadowReceiverMaskPass;
using oxygen::engine::ConventionalShadowReceiverMaskPassConfig;
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
using oxygen::graphics::Framebuffer;
using oxygen::graphics::FramebufferDesc;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::Texture;
using oxygen::renderer::ConventionalShadowReceiverAnalysisJob;
using oxygen::renderer::ConventionalShadowReceiverAnalysisPlan;
using oxygen::renderer::ConventionalShadowReceiverMaskSummary;
using oxygen::renderer::LightManager;
using oxygen::scene::DirectionalLight;
using oxygen::scene::Scene;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;

constexpr ViewId kTestViewId { 38U };
constexpr std::uint32_t kWidth = 16U;
constexpr std::uint32_t kHeight = 16U;
constexpr std::uint32_t kUploadRowPitch = 256U;

class ConventionalShadowReceiverMaskPassTest
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

  auto MakeHarnessFramebuffer(std::string_view debug_name)
    -> std::shared_ptr<Framebuffer>
  {
    auto color_desc = oxygen::graphics::TextureDesc {};
    color_desc.width = kWidth;
    color_desc.height = kHeight;
    color_desc.format = Format::kRGBA8UNorm;
    color_desc.texture_type = TextureType::kTexture2D;
    color_desc.is_render_target = true;
    color_desc.is_shader_resource = true;
    color_desc.initial_state = ResourceStates::kCommon;
    color_desc.debug_name = std::string(debug_name) + ".Color";

    auto color = Backend().CreateTexture(color_desc);
    CHECK_NOTNULL_F(
      color.get(), "Failed to create receiver-mask harness framebuffer");
    auto framebuffer_desc = FramebufferDesc {};
    framebuffer_desc.AddColorAttachment({ .texture = color });
    auto framebuffer = Backend().CreateFramebuffer(framebuffer_desc);
    CHECK_NOTNULL_F(
      framebuffer.get(), "Failed to create receiver-mask harness framebuffer");
    return framebuffer;
  }

  [[nodiscard]] static auto DeviceDepthFromLinearViewDepth(
    const float linear_depth, const glm::mat4& projection_matrix) -> float
  {
    const auto clip
      = projection_matrix * glm::vec4(0.0F, 0.0F, -linear_depth, 1.0F);
    return clip.z / std::max(clip.w, 1.0e-6F);
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
             << job.split_and_full_depth_range.y << "]";
    }
    return stream.str();
  }

  [[nodiscard]] static auto JobContainsEyeDepth(const std::size_t job_index,
    const ConventionalShadowReceiverAnalysisJob& job, const float eye_depth)
    -> bool
  {
    const auto split_begin = job.split_and_full_depth_range.x;
    const auto split_end = job.split_and_full_depth_range.y;
    return eye_depth <= split_end
      && (job_index == 0U ? eye_depth >= split_begin : eye_depth > split_begin);
  }

  [[nodiscard]] static auto DescribeMaskSummaries(
    const std::span<const ConventionalShadowReceiverMaskSummary> records)
    -> std::string
  {
    auto stream = std::ostringstream {};
    stream << "record_count=" << records.size();
    for (std::size_t i = 0; i < records.size(); ++i) {
      const auto& record = records[i];
      stream << " | record[" << i << "]"
             << " flags=0x" << std::hex << record.flags << std::dec
             << " samples=" << record.sample_count
             << " occupied=" << record.occupied_tile_count
             << " hierarchy_occupied=" << record.hierarchy_occupied_tile_count
             << " base_tile_resolution=" << record.base_tile_resolution
             << " hierarchy_tile_resolution="
             << record.hierarchy_tile_resolution
             << " dilation_tile_radius=" << record.dilation_tile_radius;
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

    auto node = scene.CreateNode("receiver-mask.directional", flags);
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

NOLINT_TEST_F(ConventionalShadowReceiverMaskPassTest,
  ProducesSparseReceiverTileMaskForSampledCascade)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto scene = std::make_shared<Scene>("receiver-mask.scene", 64U);
  auto directional_node = CreateShadowCastingDirectionalNode(*scene);
  auto directional_impl = directional_node.GetImpl();
  ASSERT_TRUE(directional_impl.has_value());

  constexpr auto kFrameSlot = Slot { 0U };
  constexpr auto kFrameSequence = SequenceNumber { 1U };
  const auto resolved_view = MakeResolvedView();
  const auto view_constants
    = MakeViewConstants(resolved_view, kFrameSlot, kFrameSequence);

  auto depth_texture = CreateDepthTexture("receiver-mask.depth");
  ASSERT_NE(depth_texture, nullptr);
  SeedDepthPatch(depth_texture, resolved_view, "receiver-mask.depth");

  auto prepared_frame = PreparedSceneFrame {};
  auto framebuffer = MakeHarnessFramebuffer("receiver-mask");
  auto harness = oxygen::renderer::harness::single_pass::presets::
    ForPreparedSceneGraphicsPass(*renderer,
      oxygen::engine::Renderer::FrameSessionInput {
        .frame_slot = kFrameSlot,
        .frame_sequence = kFrameSequence,
        .scene = observer_ptr<Scene> { scene.get() },
      },
      oxygen::observer_ptr<const Framebuffer> { framebuffer.get() },
      oxygen::engine::Renderer::ResolvedViewInput {
        .view_id = kTestViewId,
        .value = resolved_view,
      },
      oxygen::engine::Renderer::PreparedFrameInput { .value = prepared_frame },
      oxygen::engine::Renderer::CoreShaderInputsInput {
        .view_id = kTestViewId,
        .value = view_constants,
      });
  auto harness_result = harness.Finalize();
  ASSERT_TRUE(harness_result.has_value());
  auto& render_context = harness_result->GetRenderContext();

  auto depth_config = std::make_shared<DepthPrePass::Config>();
  depth_config->depth_texture = depth_texture;
  depth_config->debug_name = "receiver-mask.depth-prepass";
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
  auto covers_near_seeded_depth = false;
  auto covers_mid_seeded_depth = false;
  for (std::size_t job_index = 0U; job_index < plan->jobs.size(); ++job_index) {
    covers_near_seeded_depth = covers_near_seeded_depth
      || JobContainsEyeDepth(job_index, plan->jobs[job_index], 1.25F);
    covers_mid_seeded_depth = covers_mid_seeded_depth
      || JobContainsEyeDepth(job_index, plan->jobs[job_index], 3.50F);
  }
  ASSERT_TRUE(covers_near_seeded_depth) << DescribeJobPlan(*plan);
  ASSERT_TRUE(covers_mid_seeded_depth) << DescribeJobPlan(*plan);

  auto hzb_pass
    = ScreenHzbBuildPass(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<ScreenHzbBuildPassConfig>(
        ScreenHzbBuildPassConfig { .debug_name = "receiver-mask.hzb" }));

  auto receiver_analysis_pass = ConventionalShadowReceiverAnalysisPass(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    std::make_shared<ConventionalShadowReceiverAnalysisPassConfig>(
      ConventionalShadowReceiverAnalysisPassConfig {
        .debug_name = "receiver-mask.analysis",
      }));

  auto receiver_mask_pass = ConventionalShadowReceiverMaskPass(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    std::make_shared<ConventionalShadowReceiverMaskPassConfig>(
      ConventionalShadowReceiverMaskPassConfig {
        .debug_name = "receiver-mask",
      }));

  {
    auto recorder = AcquireRecorder("receiver-mask.run");
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
    render_context.RegisterPass<ConventionalShadowReceiverAnalysisPass>(
      &receiver_analysis_pass);

    oxygen::co::Run(loop, [&]() -> oxygen::co::Co<> {
      co_await receiver_mask_pass.PrepareResources(render_context, *recorder);
      co_return;
    });
    oxygen::co::Run(loop, [&]() -> oxygen::co::Co<> {
      co_await receiver_mask_pass.Execute(render_context, *recorder);
      co_return;
    });
  }
  WaitForQueueIdle();

  const auto output = receiver_mask_pass.GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(output.available);
  ASSERT_NE(output.summary_buffer, nullptr);
  ASSERT_NE(output.base_mask_buffer, nullptr);
  ASSERT_NE(output.hierarchy_mask_buffer, nullptr);
  ASSERT_GT(output.job_count, 0U);
  ASSERT_GT(output.base_tile_resolution, 0U);
  ASSERT_GT(output.hierarchy_tile_resolution, 0U);
  ASSERT_GT(output.hierarchy_reduction, 0U);

  auto readback_manager = Backend().GetReadbackManager();
  ASSERT_NE(readback_manager, nullptr);

  const auto summary_bytes
    = readback_manager->ReadBufferNow(*output.summary_buffer);
  ASSERT_TRUE(summary_bytes.has_value());
  ASSERT_EQ(summary_bytes->size(),
    static_cast<std::size_t>(output.job_count)
      * sizeof(ConventionalShadowReceiverMaskSummary));

  const auto base_bytes
    = readback_manager->ReadBufferNow(*output.base_mask_buffer);
  ASSERT_TRUE(base_bytes.has_value());
  const auto hierarchy_bytes
    = readback_manager->ReadBufferNow(*output.hierarchy_mask_buffer);
  ASSERT_TRUE(hierarchy_bytes.has_value());

  auto summaries
    = std::vector<ConventionalShadowReceiverMaskSummary>(output.job_count);
  std::memcpy(summaries.data(), summary_bytes->data(), summary_bytes->size());

  auto base_mask
    = std::vector<std::uint32_t>(base_bytes->size() / sizeof(std::uint32_t));
  std::memcpy(base_mask.data(), base_bytes->data(), base_bytes->size());

  auto hierarchy_mask = std::vector<std::uint32_t>(
    hierarchy_bytes->size() / sizeof(std::uint32_t));
  std::memcpy(
    hierarchy_mask.data(), hierarchy_bytes->data(), hierarchy_bytes->size());

  const auto base_tiles_per_job
    = output.base_tile_resolution * output.base_tile_resolution;
  const auto hierarchy_tiles_per_job
    = output.hierarchy_tile_resolution * output.hierarchy_tile_resolution;
  ASSERT_EQ(base_mask.size(),
    static_cast<std::size_t>(output.job_count) * base_tiles_per_job);
  ASSERT_EQ(hierarchy_mask.size(),
    static_cast<std::size_t>(output.job_count) * hierarchy_tiles_per_job);

  bool found_sampled_job = false;
  bool found_sparse_job = false;
  bool near_depth_sampled = false;
  bool mid_depth_sampled = false;
  for (std::uint32_t job_index = 0U; job_index < output.job_count;
    ++job_index) {
    const auto base_begin = base_mask.begin()
      + static_cast<std::ptrdiff_t>(job_index * base_tiles_per_job);
    const auto base_end
      = base_begin + static_cast<std::ptrdiff_t>(base_tiles_per_job);
    const auto hierarchy_begin = hierarchy_mask.begin()
      + static_cast<std::ptrdiff_t>(job_index * hierarchy_tiles_per_job);
    const auto hierarchy_end
      = hierarchy_begin + static_cast<std::ptrdiff_t>(hierarchy_tiles_per_job);

    const auto occupied_tile_count = static_cast<std::uint32_t>(std::count_if(
      base_begin, base_end, [](const auto value) { return value != 0U; }));
    const auto hierarchy_occupied_tile_count
      = static_cast<std::uint32_t>(std::count_if(hierarchy_begin, hierarchy_end,
        [](const auto value) { return value != 0U; }));

    const auto& summary = summaries[job_index];
    EXPECT_EQ(summary.occupied_tile_count, occupied_tile_count)
      << DescribeMaskSummaries(summaries);
    EXPECT_EQ(
      summary.hierarchy_occupied_tile_count, hierarchy_occupied_tile_count)
      << DescribeMaskSummaries(summaries);
    EXPECT_EQ(summary.base_tile_resolution, output.base_tile_resolution);
    EXPECT_EQ(
      summary.hierarchy_tile_resolution, output.hierarchy_tile_resolution);
    EXPECT_EQ(summary.hierarchy_reduction, output.hierarchy_reduction);

    if (summary.sample_count > 0U) {
      found_sampled_job = true;
      near_depth_sampled = near_depth_sampled
        || JobContainsEyeDepth(job_index, plan->jobs[job_index], 1.25F);
      mid_depth_sampled = mid_depth_sampled
        || JobContainsEyeDepth(job_index, plan->jobs[job_index], 3.50F);
      EXPECT_NE(summary.flags
          & oxygen::renderer::kConventionalShadowReceiverMaskFlagValid,
        0U);
      EXPECT_NE(summary.flags
          & oxygen::renderer::kConventionalShadowReceiverMaskFlagHierarchyBuilt,
        0U);
      EXPECT_GT(summary.occupied_tile_count, 0U)
        << DescribeMaskSummaries(summaries);
      EXPECT_GT(summary.hierarchy_occupied_tile_count, 0U)
        << DescribeMaskSummaries(summaries);
      if (summary.occupied_tile_count < base_tiles_per_job) {
        found_sparse_job = true;
      }
    } else {
      EXPECT_NE(summary.flags
          & oxygen::renderer::kConventionalShadowReceiverMaskFlagEmpty,
        0U);
      EXPECT_EQ(summary.occupied_tile_count, 0U)
        << DescribeMaskSummaries(summaries);
      EXPECT_EQ(summary.hierarchy_occupied_tile_count, 0U)
        << DescribeMaskSummaries(summaries);
    }
  }

  ASSERT_TRUE(found_sampled_job) << DescribeMaskSummaries(summaries);
  ASSERT_TRUE(found_sparse_job) << DescribeMaskSummaries(summaries);
  ASSERT_TRUE(near_depth_sampled) << DescribeJobPlan(*plan);
  ASSERT_TRUE(mid_depth_sampled) << DescribeJobPlan(*plan);
}

} // namespace
