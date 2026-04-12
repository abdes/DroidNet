//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/ViewHelpers.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Renderer/FacadePresets.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/Passes/ConventionalShadowRasterPass.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Renderer/Test/DepthPrePass/DepthPrePassGpuTestFixture.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

namespace {

using oxygen::Format;
using oxygen::observer_ptr;
using oxygen::TextureType;
using oxygen::engine::ConventionalShadowRasterPass;
using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::Renderer;
using oxygen::engine::ViewConstants;
using oxygen::engine::testing::DepthPrePassGpuTestFixture;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::FramebufferDesc;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::Texture;
using oxygen::scene::DirectionalLight;
using oxygen::scene::Scene;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;

auto PreparePassResources(ConventionalShadowRasterPass& pass,
  const oxygen::engine::RenderContext& rc, CommandRecorder& recorder) -> void
{
  oxygen::co::testing::TestEventLoop loop {};
  oxygen::co::Run(loop, [&]() -> oxygen::co::Co<> {
    co_await pass.PrepareResources(rc, recorder);
    co_return;
  });
}

auto MakeShadowViewConstants(const oxygen::ResolvedView& resolved_view,
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

[[nodiscard]] auto MakeShadowResolvedView(
  const std::uint32_t width, const std::uint32_t height) -> oxygen::ResolvedView
{
  const auto aspect_ratio = height == 0U
    ? 1.0F
    : static_cast<float>(width) / static_cast<float>(height);
  const auto view_matrix = glm::lookAtRH(glm::vec3 { 0.0F, 0.0F, 0.0F },
    glm::vec3 { 0.0F, 0.0F, -1.0F }, glm::vec3 { 0.0F, 1.0F, 0.0F });
  const auto projection_matrix
    = oxygen::MakeReversedZPerspectiveProjectionRH_ZO(
      glm::radians(90.0F), aspect_ratio, 0.1F, 100.0F);

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
    .view_matrix = view_matrix,
    .proj_matrix = projection_matrix,
    .camera_position = glm::vec3 { 0.0F, 0.0F, 0.0F },
    .depth_range = oxygen::NdcDepthRange::ZeroToOne,
    .near_plane = 0.1F,
    .far_plane = 100.0F,
  });
}

auto CreateShadowCastingDirectionalNode(Scene& scene) -> SceneNode
{
  const auto flags = SceneNode::Flags {}
                       .SetFlag(SceneNodeFlags::kVisible,
                         SceneFlag {}.SetEffectiveValueBit(true))
                       .SetFlag(SceneNodeFlags::kCastsShadows,
                         SceneFlag {}.SetEffectiveValueBit(true));

  auto node = scene.CreateNode("shadow-pass.directional-light", flags);
  EXPECT_TRUE(node.IsValid());

  auto impl = node.GetImpl();
  EXPECT_TRUE(impl.has_value());
  if (impl.has_value()) {
    impl->get().AddComponent<DirectionalLight>();
    impl->get().GetComponent<DirectionalLight>().Common().casts_shadows = true;
    impl->get().UpdateTransforms(scene);
  }

  return node;
}

class ConventionalShadowRasterPassConfigTest
  : public DepthPrePassGpuTestFixture {
protected:
  auto CreateDepthTextureArray(const std::uint32_t width,
    const std::uint32_t height, const std::uint32_t array_size,
    std::string_view debug_name) -> std::shared_ptr<Texture>
  {
    auto texture_desc = oxygen::graphics::TextureDesc {};
    texture_desc.width = width;
    texture_desc.height = height;
    texture_desc.array_size = array_size;
    texture_desc.format = Format::kDepth32;
    texture_desc.texture_type = TextureType::kTexture2DArray;
    texture_desc.is_shader_resource = true;
    texture_desc.is_render_target = true;
    texture_desc.is_typeless = true;
    texture_desc.use_clear_value = true;
    texture_desc.clear_value
      = oxygen::graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F };
    texture_desc.initial_state = ResourceStates::kCommon;
    texture_desc.debug_name = std::string(debug_name);
    return Backend().CreateTexture(texture_desc);
  }

  auto MakeDepthFramebuffer(const std::shared_ptr<Texture>& depth_texture,
    std::string_view debug_name) -> std::shared_ptr<Framebuffer>
  {
    CHECK_NOTNULL_F(depth_texture.get(),
      "Conventional shadow config tests require a depth target for '{}'",
      debug_name);
    auto framebuffer_desc = FramebufferDesc {};
    framebuffer_desc.SetDepthAttachment({ .texture = depth_texture });
    auto framebuffer = Backend().CreateFramebuffer(framebuffer_desc);
    CHECK_NOTNULL_F(framebuffer.get(),
      "Failed to create conventional shadow config framebuffer for '{}'",
      debug_name);
    return framebuffer;
  }
};

NOLINT_TEST_F(ConventionalShadowRasterPassConfigTest,
  RejectsNonArrayDepthTextureDuringPrepareResources)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto depth_texture = CreateDepthTexture(8U, 8U, "shadow-pass.invalid-depth");
  ASSERT_NE(depth_texture, nullptr);

  auto pass = ConventionalShadowRasterPass(
    std::make_shared<ConventionalShadowRasterPass::Config>(
      ConventionalShadowRasterPass::Config {
        .depth_texture = depth_texture,
        .debug_name = "shadow-pass.invalid-depth",
      }));

  auto framebuffer = MakeDepthFramebuffer(depth_texture, "shadow-pass.invalid");
  auto harness = oxygen::renderer::harness::single_pass::presets::
    ForResolvedViewGraphicsPass(*renderer,
      oxygen::engine::Renderer::FrameSessionInput {
        .frame_slot = Slot { 0U },
        .frame_sequence = SequenceNumber { 1U },
      },
      oxygen::observer_ptr<const Framebuffer> { framebuffer.get() },
      oxygen::engine::Renderer::ResolvedViewInput {
        .view_id = kTestViewId,
        .value = MakeShadowResolvedView(8U, 8U),
      });
  auto harness_result = harness.Finalize();
  ASSERT_TRUE(harness_result.has_value());
  auto& render_context = harness_result->GetRenderContext();

  auto recorder = AcquireRecorder("shadow-pass.invalid-depth.prepare");
  ASSERT_NE(recorder, nullptr);
  EnsureTracked(*recorder, depth_texture, ResourceStates::kCommon);

  EXPECT_THROW(PreparePassResources(pass, render_context, *recorder),
    std::invalid_argument);
}

NOLINT_TEST_F(ConventionalShadowRasterPassConfigTest,
  AcceptsTextureArrayDepthTargetDuringPrepareResources)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto depth_texture
    = CreateDepthTextureArray(8U, 8U, 4U, "shadow-pass.array-depth");
  ASSERT_NE(depth_texture, nullptr);

  auto pass = ConventionalShadowRasterPass(
    std::make_shared<ConventionalShadowRasterPass::Config>(
      ConventionalShadowRasterPass::Config {
        .depth_texture = depth_texture,
        .debug_name = "shadow-pass.array-depth",
      }));

  auto framebuffer = MakeDepthFramebuffer(depth_texture, "shadow-pass.array");
  auto harness = oxygen::renderer::harness::single_pass::presets::
    ForResolvedViewGraphicsPass(*renderer,
      oxygen::engine::Renderer::FrameSessionInput {
        .frame_slot = Slot { 0U },
        .frame_sequence = SequenceNumber { 2U },
      },
      oxygen::observer_ptr<const Framebuffer> { framebuffer.get() },
      oxygen::engine::Renderer::ResolvedViewInput {
        .view_id = kTestViewId,
        .value = MakeShadowResolvedView(8U, 8U),
      });
  auto harness_result = harness.Finalize();
  ASSERT_TRUE(harness_result.has_value());
  auto& render_context = harness_result->GetRenderContext();

  auto recorder = AcquireRecorder("shadow-pass.array-depth.prepare");
  ASSERT_NE(recorder, nullptr);
  EnsureTracked(*recorder, depth_texture, ResourceStates::kCommon);

  EXPECT_NO_THROW(PreparePassResources(pass, render_context, *recorder));
}

NOLINT_TEST_F(ConventionalShadowRasterPassConfigTest,
  PrepareResourcesUsesShadowManagerAuthoritativeDepthTexture)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto scene = std::make_shared<Scene>("shadow-pass.scene", 64U);
  auto directional_node = CreateShadowCastingDirectionalNode(*scene);
  auto directional_impl = directional_node.GetImpl();
  ASSERT_TRUE(directional_impl.has_value());

  constexpr auto kFrameSlot = Slot { 0U };
  constexpr auto kFrameSequence = SequenceNumber { 3U };
  constexpr std::uint32_t kWidth = 8U;
  constexpr std::uint32_t kHeight = 8U;

  const auto resolved_view = MakeShadowResolvedView(kWidth, kHeight);
  const auto view_constants
    = MakeShadowViewConstants(resolved_view, kFrameSlot, kFrameSequence);
  auto prepared_frame = PreparedSceneFrame {};
  auto bootstrap_depth
    = CreateDepthTextureArray(kWidth, kHeight, 1U, "shadow-pass.bootstrap");
  ASSERT_NE(bootstrap_depth, nullptr);
  auto framebuffer
    = MakeDepthFramebuffer(bootstrap_depth, "shadow-pass.sync-bootstrap");
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

  const auto light_manager = renderer->GetLightManager();
  ASSERT_NE(light_manager.get(), nullptr);
  light_manager->CollectFromNode(
    directional_node.GetHandle(), directional_impl->get());

  const auto shadow_manager = renderer->GetShadowManager();
  ASSERT_NE(shadow_manager.get(), nullptr);
  shadow_manager->ReserveFrameResources(1U, *light_manager);

  const auto shadow_caster_bounds
    = std::array { glm::vec4 { 0.0F, 0.0F, 0.0F, 4.0F } };
  const auto publication = shadow_manager->PublishForView(kTestViewId,
    view_constants, *light_manager, observer_ptr<Scene> { scene.get() },
    static_cast<float>(kWidth), {}, shadow_caster_bounds);
  EXPECT_NE(publication.directional_shadow_texture_srv,
    oxygen::kInvalidShaderVisibleIndex);

  const auto authoritative_depth_texture
    = shadow_manager->GetConventionalShadowDepthTexture();
  ASSERT_NE(authoritative_depth_texture, nullptr);

  const auto* raster_plan = shadow_manager->TryGetRasterRenderPlan(kTestViewId);
  ASSERT_NE(raster_plan, nullptr);
  ASSERT_NE(raster_plan->depth_texture, nullptr);
  ASSERT_FALSE(raster_plan->jobs.empty());
  EXPECT_EQ(
    raster_plan->depth_texture.get(), authoritative_depth_texture.get());

  auto stale_depth_texture
    = CreateDepthTextureArray(kWidth, kHeight, 1U, "shadow-pass.stale-depth");
  ASSERT_NE(stale_depth_texture, nullptr);

  auto pass = ConventionalShadowRasterPass(
    std::make_shared<ConventionalShadowRasterPass::Config>(
      ConventionalShadowRasterPass::Config {
        .depth_texture = stale_depth_texture,
        .debug_name = "shadow-pass.sync-to-shadow-manager",
      }));

  auto recorder = AcquireRecorder("shadow-pass.sync-to-shadow-manager.prepare");
  ASSERT_NE(recorder, nullptr);
  EnsureTracked(
    *recorder, authoritative_depth_texture, ResourceStates::kCommon);

  EXPECT_NO_THROW(PreparePassResources(pass, render_context, *recorder));
}

} // namespace
