//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

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
  [[nodiscard]] auto CreateShadowManager() const
    -> std::unique_ptr<ShadowManager>
  {
    return std::make_unique<ShadowManager>(observer_ptr { gfx_.get() },
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

//! ShadowManager publishes shared shadow products for shadow-casting
//! directionals.
NOLINT_TEST_F(
  LightManagerTest, ShadowManagerPublishForView_DirectionalProductsArePublished)
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

  ASSERT_NE(published.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(
    published.directional_shadow_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_EQ(published.shadow_instances.size(), 1U);
  ASSERT_EQ(published.directional_metadata.size(), 1U);

  const auto& instance = published.shadow_instances[0];
  EXPECT_EQ(instance.light_index, 0U);
  EXPECT_EQ(instance.payload_index, 0U);
  EXPECT_EQ(instance.domain, 0U);
  EXPECT_EQ(instance.implementation_kind, 1U);
  EXPECT_NE(instance.flags & (1U << 0), 0U);
  EXPECT_NE(instance.flags & (1U << 1), 0U);
  EXPECT_NE(instance.flags & (1U << 2), 0U);

  const auto& metadata = published.directional_metadata[0];
  EXPECT_EQ(metadata.shadow_instance_index, 0U);
  EXPECT_EQ(metadata.implementation_kind, 1U);
  EXPECT_FLOAT_EQ(metadata.distribution_exponent, 2.0F);
  EXPECT_EQ(metadata.cascade_count, 4U);
  EXPECT_FLOAT_EQ(metadata.cascade_distances[0], 8.0F);
  EXPECT_FLOAT_EQ(metadata.cascade_distances[3], 160.0F);
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
    oxygen::ViewId { 8 }, view_constants, manager, &synthetic_sun);

  ASSERT_NE(published.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(
    published.directional_shadow_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_EQ(published.shadow_instances.size(), 1U);
  ASSERT_EQ(published.directional_metadata.size(), 1U);
  EXPECT_EQ(published.sun_shadow_index, 0U);

  const auto& instance = published.shadow_instances[0];
  EXPECT_EQ(instance.light_index, 0xFFFFFFFFU);
  EXPECT_NE(instance.flags & (1U << 2), 0U);
}

} // namespace
