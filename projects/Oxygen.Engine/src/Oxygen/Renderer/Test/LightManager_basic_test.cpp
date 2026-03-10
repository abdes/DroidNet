//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>
#include <numeric>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>
#include <Oxygen/Renderer/Types/VirtualShadowRequestFeedback.h>
#include <Oxygen/Renderer/Upload/InlineTransfersCoordinator.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::renderer::internal {
auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}
} // namespace oxygen::renderer::internal

namespace {

using oxygen::kInvalidShaderVisibleIndex;
using oxygen::observer_ptr;
using oxygen::engine::DirectionalLightFlags;
using oxygen::engine::ViewConstants;
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
using oxygen::scene::Scene;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;

struct VirtualFeedbackLayout {
  std::uint32_t clip_level_count { 0U };
  std::uint32_t pages_per_axis { 0U };
  std::uint32_t pages_per_level { 0U };
};

auto ResolveVirtualFeedbackLayout(ShadowManager& shadow_manager,
  LightManager& manager, const ViewConstants& view_constants,
  const oxygen::ViewId view_id, const std::span<const glm::vec4> shadow_casters,
  const ShadowManager::SyntheticSunShadowInput& synthetic_sun)
  -> VirtualFeedbackLayout
{
  const auto published = shadow_manager.PublishForView(view_id, view_constants,
    manager, shadow_casters, {}, &synthetic_sun, std::chrono::milliseconds(16));
  EXPECT_NE(published.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  const auto* introspection
    = shadow_manager.TryGetVirtualViewIntrospection(view_id);
  EXPECT_NE(introspection, nullptr);
  if (introspection == nullptr
    || introspection->directional_virtual_metadata.empty()) {
    return {};
  }

  const auto& metadata = introspection->directional_virtual_metadata.front();
  return VirtualFeedbackLayout {
    .clip_level_count = metadata.clip_level_count,
    .pages_per_axis = metadata.pages_per_axis,
    .pages_per_level = metadata.pages_per_axis * metadata.pages_per_axis,
  };
}

auto AdvanceRendererFrame(LightManager& manager, ShadowManager& shadow_manager,
  ViewConstants& view_constants, const SequenceNumber sequence, const Slot slot)
  -> void
{
  manager.OnFrameStart(RendererTagFactory::Get(), sequence, slot);
  shadow_manager.OnFrameStart(RendererTagFactory::Get(), sequence, slot);
  view_constants.SetFrameSequenceNumber(sequence, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(slot, ViewConstants::kRenderer);
}

//=== LightManager Basic Tests
//===---------------------------------------------//

class LightManagerTest : public testing::Test {
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

    manager_ = std::make_unique<LightManager>(observer_ptr { gfx_.get() },
      observer_ptr { staging_provider_.get() },
      observer_ptr { inline_transfers_.get() });

    static constexpr size_t kTestSceneCapacity = 64;
    scene_
      = std::make_shared<Scene>("LightManagerTestScene", kTestSceneCapacity);
  }

  [[nodiscard]] auto Manager() const -> LightManager& { return *manager_; }
  [[nodiscard]] auto CreateShadowManager(
    const oxygen::DirectionalShadowImplementationPolicy directional_policy
    = oxygen::DirectionalShadowImplementationPolicy::kConventionalOnly,
    const oxygen::ShadowQualityTier quality_tier
    = oxygen::ShadowQualityTier::kHigh) const -> std::unique_ptr<ShadowManager>
  {
    return std::make_unique<ShadowManager>(observer_ptr { gfx_.get() },
      observer_ptr { staging_provider_.get() },
      observer_ptr { inline_transfers_.get() }, quality_tier,
      directional_policy);
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

private:
  std::shared_ptr<FakeGraphics> gfx_;
  std::unique_ptr<UploadCoordinator> uploader_;
  std::shared_ptr<StagingProvider> staging_provider_;
  std::unique_ptr<InlineTransfersCoordinator> inline_transfers_;
  std::unique_ptr<LightManager> manager_;
  std::shared_ptr<Scene> scene_;
};

//! Invisible nodes are a hard gate and emit no lights.
NOLINT_TEST_F(LightManagerTest, CollectFromNode_InvisibleNodeEmitsNoLights)
{
  // Arrange
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto node = CreateNode("invisible", /*visible=*/false,
    /*casts_shadows=*/true);
  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().AddComponent<oxygen::scene::DirectionalLight>();
  UpdateTransforms(node);

  // Act
  manager.CollectFromNode(impl->get());

  // Assert
  EXPECT_TRUE(manager.GetDirectionalLights().empty());
  EXPECT_TRUE(manager.GetPositionalLights().empty());
}

//! Lights with `affects_world=false` are not collected.
NOLINT_TEST_F(LightManagerTest, CollectFromNode_AffectsWorldFalseEmitsNoLights)
{
  // Arrange
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto node = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().AddComponent<oxygen::scene::DirectionalLight>();
  auto& light = impl->get().GetComponent<oxygen::scene::DirectionalLight>();
  light.Common().affects_world = false;
  UpdateTransforms(node);

  // Act
  manager.CollectFromNode(impl->get());

  // Assert
  EXPECT_TRUE(manager.GetDirectionalLights().empty());
}

//! Baked mobility lights are excluded from runtime collection.
NOLINT_TEST_F(LightManagerTest, CollectFromNode_BakedMobilityEmitsNoLights)
{
  // Arrange
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto node = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().AddComponent<oxygen::scene::DirectionalLight>();
  auto& light = impl->get().GetComponent<oxygen::scene::DirectionalLight>();
  light.Common().mobility = oxygen::scene::LightMobility::kBaked;
  UpdateTransforms(node);

  // Act
  manager.CollectFromNode(impl->get());

  // Assert
  EXPECT_TRUE(manager.GetDirectionalLights().empty());
}

//! Shadow eligibility requires both the light property and the node flag.
NOLINT_TEST_F(
  LightManagerTest, CollectFromNode_ShadowEligibilityRequiresNodeFlag)
{
  // Arrange
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto node = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/false);
  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().AddComponent<oxygen::scene::DirectionalLight>();
  auto& light = impl->get().GetComponent<oxygen::scene::DirectionalLight>();
  light.Common().casts_shadows = true;
  UpdateTransforms(node);

  // Act
  manager.CollectFromNode(impl->get());

  // Assert
  const auto lights = manager.GetDirectionalLights();
  ASSERT_EQ(lights.size(), 1);

  constexpr auto kInvalidShadowIndex = 0xFFFFFFFFu;
  EXPECT_EQ(lights[0].shadow_index, kInvalidShadowIndex);

  const auto flags = lights[0].flags;
  const auto casts_shadows_bit
    = static_cast<std::uint32_t>(DirectionalLightFlags::kCastsShadows);
  EXPECT_EQ(flags & casts_shadows_bit, 0U);
}

//! Directional light direction is derived from world rotation * Forward.
NOLINT_TEST_F(LightManagerTest, CollectFromNode_DirectionUsesWorldRotation)
{
  // Arrange
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto node = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);

  auto transform = node.GetTransform();
  const auto rotation
    = glm::angleAxis(glm::radians(90.0F), glm::vec3 { 0, 1, 0 });
  ASSERT_TRUE(transform.SetLocalRotation(rotation));

  UpdateTransforms(node);

  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().AddComponent<oxygen::scene::DirectionalLight>();

  const glm::vec3 expected_dir
    = glm::normalize(rotation * oxygen::space::move::Forward);

  // Act
  manager.CollectFromNode(impl->get());

  // Assert
  const auto lights = manager.GetDirectionalLights();
  ASSERT_EQ(lights.size(), 1);

  EXPECT_NEAR(lights[0].direction_ws.x, expected_dir.x, 1e-5F);
  EXPECT_NEAR(lights[0].direction_ws.y, expected_dir.y, 1e-5F);
  EXPECT_NEAR(lights[0].direction_ws.z, expected_dir.z, 1e-5F);
}

//! When no lights are collected, SRV indices remain invalid.
NOLINT_TEST_F(LightManagerTest, EnsureFrameResources_NoLightsKeepsSrvInvalid)
{
  // Arrange
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  // Act
  manager.EnsureFrameResources();

  // Assert
  EXPECT_EQ(manager.GetDirectionalLightsSrvIndex(), kInvalidShaderVisibleIndex);
  EXPECT_EQ(manager.GetPositionalLightsSrvIndex(), kInvalidShaderVisibleIndex);
  EXPECT_TRUE(manager.GetDirectionalShadowCandidates().empty());
}

//! Collecting lights and ensuring frame resources yields valid SRV indices.
NOLINT_TEST_F(LightManagerTest,
  EnsureFrameResources_WithDirectionalAndPositionalLightsAllocatesSrvs)
{
  // Arrange
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto dir_node = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto dir_impl = dir_node.GetImpl();
  ASSERT_TRUE(dir_impl.has_value());
  dir_impl->get().AddComponent<oxygen::scene::DirectionalLight>();
  dir_impl->get()
    .GetComponent<oxygen::scene::DirectionalLight>()
    .Common()
    .casts_shadows
    = true;
  UpdateTransforms(dir_node);

  auto point_node
    = CreateNode("point", /*visible=*/true, /*casts_shadows=*/true);
  auto point_impl = point_node.GetImpl();
  ASSERT_TRUE(point_impl.has_value());
  point_impl->get().AddComponent<oxygen::scene::PointLight>();
  UpdateTransforms(point_node);

  manager.CollectFromNode(dir_impl->get());
  manager.CollectFromNode(point_impl->get());

  // Act
  manager.EnsureFrameResources();

  // Assert
  EXPECT_EQ(manager.GetDirectionalLights().size(), 1);
  EXPECT_EQ(manager.GetDirectionalShadowCandidates().size(), 1);
  EXPECT_EQ(manager.GetPositionalLights().size(), 1);

  EXPECT_NE(manager.GetDirectionalLightsSrvIndex(), kInvalidShaderVisibleIndex);
  EXPECT_NE(manager.GetPositionalLightsSrvIndex(), kInvalidShaderVisibleIndex);
}

//! Canonical directional shadow defaults must be non-zero so both scene lights
//! and synthetic suns start from the same sane split contract.
NOLINT_TEST(LightCommonDefaultsTest,
  CascadedShadowSettings_DefaultsUseCanonicalCascadeDistances)
{
  const oxygen::scene::CascadedShadowSettings defaults {};
  EXPECT_EQ(defaults.cascade_count, oxygen::scene::kMaxShadowCascades);
  EXPECT_FLOAT_EQ(defaults.cascade_distances[0], 8.0F);
  EXPECT_FLOAT_EQ(defaults.cascade_distances[1], 24.0F);
  EXPECT_FLOAT_EQ(defaults.cascade_distances[2], 64.0F);
  EXPECT_FLOAT_EQ(defaults.cascade_distances[3], 160.0F);
  EXPECT_FLOAT_EQ(defaults.distribution_exponent, 1.0F);
}

//! Legacy/cooked zero cascade distances must be canonicalized before the
//! shadow runtime consumes a directional light.
NOLINT_TEST_F(LightManagerTest,
  CollectFromNode_DirectionalShadowCandidateCanonicalizesLegacyZeroCascadeSplits)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto node = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().AddComponent<oxygen::scene::DirectionalLight>();
  auto& light = impl->get().GetComponent<oxygen::scene::DirectionalLight>();
  light.Common().casts_shadows = true;
  light.CascadedShadows().cascade_distances = { 0.0F, 0.0F, 0.0F, 0.0F };
  UpdateTransforms(node);

  manager.CollectFromNode(impl->get());

  ASSERT_EQ(manager.GetDirectionalShadowCandidates().size(), 1U);
  const auto& candidate = manager.GetDirectionalShadowCandidates().front();
  EXPECT_EQ(candidate.cascade_count, oxygen::scene::kMaxShadowCascades);
  EXPECT_FLOAT_EQ(candidate.cascade_distances[0], 8.0F);
  EXPECT_FLOAT_EQ(candidate.cascade_distances[1], 24.0F);
  EXPECT_FLOAT_EQ(candidate.cascade_distances[2], 64.0F);
  EXPECT_FLOAT_EQ(candidate.cascade_distances[3], 160.0F);
  EXPECT_FLOAT_EQ(candidate.distribution_exponent, 1.0F);
}

//! ShadowManager publishes shading-facing shadow data and a backend-neutral
//! raster render plan for shadow-casting directionals.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_DirectionalPublicationAndRasterPlanArePublished)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto node = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().AddComponent<oxygen::scene::DirectionalLight>();
  auto& light = impl->get().GetComponent<oxygen::scene::DirectionalLight>();
  light.Common().casts_shadows = true;
  light.Common().shadow.contact_shadows = true;
  light.SetIsSunLight(true);
  light.CascadedShadows().distribution_exponent = 2.0F;
  light.CascadedShadows().cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F };
  UpdateTransforms(node);

  manager.CollectFromNode(impl->get());
  manager.EnsureFrameResources();

  auto shadow_manager = CreateShadowManager();
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const auto published = shadow_manager->PublishForView(
    oxygen::ViewId { 7 }, view_constants, manager);
  const auto* introspection
    = shadow_manager->TryGetViewIntrospection(oxygen::ViewId { 7 });
  const auto* raster_plan
    = shadow_manager->TryGetRasterRenderPlan(oxygen::ViewId { 7 });

  ASSERT_NE(published.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(
    published.directional_shadow_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(introspection, nullptr);
  ASSERT_NE(raster_plan, nullptr);
  ASSERT_EQ(introspection->shadow_instances.size(), 1U);
  ASSERT_EQ(introspection->directional_metadata.size(), 1U);
  ASSERT_EQ(introspection->raster_jobs.size(), 4U);
  ASSERT_EQ(raster_plan->jobs.size(), 4U);
  ASSERT_NE(raster_plan->depth_texture, nullptr);

  const auto& instance = introspection->shadow_instances[0];
  EXPECT_EQ(instance.light_index, 0U);
  EXPECT_EQ(instance.payload_index, 0U);
  EXPECT_EQ(instance.domain, 0U);
  EXPECT_EQ(instance.implementation_kind, 1U);
  EXPECT_NE(instance.flags & (1U << 0), 0U);
  EXPECT_NE(instance.flags & (1U << 1), 0U);
  EXPECT_NE(instance.flags & (1U << 2), 0U);

  const auto& metadata = introspection->directional_metadata[0];
  EXPECT_EQ(metadata.shadow_instance_index, 0U);
  EXPECT_EQ(metadata.implementation_kind, 1U);
  EXPECT_FLOAT_EQ(metadata.distribution_exponent, 2.0F);
  EXPECT_EQ(metadata.cascade_count, 4U);
  EXPECT_FLOAT_EQ(metadata.cascade_distances[0], 8.0F);
  EXPECT_FLOAT_EQ(metadata.cascade_distances[3], 160.0F);
  EXPECT_EQ(introspection->raster_jobs[0].payload_index, 0U);
  EXPECT_EQ(introspection->raster_jobs[3].target_array_slice, 3U);
  EXPECT_EQ(published.sun_shadow_index, 0U);
}

//! ShadowManager can publish a shadowed synthetic sun without a scene
//! directional light.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_SyntheticSunPublishesSunShadowIndex)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager();
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::vec3(0.0F, 0.0F, -1.0F),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };

  const auto published = shadow_manager->PublishForView(
    oxygen::ViewId { 8 }, view_constants, manager, {}, {}, &synthetic_sun);
  const auto* introspection
    = shadow_manager->TryGetViewIntrospection(oxygen::ViewId { 8 });

  ASSERT_NE(published.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(
    published.directional_shadow_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(introspection, nullptr);
  ASSERT_EQ(introspection->shadow_instances.size(), 1U);
  ASSERT_EQ(introspection->directional_metadata.size(), 1U);
  EXPECT_EQ(published.sun_shadow_index, 0U);

  const auto& instance = introspection->shadow_instances[0];
  EXPECT_EQ(instance.light_index, 0xFFFFFFFFU);
  EXPECT_NE(instance.flags & (1U << 2), 0U);
}

//! The current directional VSM slice must still activate for a synthetic sun
//! even when a scene sun light also exists, because forward shading consumes
//! the resolved sun through `sun_shadow_index` and skips the scene sun light.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanPrefersSyntheticSunOverSceneSunLight)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto node = CreateNode("scene_sun", /*visible=*/true, /*casts_shadows=*/true);
  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().AddComponent<oxygen::scene::DirectionalLight>();
  auto& light = impl->get().GetComponent<oxygen::scene::DirectionalLight>();
  light.Common().casts_shadows = true;
  light.SetIsSunLight(true);
  UpdateTransforms(node);

  manager.CollectFromNode(impl->get());
  manager.EnsureFrameResources();

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto published = shadow_manager->PublishForView(oxygen::ViewId { 81 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* virtual_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 81 });
  const auto* virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 81 });

  ASSERT_NE(published.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(published.virtual_directional_shadow_metadata_srv,
    kInvalidShaderVisibleIndex);
  ASSERT_NE(
    published.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(
    published.virtual_shadow_physical_pool_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(virtual_plan, nullptr);
  ASSERT_NE(virtual_introspection, nullptr);
  EXPECT_EQ(published.sun_shadow_index, 0U);
  EXPECT_FALSE(virtual_plan->jobs.empty());
  ASSERT_FALSE(virtual_introspection->directional_virtual_metadata.empty());
  EXPECT_NE(virtual_introspection->directional_virtual_metadata.front().flags
      & static_cast<std::uint32_t>(
        oxygen::engine::ShadowProductFlags::kSunLight),
    0U);
}

//! Virtual shadow planning is driven by visible receiver demand instead of a
//! centered resident window.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanUsesVisibleReceiverBounds)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kPreferVirtual);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto published = shadow_manager->PublishForView(oxygen::ViewId { 10 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* virtual_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 10 });
  const auto* virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 10 });

  ASSERT_NE(published.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(published.virtual_directional_shadow_metadata_srv,
    kInvalidShaderVisibleIndex);
  ASSERT_NE(
    published.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(
    published.virtual_shadow_physical_pool_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(virtual_plan, nullptr);
  ASSERT_NE(virtual_introspection, nullptr);
  EXPECT_FALSE(virtual_plan->jobs.empty());
  EXPECT_EQ(virtual_introspection->virtual_raster_jobs.size(),
    virtual_plan->jobs.size());
}

//! Explicit per-view request feedback becomes eligible on the next compatible
//! publication and can then map the requested pages.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanUsesSubmittedRequestFeedbackAfterSafeFrameDelay)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };

  const auto layout = ResolveVirtualFeedbackLayout(*shadow_manager, manager,
    view_constants, oxygen::ViewId { 120 }, shadow_casters, synthetic_sun);
  ASSERT_GT(layout.clip_level_count, 1U);
  ASSERT_GT(layout.pages_per_level, 1U);

  const std::uint32_t fine_page0 = layout.pages_per_level - 1U;
  const std::uint32_t coarse_page0
    = layout.pages_per_level + (layout.pages_per_level - 1U);

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 20 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_virtual_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 20 });
  const auto* first_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 20 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_plan, nullptr);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_GT(
    first_virtual_introspection->page_table_entries.size(), coarse_page0);
  EXPECT_EQ(first_virtual_introspection->page_table_entries[fine_page0], 0U);
  EXPECT_EQ(first_virtual_introspection->page_table_entries[coarse_page0], 0U);
  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 20 });

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 20 },
    oxygen::renderer::VirtualShadowRequestFeedback {
      .source_frame_sequence = SequenceNumber { 1 },
      .pages_per_axis = layout.pages_per_axis,
      .clip_level_count = layout.clip_level_count,
      .requested_page_indices = { fine_page0, coarse_page0 },
    });

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto published = shadow_manager->PublishForView(oxygen::ViewId { 20 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* virtual_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 20 });
  const auto* virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 20 });

  ASSERT_NE(published.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(virtual_plan, nullptr);
  ASSERT_NE(virtual_introspection, nullptr);
  EXPECT_FALSE(virtual_plan->jobs.empty());
  ASSERT_GT(virtual_introspection->page_table_entries.size(), coarse_page0);
  EXPECT_NE(virtual_introspection->page_table_entries[fine_page0], 0U);
  EXPECT_NE(virtual_introspection->page_table_entries[coarse_page0], 0U);
}

//! Ultra-tier directional VSM should publish a dense virtual address space
//! while keeping physical page texel size above the minimum useful floor.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanUsesDenseGridForUltraTier)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly,
    oxygen::ShadowQualityTier::kUltra);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };

  const auto published = shadow_manager->PublishForView(oxygen::ViewId { 30 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* virtual_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 30 });

  ASSERT_NE(published.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(virtual_plan, nullptr);
  const auto* virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 30 });
  ASSERT_NE(virtual_introspection, nullptr);
  ASSERT_FALSE(virtual_introspection->directional_virtual_metadata.empty());
  EXPECT_GE(
    virtual_introspection->directional_virtual_metadata.front().pages_per_axis,
    64U);
  EXPECT_GE(virtual_plan->page_size_texels, 128U);
}

//! Ultra-tier directional VSM should use a denser virtual address space than
//! the physical pool so quality can improve without sizing the atlas for full
//! residency.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanUsesDenserAddressSpaceThanPhysicalPool)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly,
    oxygen::ShadowQualityTier::kUltra);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto published = shadow_manager->PublishForView(oxygen::ViewId { 31 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* virtual_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 31 });
  const auto* virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 31 });

  ASSERT_NE(published.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(virtual_plan, nullptr);
  ASSERT_NE(virtual_introspection, nullptr);
  ASSERT_FALSE(virtual_introspection->directional_virtual_metadata.empty());

  const auto& metadata
    = virtual_introspection->directional_virtual_metadata.front();
  const auto virtual_page_count = metadata.clip_level_count
    * metadata.pages_per_axis * metadata.pages_per_axis;
  const auto physical_tile_capacity
    = virtual_plan->atlas_tiles_per_axis * virtual_plan->atlas_tiles_per_axis;

  EXPECT_EQ(metadata.clip_level_count, 12U);
  EXPECT_GE(metadata.pages_per_axis, 64U);
  EXPECT_LT(physical_tile_capacity, virtual_page_count);
}

//! Explicit virtual request feedback remains the active request source while it
//! stays compatible and fresh enough for the current frame window.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualRequestFeedbackPersistsWithinFreshnessWindow)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> second_visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto layout = ResolveVirtualFeedbackLayout(*shadow_manager, manager,
    view_constants, oxygen::ViewId { 121 }, shadow_casters, synthetic_sun);
  ASSERT_GT(layout.pages_per_level, 1U);
  const std::uint32_t requested_page = layout.pages_per_level - 1U;

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 21 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 21 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_GT(
    first_virtual_introspection->page_table_entries.size(), requested_page);
  EXPECT_EQ(
    first_virtual_introspection->page_table_entries[requested_page], 0U);
  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 21 });

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 21 },
    oxygen::renderer::VirtualShadowRequestFeedback {
      .source_frame_sequence = SequenceNumber { 1 },
      .pages_per_axis = layout.pages_per_axis,
      .clip_level_count = layout.clip_level_count,
      .requested_page_indices = { requested_page },
    });

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 21 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 21 });
  const auto* second_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 21 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_plan, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_GT(
    second_virtual_introspection->page_table_entries.size(), requested_page);
  EXPECT_NE(
    second_virtual_introspection->page_table_entries[requested_page], 0U);
  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 21 });

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 3 }, Slot { 0 });

  const auto third = shadow_manager->PublishForView(oxygen::ViewId { 21 },
    view_constants, manager, shadow_casters, second_visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* third_virtual_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 21 });
  const auto* third_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 21 });

  ASSERT_NE(third.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(third_virtual_plan, nullptr);
  ASSERT_NE(third_virtual_introspection, nullptr);
  EXPECT_LE(third_virtual_plan->jobs.size(), second_virtual_plan->jobs.size());
  EXPECT_GT(third_virtual_introspection->mapped_page_count, 0U);
  EXPECT_GE(third_virtual_introspection->mapped_page_count,
    third_virtual_introspection->pending_page_count);
  ASSERT_GT(
    third_virtual_introspection->page_table_entries.size(), requested_page);
  EXPECT_NE(
    third_virtual_introspection->page_table_entries[requested_page], 0U);
}

//! Feedback-driven requests should apply a small page guard band so fine
//! virtual coverage does not disappear exactly at the camera frustum edge.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualRequestFeedbackAppliesGuardBand)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };

  const auto layout = ResolveVirtualFeedbackLayout(*shadow_manager, manager,
    view_constants, oxygen::ViewId { 127 }, shadow_casters, synthetic_sun);
  ASSERT_GT(layout.pages_per_level, 1U);

  const auto bootstrap = shadow_manager->PublishForView(oxygen::ViewId { 27 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(bootstrap.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 27 });

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 27 },
    oxygen::renderer::VirtualShadowRequestFeedback {
      .source_frame_sequence = SequenceNumber { 1 },
      .pages_per_axis = layout.pages_per_axis,
      .clip_level_count = layout.clip_level_count,
      .requested_page_indices = { 0U },
    });

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto publication = shadow_manager->PublishForView(oxygen::ViewId { 27 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* virtual_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 27 });
  const auto* virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 27 });

  ASSERT_NE(
    publication.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(virtual_plan, nullptr);
  ASSERT_NE(virtual_introspection, nullptr);
  ASSERT_FALSE(virtual_plan->jobs.empty());

  std::uint32_t mapped_clip0_pages = 0U;
  for (std::uint32_t i = 0U; i < layout.pages_per_level; ++i) {
    if (virtual_introspection->page_table_entries[i] != 0U) {
      ++mapped_clip0_pages;
    }
  }

  EXPECT_GT(virtual_introspection->mapped_page_count, 1U);
  EXPECT_GT(mapped_clip0_pages, 1U);
}

//! Feedback-driven pages should keep a short mapping grace window so a small
//! frustum-edge miss does not immediately unmap a previously visible page.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualRequestFeedbackKeepsRecentMappedPages)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -8.0F, 5.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.4F, -0.3F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };

  const auto layout = ResolveVirtualFeedbackLayout(*shadow_manager, manager,
    view_constants, oxygen::ViewId { 128 }, shadow_casters, synthetic_sun);
  ASSERT_GT(layout.pages_per_level, 1U);

  const auto bootstrap = shadow_manager->PublishForView(oxygen::ViewId { 28 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(bootstrap.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 28 });

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 28 },
    oxygen::renderer::VirtualShadowRequestFeedback {
      .source_frame_sequence = SequenceNumber { 1 },
      .pages_per_axis = layout.pages_per_axis,
      .clip_level_count = layout.clip_level_count,
      .requested_page_indices = { 0U },
    });

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 28 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 28 });

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 28 },
    oxygen::renderer::VirtualShadowRequestFeedback {
      .source_frame_sequence = SequenceNumber { 2 },
      .pages_per_axis = layout.pages_per_axis,
      .clip_level_count = layout.clip_level_count,
      .requested_page_indices = { 1U },
    });

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 3 }, Slot { 0 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 28 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 28 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_GE(second_virtual_introspection->page_table_entries.size(), 2U);
  EXPECT_NE(second_virtual_introspection->page_table_entries[0], 0U);
  EXPECT_NE(second_virtual_introspection->page_table_entries[1], 0U);
}

//! Grace-window hysteresis must not pollute close-range feedback with a large
//! previous far-view request set. Small current request sets must still map
//! their newly requested fine pages.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualRequestFeedbackSmallSetOverridesOldFarSet)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -8.0F, 5.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.4F, -0.3F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };

  const auto layout = ResolveVirtualFeedbackLayout(*shadow_manager, manager,
    view_constants, oxygen::ViewId { 129 }, shadow_casters, synthetic_sun);
  ASSERT_GT(layout.pages_per_level, 2U);
  const auto target_page
    = std::min(layout.pages_per_level - 1U, layout.pages_per_axis + 1U);

  std::vector<std::uint32_t> far_requested_pages(
    std::min<std::uint32_t>(layout.pages_per_level, 64U));
  std::iota(far_requested_pages.begin(), far_requested_pages.end(), 0U);

  const auto bootstrap = shadow_manager->PublishForView(oxygen::ViewId { 29 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(bootstrap.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 29 });

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 29 },
    oxygen::renderer::VirtualShadowRequestFeedback {
      .source_frame_sequence = SequenceNumber { 1 },
      .pages_per_axis = layout.pages_per_axis,
      .clip_level_count = layout.clip_level_count,
      .requested_page_indices = far_requested_pages,
    });

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 29 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 29 });

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 29 },
    oxygen::renderer::VirtualShadowRequestFeedback {
      .source_frame_sequence = SequenceNumber { 2 },
      .pages_per_axis = layout.pages_per_axis,
      .clip_level_count = layout.clip_level_count,
      .requested_page_indices = { target_page },
    });

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 3 }, Slot { 0 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 29 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 29 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_GT(
    second_virtual_introspection->page_table_entries.size(), target_page);
  EXPECT_NE(second_virtual_introspection->page_table_entries[target_page], 0U);
}

//! Virtual shadow publication keeps resident pages and skips rerasterization
//! when the snapped virtual plan is unchanged.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanReusesResidentPagesForIdenticalInputs)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kPreferVirtual);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 11 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_virtual_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 11 });
  const auto* first_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 11 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_plan, nullptr);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(first_virtual_plan->jobs.empty());
  ASSERT_EQ(first_virtual_introspection->virtual_raster_jobs.size(),
    first_virtual_plan->jobs.size());
  EXPECT_EQ(first_virtual_introspection->resident_page_count,
    first_virtual_plan->jobs.size());
  EXPECT_EQ(first_virtual_introspection->mapped_page_count,
    first_virtual_plan->jobs.size());
  EXPECT_EQ(first_virtual_introspection->pending_page_count,
    first_virtual_plan->jobs.size());
  EXPECT_EQ(first_virtual_introspection->clean_page_count, 0U);

  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 11 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 11 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 11 });
  const auto* second_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 11 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_plan, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  EXPECT_EQ(second_virtual_plan->jobs.size(), 0U);
  EXPECT_EQ(second_virtual_introspection->virtual_raster_jobs.size(), 0U);
  EXPECT_EQ(second_virtual_introspection->resident_page_count,
    first_virtual_introspection->resident_page_count);
  EXPECT_EQ(second_virtual_introspection->mapped_page_count,
    first_virtual_introspection->mapped_page_count);
  EXPECT_EQ(second_virtual_introspection->pending_page_count, 0U);
  EXPECT_EQ(second_virtual_introspection->clean_page_count,
    first_virtual_introspection->resident_page_count);
  EXPECT_EQ(second.virtual_shadow_physical_pool_srv,
    first.virtual_shadow_physical_pool_srv);
}

//! Clean resident virtual pages that are no longer requested should remain
//! cached instead of being dropped immediately, so later movement can reuse a
//! larger resident working set.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanRetainsCleanPagesAcrossReceiverShift)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kPreferVirtual);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> initial_receivers {
    glm::vec4(-6.0F, 0.0F, 0.0F, 0.05F),
  };
  const std::array<glm::vec4, 1> shifted_receivers {
    glm::vec4(6.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 18 },
    view_constants, manager, shadow_casters, initial_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  const auto* first_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 18 });
  ASSERT_NE(first_virtual_introspection, nullptr);
  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 18 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 18 },
    view_constants, manager, shadow_casters, shifted_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 18 });
  const auto* second_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 18 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_plan, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  EXPECT_GT(second_virtual_introspection->mapped_page_count, 0U);
  EXPECT_GE(second_virtual_introspection->resident_page_count,
    second_virtual_introspection->mapped_page_count);
  EXPECT_GE(second_virtual_introspection->resident_page_count,
    first_virtual_introspection->resident_page_count);
  EXPECT_GT(second_virtual_introspection->clean_page_count, 0U);
}

//! When far-view pressure exposes more virtual pages than the active page
//! budget can map, the planner should keep currently mapped requested pages
//! before rotating in newly requested pages.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanPinsMappedRequestedPagesUnderBudgetPressure)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };

  const auto layout = ResolveVirtualFeedbackLayout(*shadow_manager, manager,
    view_constants, oxygen::ViewId { 124 }, shadow_casters, synthetic_sun);
  ASSERT_GT(layout.pages_per_level, 0U);
  const auto total_pages = layout.pages_per_level * layout.clip_level_count;
  const auto request_count = std::min<std::uint32_t>(total_pages, 2048U);
  ASSERT_GT(request_count, 1U);

  const auto bootstrap = shadow_manager->PublishForView(oxygen::ViewId { 24 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(bootstrap.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 24 });

  std::vector<std::uint32_t> initial_requested_pages(request_count);
  std::iota(initial_requested_pages.begin(), initial_requested_pages.end(), 0U);
  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 24 },
    oxygen::renderer::VirtualShadowRequestFeedback {
      .source_frame_sequence = SequenceNumber { 1 },
      .pages_per_axis = layout.pages_per_axis,
      .clip_level_count = layout.clip_level_count,
      .requested_page_indices = initial_requested_pages,
    });

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 24 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_virtual_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 24 });
  const auto* first_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 24 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_plan, nullptr);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(first_virtual_plan->jobs.empty());
  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 24 });

  std::vector<std::uint32_t> shifted_requested_pages(request_count);
  std::iota(shifted_requested_pages.begin(), shifted_requested_pages.end(), 1U);
  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 24 },
    oxygen::renderer::VirtualShadowRequestFeedback {
      .source_frame_sequence = SequenceNumber { 2 },
      .pages_per_axis = layout.pages_per_axis,
      .clip_level_count = layout.clip_level_count,
      .requested_page_indices = shifted_requested_pages,
    });

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 3 }, Slot { 0 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 24 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 24 });
  const auto* second_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 24 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_plan, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  EXPECT_EQ(second_virtual_introspection->mapped_page_count,
    first_virtual_introspection->mapped_page_count);
  EXPECT_GT(second_virtual_introspection->mapped_page_count, 0U);
  EXPECT_LE(second_virtual_plan->jobs.size(), 4U);
}

//! Virtual pages remain pending until the raster pass executes, so same-frame
//! republishes must not discard the initial virtual raster work.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanKeepsPendingRasterJobsUntilExecuted)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 13 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_virtual_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 13 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_plan, nullptr);
  ASSERT_FALSE(first_virtual_plan->jobs.empty());
  const auto* first_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 13 });
  ASSERT_NE(first_virtual_introspection, nullptr);
  EXPECT_EQ(first_virtual_introspection->pending_page_count,
    first_virtual_plan->jobs.size());

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 13 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 13 });
  const auto* second_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 13 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_plan, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  EXPECT_EQ(second_virtual_plan->jobs.size(), first_virtual_plan->jobs.size());
  EXPECT_EQ(second_virtual_introspection->pending_page_count,
    first_virtual_plan->jobs.size());
}

//! Patching the published view-frame slot must update the jobs the virtual
//! raster pass will actually consume, not only the cached source job list.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanPatchesPendingJobsWithViewFrameSlot)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto published = shadow_manager->PublishForView(oxygen::ViewId { 15 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(published.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);

  constexpr auto kExpectedSlot = oxygen::engine::BindlessViewFrameBindingsSlot {
    oxygen::ShaderVisibleIndex { 42U }
  };
  shadow_manager->SetPublishedViewFrameBindingsSlot(
    oxygen::ViewId { 15 }, kExpectedSlot);

  const auto* virtual_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 15 });
  ASSERT_NE(virtual_plan, nullptr);
  ASSERT_FALSE(virtual_plan->jobs.empty());
  for (const auto& job : virtual_plan->jobs) {
    EXPECT_EQ(job.view_constants.view_frame_bindings_bslot, kExpectedSlot);
  }
}

//! Virtual page reuse must invalidate shadow contents when caster inputs
//! change, even if the snapped clip metadata and requested pages stay the same.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanRerasterizesWhenCasterInputsChange)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kPreferVirtual);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> initial_shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> updated_shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.8F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 12 },
    view_constants, manager, initial_shadow_casters, visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* first_virtual_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 12 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_plan, nullptr);
  ASSERT_FALSE(first_virtual_plan->jobs.empty());

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 12 },
    view_constants, manager, updated_shadow_casters, visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* second_virtual_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 12 });
  const auto* second_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 12 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_plan, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  EXPECT_EQ(second_virtual_plan->jobs.size(), first_virtual_plan->jobs.size());
  EXPECT_EQ(second_virtual_introspection->virtual_raster_jobs.size(),
    first_virtual_plan->jobs.size());
  EXPECT_EQ(second_virtual_introspection->pending_page_count,
    first_virtual_plan->jobs.size());
  EXPECT_EQ(second.virtual_shadow_physical_pool_srv,
    first.virtual_shadow_physical_pool_srv);
}

//! Physical-pool growth must not reuse iterators into the cleared per-view
//! cache.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanSurvivesPhysicalPoolRecreation)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 14 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 14 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(33));
  const auto* second_virtual_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 14 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(
    second.virtual_shadow_physical_pool_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_plan, nullptr);
  EXPECT_FALSE(second_virtual_plan->jobs.empty());
}

//! Shadow publication is reused for identical inputs and invalidated when
//! shadow-relevant inputs change within the same frame.
NOLINT_TEST_F(
  LightManagerTest, ShadowManagerPublishForView_InvalidatesWhenViewInputsChange)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto node = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().AddComponent<oxygen::scene::DirectionalLight>();
  auto& light = impl->get().GetComponent<oxygen::scene::DirectionalLight>();
  light.Common().casts_shadows = true;
  UpdateTransforms(node);

  manager.CollectFromNode(impl->get());
  manager.EnsureFrameResources();

  auto shadow_manager = CreateShadowManager();
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const auto first = shadow_manager->PublishForView(
    oxygen::ViewId { 9 }, view_constants, manager);
  const auto second = shadow_manager->PublishForView(
    oxygen::ViewId { 9 }, view_constants, manager);
  EXPECT_EQ(
    first.shadow_instance_metadata_srv, second.shadow_instance_metadata_srv);
  EXPECT_EQ(first.directional_shadow_metadata_srv,
    second.directional_shadow_metadata_srv);

  view_constants.SetCameraPosition(glm::vec3(0.0F, -8.0F, 3.0F));
  const auto third = shadow_manager->PublishForView(
    oxygen::ViewId { 9 }, view_constants, manager);
  EXPECT_NE(
    first.shadow_instance_metadata_srv, third.shadow_instance_metadata_srv);
  EXPECT_NE(first.directional_shadow_metadata_srv,
    third.directional_shadow_metadata_srv);
}

//! Virtual shadow page reuse must be invalidated when caster content changes,
//! even if coarse caster/receiver bounds stay unchanged.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanInvalidatesWhenCasterContentChanges)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 19 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16), 0x1111U);
  const auto* first_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 19 });
  ASSERT_NE(first.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_plan, nullptr);
  ASSERT_FALSE(first_plan->jobs.empty());

  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 19 });
  const auto* executed_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 19 });
  ASSERT_NE(executed_plan, nullptr);
  EXPECT_TRUE(executed_plan->jobs.empty());

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 19 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16), 0x2222U);
  const auto* second_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 19 });
  ASSERT_NE(second.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_plan, nullptr);
  EXPECT_FALSE(second_plan->jobs.empty());
}

//! Same-frame request feedback must not replace the just-rendered page table.
//! Virtual feedback is produced after the virtual raster pass, so it is only
//! valid for the next frame's publication.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualFeedbackIsConsumedNextFrame)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto layout = ResolveVirtualFeedbackLayout(*shadow_manager, manager,
    view_constants, oxygen::ViewId { 125 }, shadow_casters, synthetic_sun);
  ASSERT_GT(layout.pages_per_level, 1U);
  const std::uint32_t requested_page = layout.pages_per_level - 1U;

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 25 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 25 });
  ASSERT_NE(first.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_introspection, nullptr);
  ASSERT_GT(first_introspection->page_table_entries.size(), requested_page);
  const auto first_requested_page
    = first_introspection->page_table_entries[requested_page];
  EXPECT_EQ(first_requested_page, 0U);
  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 25 });

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 25 },
    oxygen::renderer::VirtualShadowRequestFeedback {
      .source_frame_sequence = SequenceNumber { 1 },
      .pages_per_axis = layout.pages_per_axis,
      .clip_level_count = layout.clip_level_count,
      .requested_page_indices = { requested_page },
    });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 25 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 25 });
  ASSERT_NE(second.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_introspection, nullptr);
  ASSERT_GT(second_introspection->page_table_entries.size(), requested_page);
  EXPECT_EQ(second_introspection->page_table_entries[requested_page],
    first_requested_page);

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto third = shadow_manager->PublishForView(oxygen::ViewId { 25 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* third_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 25 });
  ASSERT_NE(third.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(third_introspection, nullptr);
  ASSERT_GT(third_introspection->page_table_entries.size(), requested_page);
  EXPECT_NE(third_introspection->page_table_entries[requested_page], 0U);
  EXPECT_NE(third_introspection->page_table_entries[requested_page],
    first_requested_page);
}

} // namespace
