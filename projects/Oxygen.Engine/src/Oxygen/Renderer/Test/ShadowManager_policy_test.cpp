//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>
#include <string>

#include <glm/vec4.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Types/Frame.h>
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
using oxygen::observer_ptr;
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
using oxygen::scene::DirectionalLight;
using oxygen::scene::PointLight;
using oxygen::scene::Scene;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;

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
  static_cast<void>(manager->PublishForView(oxygen::ViewId { 7 },
    view_constants, lights, observer_ptr<Scene> { GetScene().get() }, 1920.0F,
    rendered_items, shadow_caster_bounds, visible_receiver_bounds,
    std::chrono::milliseconds { 23 }, 0xBEEFULL));

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
