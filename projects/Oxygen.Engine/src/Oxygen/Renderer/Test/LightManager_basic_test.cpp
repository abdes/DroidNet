//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
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
using oxygen::engine::upload::DefaultUploadPolicy;
using oxygen::engine::upload::InlineTransfersCoordinator;
using oxygen::engine::upload::StagingProvider;
using oxygen::engine::upload::UploadCoordinator;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::SingleQueueStrategy;
using oxygen::renderer::LightManager;
using oxygen::renderer::internal::RendererTagFactory;
using oxygen::renderer::testing::FakeGraphics;
using oxygen::scene::Scene;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;

class LightManagerTest : public testing::Test {
protected:
  auto SetUp() -> void override
  {
    gfx_ = std::make_shared<FakeGraphics>();
    gfx_->CreateCommandQueues(SingleQueueStrategy());

    uploader_ = std::make_unique<UploadCoordinator>(
      observer_ptr { gfx_.get() }, DefaultUploadPolicy());

    staging_provider_
      = uploader_->CreateRingBufferStaging(oxygen::frame::SlotCount { 1 }, 256u);

    inline_transfers_
      = std::make_unique<InlineTransfersCoordinator>(observer_ptr { gfx_.get() });

    manager_ = std::make_unique<LightManager>(observer_ptr { gfx_.get() },
      observer_ptr { staging_provider_.get() },
      observer_ptr { inline_transfers_.get() });

    static constexpr size_t kTestSceneCapacity = 64;
    scene_ = std::make_shared<Scene>("LightManagerTestScene", kTestSceneCapacity);
  }

  [[nodiscard]] auto Manager() const -> LightManager& { return *manager_; }

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

NOLINT_TEST_F(LightManagerTest, CollectFromNode_InvisibleNodeEmitsNoLights)
{
  auto& manager = Manager();
  manager.OnFrameStart(RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto node = CreateNode("invisible", /*visible=*/false, /*casts_shadows=*/true);
  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().AddComponent<oxygen::scene::DirectionalLight>();
  UpdateTransforms(node);

  manager.CollectFromNode(impl->get());

  EXPECT_TRUE(manager.GetDirectionalLights().empty());
  EXPECT_TRUE(manager.GetPositionalLights().empty());
}

NOLINT_TEST_F(LightManagerTest, CollectFromNode_AffectsWorldFalseEmitsNoLights)
{
  auto& manager = Manager();
  manager.OnFrameStart(RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto node = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().AddComponent<oxygen::scene::DirectionalLight>();
  auto& light = impl->get().GetComponent<oxygen::scene::DirectionalLight>();
  light.Common().affects_world = false;
  UpdateTransforms(node);

  manager.CollectFromNode(impl->get());

  EXPECT_TRUE(manager.GetDirectionalLights().empty());
}

NOLINT_TEST_F(LightManagerTest, CollectFromNode_BakedMobilityEmitsNoLights)
{
  auto& manager = Manager();
  manager.OnFrameStart(RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto node = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().AddComponent<oxygen::scene::DirectionalLight>();
  auto& light = impl->get().GetComponent<oxygen::scene::DirectionalLight>();
  light.Common().mobility = oxygen::scene::LightMobility::kBaked;
  UpdateTransforms(node);

  manager.CollectFromNode(impl->get());

  EXPECT_TRUE(manager.GetDirectionalLights().empty());
}

NOLINT_TEST_F(
  LightManagerTest, CollectFromNode_ShadowEligibilityRequiresNodeFlag)
{
  auto& manager = Manager();
  manager.OnFrameStart(RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto node = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/false);
  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().AddComponent<oxygen::scene::DirectionalLight>();
  auto& light = impl->get().GetComponent<oxygen::scene::DirectionalLight>();
  light.Common().casts_shadows = true;
  UpdateTransforms(node);

  manager.CollectFromNode(impl->get());

  const auto lights = manager.GetDirectionalLights();
  ASSERT_EQ(lights.size(), 1U);

  constexpr auto kInvalidShadowIndex = 0xFFFFFFFFu;
  EXPECT_EQ(lights[0].shadow_index, kInvalidShadowIndex);
  const auto flags = lights[0].flags;
  const auto casts_shadows_bit
    = static_cast<std::uint32_t>(DirectionalLightFlags::kCastsShadows);
  EXPECT_EQ(flags & casts_shadows_bit, 0U);
}

NOLINT_TEST_F(LightManagerTest, CollectFromNode_DirectionUsesWorldRotation)
{
  auto& manager = Manager();
  manager.OnFrameStart(RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto node = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto transform = node.GetTransform();
  const auto rotation
    = glm::angleAxis(glm::radians(90.0F), glm::vec3 { 0.0F, 1.0F, 0.0F });
  ASSERT_TRUE(transform.SetLocalRotation(rotation));
  UpdateTransforms(node);

  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().AddComponent<oxygen::scene::DirectionalLight>();

  const glm::vec3 expected_dir
    = glm::normalize(rotation * oxygen::space::move::Forward);

  manager.CollectFromNode(impl->get());

  const auto lights = manager.GetDirectionalLights();
  ASSERT_EQ(lights.size(), 1U);
  EXPECT_NEAR(lights[0].direction_ws.x, expected_dir.x, 1e-5F);
  EXPECT_NEAR(lights[0].direction_ws.y, expected_dir.y, 1e-5F);
  EXPECT_NEAR(lights[0].direction_ws.z, expected_dir.z, 1e-5F);
}

NOLINT_TEST_F(LightManagerTest, EnsureFrameResources_NoLightsKeepsSrvInvalid)
{
  auto& manager = Manager();
  manager.OnFrameStart(RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  manager.EnsureFrameResources();

  EXPECT_EQ(manager.GetDirectionalLightsSrvIndex(), kInvalidShaderVisibleIndex);
  EXPECT_EQ(manager.GetPositionalLightsSrvIndex(), kInvalidShaderVisibleIndex);
  EXPECT_TRUE(manager.GetDirectionalShadowCandidates().empty());
}

NOLINT_TEST_F(LightManagerTest,
  EnsureFrameResources_WithDirectionalAndPositionalLightsAllocatesSrvs)
{
  auto& manager = Manager();
  manager.OnFrameStart(RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto dir_node = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto dir_impl = dir_node.GetImpl();
  ASSERT_TRUE(dir_impl.has_value());
  dir_impl->get().AddComponent<oxygen::scene::DirectionalLight>();
  dir_impl->get()
    .GetComponent<oxygen::scene::DirectionalLight>()
    .Common()
    .casts_shadows = true;
  UpdateTransforms(dir_node);

  auto point_node = CreateNode("point", /*visible=*/true, /*casts_shadows=*/true);
  auto point_impl = point_node.GetImpl();
  ASSERT_TRUE(point_impl.has_value());
  point_impl->get().AddComponent<oxygen::scene::PointLight>();
  UpdateTransforms(point_node);

  manager.CollectFromNode(dir_impl->get());
  manager.CollectFromNode(point_impl->get());
  manager.EnsureFrameResources();

  EXPECT_EQ(manager.GetDirectionalLights().size(), 1U);
  EXPECT_EQ(manager.GetDirectionalShadowCandidates().size(), 1U);
  EXPECT_EQ(manager.GetPositionalLights().size(), 1U);
  EXPECT_NE(manager.GetDirectionalLightsSrvIndex(), kInvalidShaderVisibleIndex);
  EXPECT_NE(manager.GetPositionalLightsSrvIndex(), kInvalidShaderVisibleIndex);
}

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

NOLINT_TEST_F(LightManagerTest,
  CollectFromNode_DirectionalShadowCandidateCanonicalizesLegacyZeroCascadeSplits)
{
  auto& manager = Manager();
  manager.OnFrameStart(RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

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

} // namespace
