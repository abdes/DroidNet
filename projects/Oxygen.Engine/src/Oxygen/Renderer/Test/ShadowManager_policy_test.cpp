//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>
#include <string>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewHelpers.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/ScenePrep/Handles.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemData.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>
#include <Oxygen/Renderer/Upload/InlineTransfersCoordinator.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::renderer::internal {
auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}
} // namespace oxygen::renderer::internal

namespace {

using oxygen::DirectionalShadowImplementationPolicy;
using oxygen::NdcDepthRange;
using oxygen::observer_ptr;
using oxygen::ResolvedView;
using oxygen::Scissors;
using oxygen::View;
using oxygen::ViewPort;
using oxygen::engine::ViewConstants;
using oxygen::engine::sceneprep::RenderItemData;
using oxygen::engine::sceneprep::TransformHandle;
using oxygen::engine::upload::DefaultUploadPolicy;
using oxygen::engine::upload::InlineTransfersCoordinator;
using oxygen::engine::upload::StagingProvider;
using oxygen::engine::upload::UploadCoordinator;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::SingleQueueStrategy;
using oxygen::renderer::LightManager;
using oxygen::renderer::ShadowManager;
using oxygen::renderer::internal::RendererTagFactory;
using oxygen::renderer::testing::FakeGraphics;
using oxygen::scene::DirectionalCsmSplitMode;
using oxygen::scene::DirectionalLight;
using oxygen::scene::PointLight;
using oxygen::scene::Scene;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;
using oxygen::scene::ShadowResolutionHint;

class ShadowManagerPolicyTest : public testing::Test {
protected:
  auto SetUp() -> void override
  {
    gfx_ = std::make_shared<FakeGraphics>();
    gfx_->CreateCommandQueues(SingleQueueStrategy());

    uploader_ = std::make_unique<UploadCoordinator>(
      observer_ptr { gfx_.get() }, DefaultUploadPolicy());

    staging_provider_ = uploader_->CreateRingBufferStaging(
      oxygen::frame::SlotCount { 1 }, 256u);

    inline_transfers_ = std::make_unique<InlineTransfersCoordinator>(
      observer_ptr { gfx_.get() });

    scene_ = std::make_shared<Scene>("ShadowManagerPolicyTestScene", 64);
  }

  [[nodiscard]] auto MakeShadowManager(
    const DirectionalShadowImplementationPolicy policy) const
    -> std::unique_ptr<ShadowManager>
  {
    return std::make_unique<ShadowManager>(observer_ptr { gfx_.get() },
      observer_ptr { staging_provider_.get() },
      observer_ptr { inline_transfers_.get() },
      oxygen::ShadowQualityTier::kHigh, policy);
  }

  [[nodiscard]] auto MakeLightManager() const -> LightManager
  {
    return LightManager(observer_ptr { gfx_.get() },
      observer_ptr { staging_provider_.get() },
      observer_ptr { inline_transfers_.get() });
  }

  [[nodiscard]] auto CreateNode(const std::string& name, const bool visible,
    const bool casts_shadows) const -> SceneNode
  {
    const auto flags = SceneNode::Flags {}
                         .SetFlag(SceneNodeFlags::kVisible,
                           SceneFlag {}.SetEffectiveValueBit(visible))
                         .SetFlag(SceneNodeFlags::kCastsShadows,
                           SceneFlag {}.SetEffectiveValueBit(casts_shadows));

    auto node = scene_->CreateNode(name, flags);
    EXPECT_TRUE(node.IsValid());
    return node;
  }

  auto UpdateTransforms(SceneNode& node) const -> void
  {
    auto impl = node.GetImpl();
    ASSERT_TRUE(impl.has_value());
    impl->get().UpdateTransforms(*scene_);
  }

  [[nodiscard]] auto GetScene() const -> const std::shared_ptr<Scene>&
  {
    return scene_;
  }

  [[nodiscard]] static auto MakeViewConstants() -> ViewConstants
  {
    auto view_constants = ViewConstants {};
    view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer)
      .SetFrameSequenceNumber(SequenceNumber { 1 }, ViewConstants::kRenderer)
      .SetBindlessViewFrameBindingsSlot(
        oxygen::engine::BindlessViewFrameBindingsSlot {
          oxygen::ShaderVisibleIndex { 1U } },
        ViewConstants::kRenderer)
      .SetTimeSeconds(0.0F, ViewConstants::kRenderer);
    return view_constants;
  }

  [[nodiscard]] static auto MakeResolvedView() -> ResolvedView
  {
    return MakeResolvedView(0.1F, 100.0F, 90.0F);
  }

  [[nodiscard]] static auto MakeResolvedView(const float near_plane,
    const float far_plane, const float fov_degrees = 90.0F) -> ResolvedView
  {
    const auto view_matrix = glm::lookAtRH(glm::vec3 { 0.0F, 0.0F, 0.0F },
      glm::vec3 { 0.0F, 0.0F, -1.0F }, glm::vec3 { 0.0F, 1.0F, 0.0F });
    const auto projection_matrix
      = oxygen::MakeReversedZPerspectiveProjectionRH_ZO(
        glm::radians(fov_degrees), 1.0F, near_plane, far_plane);

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
      .near_plane = near_plane,
      .far_plane = far_plane,
    });
  }

  [[nodiscard]] static auto MakeViewConstants(const ResolvedView& resolved_view)
    -> ViewConstants
  {
    auto view_constants = MakeViewConstants();
    view_constants.SetViewMatrix(resolved_view.ViewMatrix())
      .SetProjectionMatrix(resolved_view.ProjectionMatrix())
      .SetCameraPosition(resolved_view.CameraPosition());
    return view_constants;
  }

  [[nodiscard]] static auto HasValidDepthSpan(
    const ViewConstants& view_constants) -> bool
  {
    const auto snapshot = view_constants.GetSnapshot();
    const auto inv_proj = glm::inverse(snapshot.projection_matrix);
    constexpr auto kMinCascadeSpan = 0.1F;
    constexpr std::array<glm::vec2, 4> clip_corners {
      glm::vec2(-1.0F, -1.0F),
      glm::vec2(1.0F, -1.0F),
      glm::vec2(1.0F, 1.0F),
      glm::vec2(-1.0F, 1.0F),
    };

    auto transform_point = [](const glm::mat4& matrix, const glm::vec3 point) {
      const auto transformed = matrix * glm::vec4(point, 1.0F);
      const auto inv_w
        = std::abs(transformed.w) > 1.0e-6F ? (1.0F / transformed.w) : 1.0F;
      return glm::vec3(transformed) * inv_w;
    };

    auto near_depth = 0.0F;
    auto far_depth = 0.0F;
    for (const auto& clip_corner : clip_corners) {
      const auto near_corner
        = transform_point(inv_proj, glm::vec3(clip_corner, 1.0F));
      const auto far_corner
        = transform_point(inv_proj, glm::vec3(clip_corner, 0.0F));
      near_depth += std::max(0.0F, -near_corner.z);
      far_depth += std::max(0.0F, -far_corner.z);
    }

    near_depth /= static_cast<float>(clip_corners.size());
    far_depth /= static_cast<float>(clip_corners.size());
    return far_depth > near_depth + kMinCascadeSpan;
  }

private:
  std::shared_ptr<FakeGraphics> gfx_;
  std::unique_ptr<UploadCoordinator> uploader_;
  std::shared_ptr<StagingProvider> staging_provider_;
  std::unique_ptr<InlineTransfersCoordinator> inline_transfers_;
  std::shared_ptr<Scene> scene_;
};

NOLINT_TEST_F(ShadowManagerPolicyTest, ConventionalPolicyDoesNotCreateVsmShell)
{
  auto manager = MakeShadowManager(
    DirectionalShadowImplementationPolicy::kConventionalOnly);

  EXPECT_EQ(manager->GetVirtualShadowRenderer().get(), nullptr);
}

NOLINT_TEST_F(ShadowManagerPolicyTest, VsmPolicyCreatesVsmShell)
{
  auto manager = MakeShadowManager(
    DirectionalShadowImplementationPolicy::kVirtualShadowMap);

  EXPECT_NE(manager->GetVirtualShadowRenderer().get(), nullptr);
}

NOLINT_TEST_F(
  ShadowManagerPolicyTest, FrameLifecycleCallsAreSafeForBothPolicies)
{
  auto conventional = MakeShadowManager(
    DirectionalShadowImplementationPolicy::kConventionalOnly);
  auto vsm = MakeShadowManager(
    DirectionalShadowImplementationPolicy::kVirtualShadowMap);

  conventional->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 7 }, Slot { 0 });
  vsm->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 7 }, Slot { 0 });

  conventional->ResetCachedState();
  vsm->ResetCachedState();

  SUCCEED();
}

NOLINT_TEST_F(ShadowManagerPolicyTest,
  EmptyPublishForViewRemainsSafeAndRetainsConventionalPublicationPath)
{
  auto view_constants = MakeViewConstants();
  auto lights = MakeLightManager();
  lights.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  for (const auto policy :
    { DirectionalShadowImplementationPolicy::kConventionalOnly,
      DirectionalShadowImplementationPolicy::kVirtualShadowMap }) {
    auto manager = MakeShadowManager(policy);
    manager->OnFrameStart(
      RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

    const auto publication = manager->PublishForView(oxygen::ViewId { 1 },
      view_constants, lights, observer_ptr<Scene> {}, 1920.0F);

    EXPECT_EQ(publication.shadow_instance_metadata_srv,
      oxygen::kInvalidShaderVisibleIndex);
    EXPECT_EQ(publication.directional_shadow_metadata_srv,
      oxygen::kInvalidShaderVisibleIndex);
    EXPECT_NE(manager->TryGetFramePublication(oxygen::ViewId { 1 }), nullptr);
  }
}

NOLINT_TEST_F(ShadowManagerPolicyTest,
  ConventionalPolicyPublishesConventionalDirectionalProductsAndRasterPlan)
{
  auto manager = MakeShadowManager(
    DirectionalShadowImplementationPolicy::kConventionalOnly);
  auto lights = MakeLightManager();
  const auto resolved_view = MakeResolvedView();
  auto view_constants = MakeViewConstants(resolved_view);

  auto directional_node
    = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto directional_impl = directional_node.GetImpl();
  ASSERT_TRUE(directional_impl.has_value());
  directional_impl->get().AddComponent<DirectionalLight>();
  directional_impl->get()
    .GetComponent<DirectionalLight>()
    .Common()
    .casts_shadows
    = true;
  UpdateTransforms(directional_node);

  lights.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 4 }, Slot { 0 });
  lights.CollectFromNode(directional_node.GetHandle(), directional_impl->get());
  manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 4 }, Slot { 0 });

  EXPECT_TRUE(HasValidDepthSpan(view_constants));

  const auto publication = manager->PublishForView(oxygen::ViewId { 5 },
    view_constants, lights, observer_ptr<Scene> { GetScene().get() }, 1280.0F,
    {}, std::array { glm::vec4 { 0.0F, 0.0F, 0.0F, 4.0F } });

  EXPECT_NE(publication.shadow_instance_metadata_srv,
    oxygen::kInvalidShaderVisibleIndex);
  const auto* shadow_instance
    = manager->TryGetShadowInstanceMetadata(oxygen::ViewId { 5 });
  ASSERT_NE(shadow_instance, nullptr);
  EXPECT_EQ(shadow_instance->implementation_kind,
    static_cast<std::uint32_t>(
      oxygen::engine::ShadowImplementationKind::kConventional));
  EXPECT_NE(manager->TryGetRasterRenderPlan(oxygen::ViewId { 5 }), nullptr);
}

NOLINT_TEST_F(ShadowManagerPolicyTest,
  ConventionalPolicyAssignsDistinctDirectionalShadowSlicesPerPublishedView)
{
  auto manager = MakeShadowManager(
    DirectionalShadowImplementationPolicy::kConventionalOnly);
  auto lights = MakeLightManager();
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
  const auto resolved_view = ResolvedView(ResolvedView::Params {
    .view_config = view_config,
    .view_matrix = glm::lookAtRH(glm::vec3 { 0.0F, 0.0F, 0.0F },
      glm::vec3 { 0.0F, 0.0F, -1.0F }, glm::vec3 { 0.0F, 1.0F, 0.0F }),
    .proj_matrix = oxygen::MakeReversedZPerspectiveProjectionRH_ZO(
      glm::radians(20.0F), 1.0F, 0.1F, 30.0F),
    .camera_position = glm::vec3 { 0.0F, 0.0F, 0.0F },
    .depth_range = NdcDepthRange::ZeroToOne,
    .near_plane = 0.1F,
    .far_plane = 30.0F,
  });
  auto view_constants = MakeViewConstants(resolved_view);

  auto directional_node
    = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto directional_impl = directional_node.GetImpl();
  ASSERT_TRUE(directional_impl.has_value());
  directional_impl->get().AddComponent<DirectionalLight>();
  directional_impl->get()
    .GetComponent<DirectionalLight>()
    .Common()
    .casts_shadows
    = true;
  UpdateTransforms(directional_node);

  lights.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 5 }, Slot { 0 });
  lights.CollectFromNode(directional_node.GetHandle(), directional_impl->get());
  manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 5 }, Slot { 0 });
  manager->ReserveFrameResources(2U, lights);

  const auto shadow_caster_bounds
    = std::array { glm::vec4 { 0.0F, 0.0F, 0.0F, 4.0F } };
  const auto publication_a = manager->PublishForView(oxygen::ViewId { 5 },
    view_constants, lights, observer_ptr<Scene> { GetScene().get() }, 1280.0F,
    {}, shadow_caster_bounds);
  const auto publication_b = manager->PublishForView(oxygen::ViewId { 6 },
    view_constants, lights, observer_ptr<Scene> { GetScene().get() }, 576.0F,
    {}, shadow_caster_bounds);

  ASSERT_NE(publication_a.shadow_instance_metadata_srv,
    oxygen::kInvalidShaderVisibleIndex);
  ASSERT_NE(publication_b.shadow_instance_metadata_srv,
    oxygen::kInvalidShaderVisibleIndex);
  ASSERT_NE(publication_a.directional_shadow_texture_srv,
    oxygen::kInvalidShaderVisibleIndex);
  ASSERT_EQ(publication_a.directional_shadow_texture_srv,
    publication_b.directional_shadow_texture_srv);

  const auto* plan_a = manager->TryGetRasterRenderPlan(oxygen::ViewId { 5 });
  const auto* plan_b = manager->TryGetRasterRenderPlan(oxygen::ViewId { 6 });
  ASSERT_NE(plan_a, nullptr);
  ASSERT_NE(plan_b, nullptr);
  ASSERT_FALSE(plan_a->jobs.empty());
  ASSERT_FALSE(plan_b->jobs.empty());
  ASSERT_EQ(plan_a->jobs.size(), plan_b->jobs.size());

  std::vector<std::uint32_t> slices_a {};
  std::vector<std::uint32_t> slices_b {};
  slices_a.reserve(plan_a->jobs.size());
  slices_b.reserve(plan_b->jobs.size());
  for (const auto& job : plan_a->jobs) {
    slices_a.push_back(job.target_array_slice);
  }
  for (const auto& job : plan_b->jobs) {
    slices_b.push_back(job.target_array_slice);
  }

  EXPECT_EQ(slices_a, (std::vector<std::uint32_t> { 0U, 1U, 2U, 3U }));
  EXPECT_EQ(slices_b, (std::vector<std::uint32_t> { 4U, 5U, 6U, 7U }));
  ASSERT_NE(plan_b->depth_texture, nullptr);
  EXPECT_GE(plan_b->depth_texture->GetDescriptor().array_size,
    slices_a.size() + slices_b.size());
}

NOLINT_TEST_F(ShadowManagerPolicyTest,
  ConventionalPolicyRepublishesWhenVisibleReceiverBoundsChange)
{
  auto manager = MakeShadowManager(
    DirectionalShadowImplementationPolicy::kConventionalOnly);
  auto lights = MakeLightManager();
  const auto resolved_view = MakeResolvedView();
  auto view_constants = MakeViewConstants(resolved_view);

  auto directional_node
    = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto directional_impl = directional_node.GetImpl();
  ASSERT_TRUE(directional_impl.has_value());
  directional_impl->get().AddComponent<DirectionalLight>();
  directional_impl->get()
    .GetComponent<DirectionalLight>()
    .Common()
    .casts_shadows
    = true;
  UpdateTransforms(directional_node);

  lights.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 6 }, Slot { 0 });
  lights.CollectFromNode(directional_node.GetHandle(), directional_impl->get());
  manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 6 }, Slot { 0 });

  const auto shadow_caster_bounds
    = std::array { glm::vec4 { 0.0F, 0.0F, 0.0F, 8.0F } };
  const auto left_receivers
    = std::array { glm::vec4 { -6.0F, 0.0F, -8.0F, 2.5F } };
  const auto right_receivers
    = std::array { glm::vec4 { 6.0F, 0.0F, -8.0F, 2.5F } };

  const auto publication_left = manager->PublishForView(oxygen::ViewId { 5 },
    view_constants, lights, observer_ptr<Scene> { GetScene().get() }, 1280.0F,
    {}, shadow_caster_bounds, left_receivers);
  const auto* plan_left = manager->TryGetRasterRenderPlan(oxygen::ViewId { 5 });
  ASSERT_NE(publication_left.shadow_instance_metadata_srv,
    oxygen::kInvalidShaderVisibleIndex);
  ASSERT_NE(publication_left.directional_shadow_metadata_srv,
    oxygen::kInvalidShaderVisibleIndex);
  ASSERT_NE(publication_left.directional_shadow_texture_srv,
    oxygen::kInvalidShaderVisibleIndex);
  ASSERT_NE(plan_left, nullptr);
  ASSERT_FALSE(plan_left->jobs.empty());

  const auto publication_right = manager->PublishForView(oxygen::ViewId { 5 },
    view_constants, lights, observer_ptr<Scene> { GetScene().get() }, 1280.0F,
    {}, shadow_caster_bounds, right_receivers);
  const auto* plan_right
    = manager->TryGetRasterRenderPlan(oxygen::ViewId { 5 });
  ASSERT_NE(publication_right.shadow_instance_metadata_srv,
    oxygen::kInvalidShaderVisibleIndex);
  ASSERT_NE(publication_right.directional_shadow_metadata_srv,
    oxygen::kInvalidShaderVisibleIndex);
  ASSERT_NE(plan_right, nullptr);
  ASSERT_FALSE(plan_right->jobs.empty());

  EXPECT_EQ(publication_left.directional_shadow_texture_srv,
    publication_right.directional_shadow_texture_srv);
  EXPECT_TRUE(publication_left.shadow_instance_metadata_srv
      != publication_right.shadow_instance_metadata_srv
    || publication_left.directional_shadow_metadata_srv
      != publication_right.directional_shadow_metadata_srv);
}

NOLINT_TEST_F(ShadowManagerPolicyTest,
  ConventionalPolicyPublishesExpectedDirectionalCascadeSplitDepths)
{
  auto manager = MakeShadowManager(
    DirectionalShadowImplementationPolicy::kConventionalOnly);
  auto lights = MakeLightManager();
  const auto resolved_view = MakeResolvedView(1.0F, 300.0F, 75.0F);
  auto view_constants = MakeViewConstants(resolved_view);

  auto directional_node
    = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto directional_impl = directional_node.GetImpl();
  ASSERT_TRUE(directional_impl.has_value());
  directional_impl->get().AddComponent<DirectionalLight>();
  auto& directional_light
    = directional_impl->get().GetComponent<DirectionalLight>();
  directional_light.Common().casts_shadows = true;
  directional_light.CascadedShadows().split_mode
    = DirectionalCsmSplitMode::kManualDistances;
  directional_light.CascadedShadows().cascade_count = 4U;
  directional_light.CascadedShadows().cascade_distances
    = { 8.0F, 24.0F, 64.0F, 160.0F };
  UpdateTransforms(directional_node);

  lights.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 14 }, Slot { 0 });
  lights.CollectFromNode(directional_node.GetHandle(), directional_impl->get());
  manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 14 }, Slot { 0 });

  const auto publication = manager->PublishForView(oxygen::ViewId { 8 },
    view_constants, lights, observer_ptr<Scene> { GetScene().get() }, 1280.0F,
    {}, std::array { glm::vec4 { 0.0F, 0.0F, -40.0F, 8.0F } });
  ASSERT_NE(publication.shadow_instance_metadata_srv,
    oxygen::kInvalidShaderVisibleIndex);

  const auto* analysis_plan
    = manager->TryGetReceiverAnalysisPlan(oxygen::ViewId { 8 });
  ASSERT_NE(analysis_plan, nullptr);
  ASSERT_EQ(analysis_plan->jobs.size(), 4U);

  constexpr auto kEpsilon = 1.0e-3F;
  EXPECT_NEAR(
    analysis_plan->jobs[0].split_and_full_depth_range.x, 1.0F, kEpsilon);
  EXPECT_NEAR(
    analysis_plan->jobs[0].split_and_full_depth_range.y, 8.0F, kEpsilon);
  EXPECT_NEAR(
    analysis_plan->jobs[1].split_and_full_depth_range.x, 8.0F, kEpsilon);
  EXPECT_NEAR(
    analysis_plan->jobs[1].split_and_full_depth_range.y, 24.0F, kEpsilon);
  EXPECT_NEAR(
    analysis_plan->jobs[2].split_and_full_depth_range.x, 24.0F, kEpsilon);
  EXPECT_NEAR(
    analysis_plan->jobs[2].split_and_full_depth_range.y, 64.0F, kEpsilon);
  EXPECT_NEAR(
    analysis_plan->jobs[3].split_and_full_depth_range.x, 64.0F, kEpsilon);
  EXPECT_NEAR(
    analysis_plan->jobs[3].split_and_full_depth_range.y, 160.0F, kEpsilon);
}

NOLINT_TEST_F(ShadowManagerPolicyTest,
  ConventionalPolicyPublishesIncreasingWorldTexelSizeAcrossCascades)
{
  auto manager = MakeShadowManager(
    DirectionalShadowImplementationPolicy::kConventionalOnly);
  auto lights = MakeLightManager();
  const auto resolved_view = MakeResolvedView(1.0F, 300.0F, 75.0F);
  auto view_constants = MakeViewConstants(resolved_view);

  auto directional_node
    = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto directional_impl = directional_node.GetImpl();
  ASSERT_TRUE(directional_impl.has_value());
  directional_impl->get().AddComponent<DirectionalLight>();
  auto& directional_light
    = directional_impl->get().GetComponent<DirectionalLight>();
  directional_light.Common().casts_shadows = true;
  directional_light.CascadedShadows().split_mode
    = DirectionalCsmSplitMode::kManualDistances;
  directional_light.CascadedShadows().cascade_count = 4U;
  directional_light.CascadedShadows().cascade_distances
    = { 8.0F, 24.0F, 64.0F, 160.0F };
  UpdateTransforms(directional_node);

  lights.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 15 }, Slot { 0 });
  lights.CollectFromNode(directional_node.GetHandle(), directional_impl->get());
  manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 15 }, Slot { 0 });

  const auto publication = manager->PublishForView(oxygen::ViewId { 9 },
    view_constants, lights, observer_ptr<Scene> { GetScene().get() }, 1280.0F,
    {}, std::array { glm::vec4 { 0.0F, 0.0F, -40.0F, 8.0F } });
  ASSERT_NE(publication.shadow_instance_metadata_srv,
    oxygen::kInvalidShaderVisibleIndex);

  const auto* analysis_plan
    = manager->TryGetReceiverAnalysisPlan(oxygen::ViewId { 9 });
  ASSERT_NE(analysis_plan, nullptr);
  ASSERT_EQ(analysis_plan->jobs.size(), 4U);

  const float cascade0_texel_world = analysis_plan->jobs[0].shading_margins.x;
  const float cascade1_texel_world = analysis_plan->jobs[1].shading_margins.x;
  const float cascade2_texel_world = analysis_plan->jobs[2].shading_margins.x;
  const float cascade3_texel_world = analysis_plan->jobs[3].shading_margins.x;

  EXPECT_GT(cascade0_texel_world, 0.0F);
  EXPECT_GT(cascade1_texel_world, cascade0_texel_world)
    << "near cascades should publish finer texel density than farther cascades";
  EXPECT_GT(cascade2_texel_world, cascade1_texel_world)
    << "cascade 2 should be coarser than cascade 1";
  EXPECT_GT(cascade3_texel_world, cascade2_texel_world)
    << "cascade 3 should be coarser than cascade 2";
}

NOLINT_TEST_F(ShadowManagerPolicyTest,
  ConventionalPolicyPublishesGeneratedDirectionalCascadeSplitDepths)
{
  auto manager = MakeShadowManager(
    DirectionalShadowImplementationPolicy::kConventionalOnly);
  auto lights = MakeLightManager();
  const auto resolved_view = MakeResolvedView(1.0F, 300.0F, 75.0F);
  auto view_constants = MakeViewConstants(resolved_view);

  auto directional_node
    = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto directional_impl = directional_node.GetImpl();
  ASSERT_TRUE(directional_impl.has_value());
  directional_impl->get().AddComponent<DirectionalLight>();
  auto& directional_light
    = directional_impl->get().GetComponent<DirectionalLight>();
  directional_light.Common().casts_shadows = true;
  directional_light.CascadedShadows().split_mode
    = DirectionalCsmSplitMode::kGenerated;
  directional_light.CascadedShadows().cascade_count = 4U;
  directional_light.CascadedShadows().max_shadow_distance = 160.0F;
  directional_light.CascadedShadows().distribution_exponent = 3.0F;
  UpdateTransforms(directional_node);

  lights.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 16 }, Slot { 0 });
  lights.CollectFromNode(directional_node.GetHandle(), directional_impl->get());
  manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 16 }, Slot { 0 });

  const auto publication = manager->PublishForView(oxygen::ViewId { 10 },
    view_constants, lights, observer_ptr<Scene> { GetScene().get() }, 1280.0F,
    {}, std::array { glm::vec4 { 0.0F, 0.0F, -40.0F, 8.0F } });
  ASSERT_NE(publication.shadow_instance_metadata_srv,
    oxygen::kInvalidShaderVisibleIndex);

  const auto* analysis_plan
    = manager->TryGetReceiverAnalysisPlan(oxygen::ViewId { 10 });
  ASSERT_NE(analysis_plan, nullptr);
  ASSERT_EQ(analysis_plan->jobs.size(), 4U);

  constexpr auto kEpsilon = 1.0e-3F;
  EXPECT_NEAR(
    analysis_plan->jobs[0].split_and_full_depth_range.x, 1.0F, kEpsilon);
  EXPECT_NEAR(
    analysis_plan->jobs[0].split_and_full_depth_range.y, 4.975F, kEpsilon);
  EXPECT_NEAR(
    analysis_plan->jobs[1].split_and_full_depth_range.x, 4.975F, kEpsilon);
  EXPECT_NEAR(
    analysis_plan->jobs[1].split_and_full_depth_range.y, 16.9F, kEpsilon);
  EXPECT_NEAR(
    analysis_plan->jobs[2].split_and_full_depth_range.x, 16.9F, kEpsilon);
  EXPECT_NEAR(
    analysis_plan->jobs[2].split_and_full_depth_range.y, 52.675F, kEpsilon);
  EXPECT_NEAR(
    analysis_plan->jobs[3].split_and_full_depth_range.x, 52.675F, kEpsilon);
  EXPECT_NEAR(
    analysis_plan->jobs[3].split_and_full_depth_range.y, 160.0F, kEpsilon);
}

NOLINT_TEST_F(
  ShadowManagerPolicyTest, ConventionalPolicyAppliesDirectionalCsmRuntimeClamps)
{
  auto manager = MakeShadowManager(
    DirectionalShadowImplementationPolicy::kConventionalOnly);
  manager->SetDirectionalCsmRuntimeSettings(
    oxygen::renderer::DirectionalCsmRuntimeSettings {
      .distance_scale = 0.5F,
      .transition_scale = 1.5F,
      .max_cascades = 2U,
      .max_resolution = 2048U,
    });
  auto lights = MakeLightManager();
  const auto resolved_view = MakeResolvedView(1.0F, 300.0F, 75.0F);
  auto view_constants = MakeViewConstants(resolved_view);

  auto directional_node
    = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto directional_impl = directional_node.GetImpl();
  ASSERT_TRUE(directional_impl.has_value());
  directional_impl->get().AddComponent<DirectionalLight>();
  auto& directional_light
    = directional_impl->get().GetComponent<DirectionalLight>();
  directional_light.Common().casts_shadows = true;
  directional_light.Common().shadow.resolution_hint
    = oxygen::scene::ShadowResolutionHint::kUltra;
  directional_light.CascadedShadows().split_mode
    = DirectionalCsmSplitMode::kGenerated;
  directional_light.CascadedShadows().cascade_count = 4U;
  directional_light.CascadedShadows().max_shadow_distance = 160.0F;
  directional_light.CascadedShadows().distribution_exponent = 3.0F;
  directional_light.CascadedShadows().transition_fraction = 0.1F;
  directional_light.CascadedShadows().distance_fadeout_fraction = 0.1F;
  UpdateTransforms(directional_node);

  lights.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 17 }, Slot { 0 });
  lights.CollectFromNode(directional_node.GetHandle(), directional_impl->get());
  manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 17 }, Slot { 0 });

  const auto publication = manager->PublishForView(oxygen::ViewId { 11 },
    view_constants, lights, observer_ptr<Scene> { GetScene().get() }, 1280.0F,
    {}, std::array { glm::vec4 { 0.0F, 0.0F, -40.0F, 8.0F } });
  ASSERT_NE(publication.shadow_instance_metadata_srv,
    oxygen::kInvalidShaderVisibleIndex);

  const auto* analysis_plan
    = manager->TryGetReceiverAnalysisPlan(oxygen::ViewId { 11 });
  ASSERT_NE(analysis_plan, nullptr);
  ASSERT_EQ(analysis_plan->jobs.size(), 2U);

  constexpr auto kEpsilon = 1.0e-3F;
  EXPECT_NEAR(
    analysis_plan->jobs[0].split_and_full_depth_range.x, 1.0F, kEpsilon);
  EXPECT_NEAR(
    analysis_plan->jobs[0].split_and_full_depth_range.y, 20.75F, kEpsilon);
  EXPECT_NEAR(
    analysis_plan->jobs[1].split_and_full_depth_range.x, 20.75F, kEpsilon);
  EXPECT_NEAR(
    analysis_plan->jobs[1].split_and_full_depth_range.y, 80.0F, kEpsilon);

  const auto* metadata
    = manager->TryGetDirectionalShadowMetadata(oxygen::ViewId { 11 });
  ASSERT_NE(metadata, nullptr);
  EXPECT_EQ(metadata->cascade_count, 2U);
  EXPECT_NEAR(metadata->cascade_transition_widths[0], 2.9625F, kEpsilon);
  EXPECT_FLOAT_EQ(metadata->cascade_transition_widths[1], 0.0F);
  EXPECT_NEAR(metadata->max_shadow_distance, 80.0F, kEpsilon);
  EXPECT_NEAR(metadata->distance_fadeout_begin, 74.075F, kEpsilon);

  const auto* plan = manager->TryGetRasterRenderPlan(oxygen::ViewId { 11 });
  ASSERT_NE(plan, nullptr);
  ASSERT_NE(plan->depth_texture, nullptr);
  EXPECT_EQ(plan->depth_texture->GetDescriptor().width, 2048U);
  EXPECT_EQ(plan->depth_texture->GetDescriptor().height, 2048U);
}

NOLINT_TEST_F(ShadowManagerPolicyTest,
  ConventionalPolicyRespectsDirectionalResolutionHintAndReallocatesDownward)
{
  auto manager = MakeShadowManager(
    DirectionalShadowImplementationPolicy::kConventionalOnly);
  auto lights = MakeLightManager();
  const auto resolved_view = MakeResolvedView(1.0F, 300.0F, 75.0F);

  auto directional_node
    = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto directional_impl = directional_node.GetImpl();
  ASSERT_TRUE(directional_impl.has_value());
  directional_impl->get().AddComponent<DirectionalLight>();
  auto& directional_light
    = directional_impl->get().GetComponent<DirectionalLight>();
  directional_light.Common().casts_shadows = true;
  directional_light.CascadedShadows().split_mode
    = DirectionalCsmSplitMode::kGenerated;
  directional_light.CascadedShadows().cascade_count = 4U;
  directional_light.CascadedShadows().max_shadow_distance = 160.0F;
  directional_light.CascadedShadows().distribution_exponent = 3.0F;
  directional_light.CascadedShadows().transition_fraction = 0.1F;
  directional_light.CascadedShadows().distance_fadeout_fraction = 0.1F;
  UpdateTransforms(directional_node);

  const auto publish_for_hint
    = [&](const ShadowResolutionHint hint,
        const std::uint64_t frame_number) -> std::uint32_t {
    directional_light.Common().shadow.resolution_hint = hint;

    auto view_constants = MakeViewConstants(resolved_view);
    view_constants.SetFrameSequenceNumber(
      SequenceNumber { frame_number }, ViewConstants::kRenderer);

    lights.OnFrameStart(
      RendererTagFactory::Get(), SequenceNumber { frame_number }, Slot { 0 });
    lights.CollectFromNode(
      directional_node.GetHandle(), directional_impl->get());
    manager->OnFrameStart(
      RendererTagFactory::Get(), SequenceNumber { frame_number }, Slot { 0 });

    const auto publication = manager->PublishForView(oxygen::ViewId { 11 },
      view_constants, lights, observer_ptr<Scene> { GetScene().get() }, 1280.0F,
      {}, std::array { glm::vec4 { 0.0F, 0.0F, -40.0F, 8.0F } });
    EXPECT_NE(publication.shadow_instance_metadata_srv,
      oxygen::kInvalidShaderVisibleIndex);

    const auto* plan = manager->TryGetRasterRenderPlan(oxygen::ViewId { 11 });
    EXPECT_NE(plan, nullptr);
    if (plan == nullptr) {
      return 0U;
    }

    EXPECT_NE(plan->depth_texture, nullptr);
    if (plan->depth_texture == nullptr) {
      return 0U;
    }

    EXPECT_EQ(plan->depth_texture->GetDescriptor().height,
      plan->depth_texture->GetDescriptor().width);
    return plan->depth_texture->GetDescriptor().width;
  };

  EXPECT_EQ(publish_for_hint(ShadowResolutionHint::kLow, 21U), 1024U);
  EXPECT_EQ(publish_for_hint(ShadowResolutionHint::kMedium, 22U), 2048U);
  EXPECT_EQ(publish_for_hint(ShadowResolutionHint::kHigh, 23U), 3072U);
  EXPECT_EQ(publish_for_hint(ShadowResolutionHint::kUltra, 24U), 3072U);
  EXPECT_EQ(publish_for_hint(ShadowResolutionHint::kLow, 25U), 1024U);
}

NOLINT_TEST_F(
  ShadowManagerPolicyTest, VsmPublishCapturesPerViewInputsIntoPreparedState)
{
  auto manager = MakeShadowManager(
    DirectionalShadowImplementationPolicy::kVirtualShadowMap);
  auto lights = MakeLightManager();
  auto view_constants = MakeViewConstants();
  view_constants.SetFrameSlot(Slot { 2 }, ViewConstants::kRenderer)
    .SetFrameSequenceNumber(SequenceNumber { 13 }, ViewConstants::kRenderer);

  auto directional_node
    = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto directional_impl = directional_node.GetImpl();
  ASSERT_TRUE(directional_impl.has_value());
  directional_impl->get().AddComponent<DirectionalLight>();
  directional_impl->get()
    .GetComponent<DirectionalLight>()
    .Common()
    .casts_shadows
    = true;
  UpdateTransforms(directional_node);

  auto point_node
    = CreateNode("point", /*visible=*/true, /*casts_shadows=*/true);
  auto point_impl = point_node.GetImpl();
  ASSERT_TRUE(point_impl.has_value());
  point_impl->get().AddComponent<PointLight>();
  point_impl->get().GetComponent<PointLight>().Common().casts_shadows = true;
  UpdateTransforms(point_node);

  lights.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 13 }, Slot { 2 });
  lights.CollectFromNode(directional_node.GetHandle(), directional_impl->get());
  lights.CollectFromNode(point_node.GetHandle(), point_impl->get());

  manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 13 }, Slot { 2 });

  const auto shadow_caster_bounds
    = std::array { glm::vec4 { 1.0F, 2.0F, 3.0F, 4.0F } };
  const auto visible_receiver_bounds
    = std::array { glm::vec4 { 5.0F, 6.0F, 7.0F, 8.0F } };
  const auto rendered_items = std::array { RenderItemData {
    .submesh_index = 3U,
    .node_handle = directional_node.GetHandle(),
    .transform_handle = TransformHandle {
      TransformHandle::Index { 17U },
      oxygen::bindless::Generation { 1U },
    },
    .cast_shadows = true,
    .main_view_visible = true,
    .static_shadow_caster = true,
  } };
  const auto publication = manager->PublishForView(oxygen::ViewId { 7 },
    view_constants, lights, observer_ptr<Scene> { GetScene().get() }, 1920.0F,
    rendered_items, shadow_caster_bounds, visible_receiver_bounds,
    std::chrono::milliseconds { 23 }, 0xBEEFULL);

  EXPECT_NE(publication.shadow_instance_metadata_srv,
    oxygen::kInvalidShaderVisibleIndex);
  EXPECT_EQ(publication.directional_shadow_metadata_srv,
    oxygen::kInvalidShaderVisibleIndex);
  EXPECT_EQ(publication.directional_shadow_texture_srv,
    oxygen::kInvalidShaderVisibleIndex);
  EXPECT_EQ(manager->TryGetRasterRenderPlan(oxygen::ViewId { 7 }), nullptr);
  const auto* shadow_instance
    = manager->TryGetShadowInstanceMetadata(oxygen::ViewId { 7 });
  ASSERT_NE(shadow_instance, nullptr);
  EXPECT_EQ(shadow_instance->light_index, 0U);
  EXPECT_EQ(shadow_instance->implementation_kind,
    static_cast<std::uint32_t>(
      oxygen::engine::ShadowImplementationKind::kVirtual));
  EXPECT_EQ(shadow_instance->domain,
    static_cast<std::uint32_t>(oxygen::engine::ShadowDomain::kDirectional));
  EXPECT_NE(shadow_instance->flags
      & static_cast<std::uint32_t>(oxygen::engine::ShadowProductFlags::kValid),
    0U);

  const auto vsm_renderer = manager->GetVirtualShadowRenderer();
  ASSERT_NE(vsm_renderer.get(), nullptr);

  const auto* prepared_view
    = vsm_renderer->TryGetPreparedViewState(oxygen::ViewId { 7 });
  ASSERT_NE(prepared_view, nullptr);
  EXPECT_EQ(prepared_view->frame_sequence, SequenceNumber { 13 });
  EXPECT_EQ(prepared_view->frame_slot, Slot { 2 });
  EXPECT_EQ(prepared_view->active_scene.get(), GetScene().get());
  EXPECT_EQ(prepared_view->view_constants_snapshot.frame_seq_num,
    SequenceNumber { 13 });
  EXPECT_EQ(prepared_view->view_constants_snapshot.frame_slot, 2U);
  EXPECT_TRUE(
    prepared_view->view_constants_snapshot.view_frame_bindings_bslot.IsValid());
  EXPECT_FLOAT_EQ(prepared_view->camera_viewport_width, 1920.0F);
  ASSERT_EQ(prepared_view->directional_shadow_candidates.size(), 1U);
  ASSERT_EQ(prepared_view->positional_lights.size(), 1U);
  ASSERT_EQ(prepared_view->positional_shadow_candidates.size(), 1U);
  EXPECT_EQ(prepared_view->directional_shadow_candidates[0].node_handle,
    directional_node.GetHandle());
  EXPECT_EQ(prepared_view->positional_shadow_candidates[0].node_handle,
    point_node.GetHandle());
  EXPECT_EQ(prepared_view->positional_shadow_candidates[0].light_index, 0U);
  ASSERT_EQ(prepared_view->scene_primitive_history.size(), 1U);
  EXPECT_EQ(prepared_view->scene_primitive_history[0].node_handle,
    directional_node.GetHandle());
  EXPECT_EQ(
    prepared_view->scene_primitive_history[0].primitive.transform_index, 17U);
  EXPECT_EQ(
    prepared_view->scene_primitive_history[0].primitive.transform_generation,
    1U);
  EXPECT_EQ(
    prepared_view->scene_primitive_history[0].primitive.submesh_index, 3U);
  EXPECT_TRUE(prepared_view->scene_primitive_history[0].static_shadow_caster);
  ASSERT_EQ(
    prepared_view->shadow_caster_bounds.size(), shadow_caster_bounds.size());
  EXPECT_EQ(prepared_view->shadow_caster_bounds[0], shadow_caster_bounds[0]);
  ASSERT_EQ(prepared_view->visible_receiver_bounds.size(),
    visible_receiver_bounds.size());
  EXPECT_EQ(
    prepared_view->visible_receiver_bounds[0], visible_receiver_bounds[0]);
  EXPECT_EQ(prepared_view->gpu_budget, std::chrono::milliseconds { 23 });
  EXPECT_EQ(prepared_view->shadow_caster_content_hash, 0xBEEFULL);
  EXPECT_TRUE(prepared_view->has_virtual_shadow_work);
}

NOLINT_TEST_F(
  ShadowManagerPolicyTest, VsmPublishRespectsDirectionalResolutionHintBudget)
{
  const auto pages_per_axis_for_hint
    = [&](const ShadowResolutionHint hint) -> std::uint32_t {
    auto manager = MakeShadowManager(
      DirectionalShadowImplementationPolicy::kVirtualShadowMap);
    auto lights = MakeLightManager();
    auto view_constants = MakeViewConstants();

    auto directional_node
      = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
    auto directional_impl = directional_node.GetImpl();
    EXPECT_TRUE(directional_impl.has_value());
    if (!directional_impl.has_value()) {
      return 0U;
    }

    directional_impl->get().AddComponent<DirectionalLight>();
    auto& directional_light
      = directional_impl->get().GetComponent<DirectionalLight>();
    directional_light.Common().casts_shadows = true;
    directional_light.Common().shadow.resolution_hint = hint;
    directional_light.CascadedShadows().cascade_count = 4U;
    directional_light.CascadedShadows().split_mode
      = DirectionalCsmSplitMode::kGenerated;
    UpdateTransforms(directional_node);

    lights.OnFrameStart(
      RendererTagFactory::Get(), SequenceNumber { 31 }, Slot { 0 });
    lights.CollectFromNode(
      directional_node.GetHandle(), directional_impl->get());
    manager->OnFrameStart(
      RendererTagFactory::Get(), SequenceNumber { 31 }, Slot { 0 });

    const auto publication
      = manager->PublishForView(oxygen::ViewId { 19 }, view_constants, lights,
        observer_ptr<Scene> { GetScene().get() }, 1920.0F);
    EXPECT_NE(publication.shadow_instance_metadata_srv,
      oxygen::kInvalidShaderVisibleIndex);

    const auto vsm_renderer = manager->GetVirtualShadowRenderer();
    EXPECT_NE(vsm_renderer.get(), nullptr);
    if (vsm_renderer == nullptr) {
      return 0U;
    }

    const auto products
      = vsm_renderer->BuildPreparedViewProducts(oxygen::ViewId { 19 });
    EXPECT_TRUE(products.has_value());
    if (!products.has_value()) {
      return 0U;
    }

    EXPECT_EQ(products->virtual_frame.directional_layouts.size(), 1U);
    if (products->virtual_frame.directional_layouts.empty()) {
      return 0U;
    }

    return products->virtual_frame.directional_layouts.front().pages_per_axis;
  };

  EXPECT_EQ(pages_per_axis_for_hint(ShadowResolutionHint::kLow), 8U);
  EXPECT_EQ(pages_per_axis_for_hint(ShadowResolutionHint::kMedium), 16U);
  EXPECT_EQ(pages_per_axis_for_hint(ShadowResolutionHint::kHigh), 24U);
  EXPECT_EQ(pages_per_axis_for_hint(ShadowResolutionHint::kUltra), 24U);
}

NOLINT_TEST_F(
  ShadowManagerPolicyTest, VsmPublishRebindsSceneObservationToLatestActiveScene)
{
  auto manager = MakeShadowManager(
    DirectionalShadowImplementationPolicy::kVirtualShadowMap);
  auto lights = MakeLightManager();
  auto view_constants = MakeViewConstants();
  auto scene_a = std::make_shared<Scene>("shadow-manager-scene-a", 32);
  auto scene_b = std::make_shared<Scene>("shadow-manager-scene-b", 32);
  auto node_a = scene_a->CreateNode("node-a");
  auto node_b = scene_b->CreateNode("node-b");
  ASSERT_TRUE(node_a.IsValid());
  ASSERT_TRUE(node_b.IsValid());

  lights.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 9 }, Slot { 0 });
  manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 9 }, Slot { 0 });

  static_cast<void>(manager->PublishForView(oxygen::ViewId { 3 },
    view_constants, lights, observer_ptr<Scene> { scene_a.get() }, 1280.0F));
  const auto vsm_renderer = manager->GetVirtualShadowRenderer();
  ASSERT_NE(vsm_renderer.get(), nullptr);

  auto& invalidation_coordinator
    = vsm_renderer->GetSceneInvalidationCoordinator();
  invalidation_coordinator.PublishScenePrimitiveHistory(std::array {
    oxygen::renderer::vsm::VsmScenePrimitiveHistoryRecord {
      .node_handle = node_a.GetHandle(),
      .primitive = oxygen::renderer::vsm::VsmPrimitiveIdentity {
        .transform_index = 11U,
        .transform_generation = 1U,
        .submesh_index = 0U,
      },
      .world_bounding_sphere = glm::vec4 { 0.0F, 0.0F, 0.0F, 2.0F },
      .static_shadow_caster = false,
    },
  });

  static_cast<void>(manager->PublishForView(oxygen::ViewId { 3 },
    view_constants, lights, observer_ptr<Scene> { scene_b.get() }, 1280.0F));
  invalidation_coordinator.PublishScenePrimitiveHistory(std::array {
    oxygen::renderer::vsm::VsmScenePrimitiveHistoryRecord {
      .node_handle = node_b.GetHandle(),
      .primitive = oxygen::renderer::vsm::VsmPrimitiveIdentity {
        .transform_index = 12U,
        .transform_generation = 1U,
        .submesh_index = 0U,
      },
      .world_bounding_sphere = glm::vec4 { 1.0F, 1.0F, 1.0F, 2.0F },
      .static_shadow_caster = false,
    },
  });

  ASSERT_TRUE(node_a.GetTransform().SetLocalPosition({ 1.0F, 0.0F, 0.0F }));
  ASSERT_TRUE(node_b.GetTransform().SetLocalPosition({ 0.0F, 1.0F, 0.0F }));

  scene_a->SyncObservers();
  scene_b->SyncObservers();

  const auto frame_inputs = invalidation_coordinator.DrainFrameInputs();
  ASSERT_EQ(frame_inputs.primitive_invalidations.size(), 1U);
  EXPECT_EQ(
    frame_inputs.primitive_invalidations[0].primitive.transform_index, 12U);
  EXPECT_TRUE(frame_inputs.light_invalidation_requests.empty());

  const auto* prepared_view
    = vsm_renderer->TryGetPreparedViewState(oxygen::ViewId { 3 });
  ASSERT_NE(prepared_view, nullptr);
  EXPECT_EQ(prepared_view->active_scene.get(), scene_b.get());
}

} // namespace
