//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <numeric>
#include <optional>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/matrix.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Renderer/Internal/ShadowBackendCommon.h>
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
using oxygen::renderer::VirtualShadowAtlasTileDebugState;
using oxygen::renderer::internal::RendererTagFactory;
using oxygen::renderer::testing::FakeGraphics;
using oxygen::scene::Scene;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;

auto CountAtlasTileDebugState(
  const std::span<const std::uint32_t> tile_states,
  const VirtualShadowAtlasTileDebugState expected_state) -> std::uint32_t
{
  return static_cast<std::uint32_t>(std::count(tile_states.begin(),
    tile_states.end(), static_cast<std::uint32_t>(expected_state)));
}

struct VirtualFeedbackLayout {
  std::uint32_t clip_level_count { 0U };
  std::uint32_t pages_per_axis { 0U };
  std::uint32_t pages_per_level { 0U };
  std::uint64_t directional_address_space_hash { 0U };
  std::array<std::int32_t, oxygen::engine::kMaxVirtualDirectionalClipLevels>
    clip_grid_origin_x {};
  std::array<std::int32_t, oxygen::engine::kMaxVirtualDirectionalClipLevels>
    clip_grid_origin_y {};
};

auto BuildVirtualFeedbackLayout(
  const oxygen::engine::DirectionalVirtualShadowMetadata& metadata)
  -> VirtualFeedbackLayout
{
  VirtualFeedbackLayout layout {
    .clip_level_count = metadata.clip_level_count,
    .pages_per_axis = metadata.pages_per_axis,
    .pages_per_level = metadata.pages_per_axis * metadata.pages_per_axis,
    .directional_address_space_hash
    = oxygen::renderer::internal::shadow_detail::
        HashDirectionalVirtualFeedbackAddressSpace(metadata),
  };
  for (std::uint32_t clip_index = 0U;
    clip_index < std::min(metadata.clip_level_count,
      oxygen::engine::kMaxVirtualDirectionalClipLevels);
    ++clip_index) {
    layout.clip_grid_origin_x[clip_index]
      = oxygen::renderer::internal::shadow_detail::
          ResolveDirectionalVirtualClipGridOriginX(metadata, clip_index);
    layout.clip_grid_origin_y[clip_index]
      = oxygen::renderer::internal::shadow_detail::
          ResolveDirectionalVirtualClipGridOriginY(metadata, clip_index);
  }
  return layout;
}

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

  return BuildVirtualFeedbackLayout(
    introspection->directional_virtual_metadata.front());
}

auto RequestedResidentKeys(const VirtualFeedbackLayout& layout,
  const std::span<const std::uint32_t> requested_page_indices)
  -> std::vector<std::uint64_t>
{
  std::vector<std::uint64_t> resident_keys {};
  resident_keys.reserve(requested_page_indices.size());
  for (const auto global_page_index : requested_page_indices) {
    if (layout.pages_per_level == 0U || layout.pages_per_axis == 0U) {
      continue;
    }

    const auto clip_index = global_page_index / layout.pages_per_level;
    if (clip_index >= layout.clip_level_count) {
      continue;
    }

    const auto local_page_index = global_page_index % layout.pages_per_level;
    const auto page_y = local_page_index / layout.pages_per_axis;
    const auto page_x = local_page_index % layout.pages_per_axis;
    resident_keys.push_back(
      oxygen::renderer::internal::shadow_detail::PackVirtualResidentPageKey(
        clip_index, layout.clip_grid_origin_x[clip_index]
          + static_cast<std::int32_t>(page_x),
        layout.clip_grid_origin_y[clip_index]
          + static_cast<std::int32_t>(page_y)));
  }
  return resident_keys;
}

auto MakeVirtualRequestFeedback(const VirtualFeedbackLayout& layout,
  const SequenceNumber source_frame_sequence,
  const std::span<const std::uint32_t> requested_page_indices)
  -> oxygen::renderer::VirtualShadowRequestFeedback
{
  return oxygen::renderer::VirtualShadowRequestFeedback {
    .source_frame_sequence = source_frame_sequence,
    .pages_per_axis = layout.pages_per_axis,
    .clip_level_count = layout.clip_level_count,
    .directional_address_space_hash = layout.directional_address_space_hash,
    .requested_resident_keys
    = RequestedResidentKeys(layout, requested_page_indices),
  };
}

auto MakeVirtualRequestFeedback(const VirtualFeedbackLayout& layout,
  const SequenceNumber source_frame_sequence,
  const std::initializer_list<std::uint32_t> requested_page_indices)
  -> oxygen::renderer::VirtualShadowRequestFeedback
{
  return MakeVirtualRequestFeedback(layout, source_frame_sequence,
    std::span<const std::uint32_t> { requested_page_indices.begin(),
      requested_page_indices.size() });
}

auto LocalPageIndexForResidentKey(const VirtualFeedbackLayout& layout,
  const std::uint64_t resident_key) -> std::optional<std::uint32_t>
{
  if (layout.pages_per_axis == 0U || layout.pages_per_level == 0U) {
    return std::nullopt;
  }

  const auto clip_index
    = oxygen::renderer::internal::shadow_detail::
        VirtualResidentPageKeyClipLevel(resident_key);
  if (clip_index >= layout.clip_level_count) {
    return std::nullopt;
  }

  const auto local_page_x
    = oxygen::renderer::internal::shadow_detail::
        VirtualResidentPageKeyGridX(resident_key)
    - layout.clip_grid_origin_x[clip_index];
  const auto local_page_y
    = oxygen::renderer::internal::shadow_detail::
        VirtualResidentPageKeyGridY(resident_key)
    - layout.clip_grid_origin_y[clip_index];
  if (local_page_x < 0 || local_page_y < 0
    || local_page_x >= static_cast<std::int32_t>(layout.pages_per_axis)
    || local_page_y >= static_cast<std::int32_t>(layout.pages_per_axis)) {
    return std::nullopt;
  }

  return clip_index * layout.pages_per_level
    + static_cast<std::uint32_t>(local_page_y) * layout.pages_per_axis
    + static_cast<std::uint32_t>(local_page_x);
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
    MakeVirtualRequestFeedback(
      layout, SequenceNumber { 1 }, { fine_page0, coarse_page0 }));

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
    MakeVirtualRequestFeedback(layout, SequenceNumber { 1 }, { requested_page }));

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
    MakeVirtualRequestFeedback(layout, SequenceNumber { 1 }, { 0U }));

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
    MakeVirtualRequestFeedback(layout, SequenceNumber { 1 }, { 0U }));

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 28 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 28 });

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 28 },
    MakeVirtualRequestFeedback(layout, SequenceNumber { 2 }, { 1U }));

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
    MakeVirtualRequestFeedback(layout, SequenceNumber { 1 },
      std::span<const std::uint32_t>(
        far_requested_pages.data(), far_requested_pages.size())));

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 29 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 29 });

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 29 },
    MakeVirtualRequestFeedback(layout, SequenceNumber { 2 }, { target_page }));

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

//! Sparse feedback in one fine clip must stay sparse. Two far-apart requested
//! pages must not inflate into one large mapped rectangle that fills the gap
//! between them.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualRequestFeedbackKeepsSparsePagesSparse)
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
    view_constants, oxygen::ViewId { 130 }, shadow_casters, synthetic_sun);
  ASSERT_GT(layout.pages_per_axis, 8U);
  const std::uint32_t sparse_page_a = 0U;
  const std::uint32_t sparse_page_b = 7U * layout.pages_per_axis + 7U;
  const std::uint32_t gap_page = 4U * layout.pages_per_axis + 4U;

  const auto bootstrap = shadow_manager->PublishForView(oxygen::ViewId { 30 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(bootstrap.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 30 });

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 30 },
    MakeVirtualRequestFeedback(
      layout, SequenceNumber { 1 }, { sparse_page_a, sparse_page_b }));

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto publication = shadow_manager->PublishForView(oxygen::ViewId { 30 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 30 });

  ASSERT_NE(
    publication.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(virtual_introspection, nullptr);
  ASSERT_GT(virtual_introspection->page_table_entries.size(), sparse_page_b);
  ASSERT_GT(virtual_introspection->page_table_entries.size(), gap_page);

  std::uint32_t mapped_clip0_pages = 0U;
  for (std::uint32_t i = 0U; i < layout.pages_per_level; ++i) {
    if (virtual_introspection->page_table_entries[i] != 0U) {
      ++mapped_clip0_pages;
    }
  }

  EXPECT_NE(virtual_introspection->page_table_entries[sparse_page_a], 0U);
  EXPECT_NE(virtual_introspection->page_table_entries[sparse_page_b], 0U);
  EXPECT_EQ(virtual_introspection->page_table_entries[gap_page], 0U);
  EXPECT_LT(mapped_clip0_pages, 50U);
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
  ASSERT_FALSE(first_virtual_introspection->atlas_tile_debug_states.empty());
  EXPECT_EQ(first_virtual_introspection->resident_page_count,
    first_virtual_plan->jobs.size());
  EXPECT_EQ(first_virtual_introspection->mapped_page_count,
    first_virtual_plan->jobs.size());
  EXPECT_EQ(first_virtual_introspection->pending_page_count,
    first_virtual_plan->jobs.size());
  EXPECT_EQ(first_virtual_introspection->clean_page_count, 0U);
  EXPECT_EQ(CountAtlasTileDebugState(
              first_virtual_introspection->atlas_tile_debug_states,
              VirtualShadowAtlasTileDebugState::kRewritten),
    first_virtual_plan->jobs.size());
  EXPECT_EQ(CountAtlasTileDebugState(
              first_virtual_introspection->atlas_tile_debug_states,
              VirtualShadowAtlasTileDebugState::kReused),
    0U);
  EXPECT_EQ(CountAtlasTileDebugState(
              first_virtual_introspection->atlas_tile_debug_states,
              VirtualShadowAtlasTileDebugState::kCached),
    0U);

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
  ASSERT_FALSE(second_virtual_introspection->atlas_tile_debug_states.empty());
  EXPECT_EQ(CountAtlasTileDebugState(
              second_virtual_introspection->atlas_tile_debug_states,
              VirtualShadowAtlasTileDebugState::kRewritten),
    0U);
  EXPECT_EQ(CountAtlasTileDebugState(
              second_virtual_introspection->atlas_tile_debug_states,
              VirtualShadowAtlasTileDebugState::kReused),
    second_virtual_introspection->resident_page_count);
  EXPECT_EQ(CountAtlasTileDebugState(
              second_virtual_introspection->atlas_tile_debug_states,
              VirtualShadowAtlasTileDebugState::kCached),
    0U);
  EXPECT_EQ(CountAtlasTileDebugState(
              second_virtual_introspection->atlas_tile_debug_states,
              VirtualShadowAtlasTileDebugState::kCleared),
    static_cast<std::uint32_t>(
      second_virtual_introspection->atlas_tile_debug_states.size())
      - second_virtual_introspection->resident_page_count);
}

//! Resident pages carried across frames but not requested by the current frame
//! must not be reported as reused in the atlas inspector.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualAtlasDebugSeparatesCachedFromReused)
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
  const std::array<glm::vec4, 2> wide_receivers {
    glm::vec4(-1.5F, 0.0F, 0.0F, 0.08F),
    glm::vec4(1.5F, 0.0F, 0.0F, 0.08F),
  };

  (void)shadow_manager->PublishForView(oxygen::ViewId { 17 }, view_constants,
    manager, shadow_casters, wide_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 17 });

  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 2 }, Slot { 1 });
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 2 }, Slot { 1 });
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 2 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 1 }, ViewConstants::kRenderer);

  const std::array<glm::vec4, 1> narrow_receivers {
    glm::vec4(-1.5F, 0.0F, 0.0F, 0.08F),
  };

  (void)shadow_manager->PublishForView(oxygen::ViewId { 17 }, view_constants,
    manager, shadow_casters, narrow_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 17 });

  ASSERT_NE(second_virtual_introspection, nullptr);
  EXPECT_GT(second_virtual_introspection->resident_page_count, 0U);
  EXPECT_GT(CountAtlasTileDebugState(
              second_virtual_introspection->atlas_tile_debug_states,
              VirtualShadowAtlasTileDebugState::kCached),
    0U);
  EXPECT_LT(CountAtlasTileDebugState(
              second_virtual_introspection->atlas_tile_debug_states,
              VirtualShadowAtlasTileDebugState::kReused),
    second_virtual_introspection->resident_page_count);
}

//! Reordering the shadow-caster bounds set must not invalidate the directional
//! VSM cache when the actual casters are unchanged.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanReusesResidentPagesForReorderedCasterBounds)
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
  const std::array<glm::vec4, 2> shadow_casters_a {
    glm::vec4(-4.0F, 0.0F, 0.5F, 0.5F),
    glm::vec4(4.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 2> shadow_casters_b {
    shadow_casters_a[1],
    shadow_casters_a[0],
  };
  const std::array<glm::vec4, 2> visible_receivers {
    glm::vec4(-4.0F, 0.0F, 0.0F, 0.05F),
    glm::vec4(4.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 110 },
    view_constants, manager, shadow_casters_a, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 110 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_GT(first_virtual_introspection->resident_page_count, 0U);

  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 110 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 110 },
    view_constants, manager, shadow_casters_b, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 110 });
  const auto* second_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 110 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_plan, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  EXPECT_EQ(second_virtual_plan->jobs.size(), 0U);
  EXPECT_EQ(second_virtual_introspection->pending_page_count, 0U);
  EXPECT_EQ(second_virtual_introspection->clean_page_count,
    first_virtual_introspection->resident_page_count);
  EXPECT_EQ(second_virtual_introspection->resident_page_count,
    first_virtual_introspection->resident_page_count);
  EXPECT_EQ(second_virtual_introspection->mapped_page_count,
    first_virtual_introspection->mapped_page_count);
  EXPECT_TRUE(std::ranges::equal(second_virtual_introspection->page_table_entries,
    first_virtual_introspection->page_table_entries));
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

//! Page-aligned directional clipmap motion should reuse overlapping resident
//! pages instead of rerasterizing the whole requested working set, even if the
//! backend chooses a slightly different light-space placement while preserving
//! the overlapping page contents.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanReusesResidentPagesAcrossClipmapShift)
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

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 30 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_virtual_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 30 });
  const auto* first_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 30 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_plan, nullptr);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(first_virtual_plan->jobs.empty());
  ASSERT_FALSE(first_virtual_introspection->directional_virtual_metadata.empty());

  const auto& first_metadata
    = first_virtual_introspection->directional_virtual_metadata.front();
  const auto inverse_light_view = glm::inverse(first_metadata.light_view);
  const float fine_page_world
    = first_metadata.clip_metadata[0].origin_page_scale.z;
  const glm::vec3 world_origin
    = glm::vec3(inverse_light_view * glm::vec4(0.0F, 0.0F, 0.0F, 1.0F));
  const glm::vec3 world_shift_x = glm::vec3(
    inverse_light_view * glm::vec4(fine_page_world, 0.0F, 0.0F, 1.0F))
    - world_origin;

  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 30 });

  view_constants.SetCameraPosition(
    view_constants.GetCameraPosition() + world_shift_x);
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 30 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 30 });
  const auto* second_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 30 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_plan, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_FALSE(second_virtual_introspection->directional_virtual_metadata.empty());

  const auto& second_metadata
    = second_virtual_introspection->directional_virtual_metadata.front();
  EXPECT_NE(first_metadata.clip_metadata[0].origin_page_scale.x,
    second_metadata.clip_metadata[0].origin_page_scale.x);
  EXPECT_LT(second_virtual_introspection->virtual_raster_jobs.size(),
    second_virtual_introspection->mapped_page_count);
  EXPECT_LT(second_virtual_plan->jobs.size(),
    first_virtual_introspection->resident_page_count);
  EXPECT_GT(second_virtual_introspection->clean_page_count, 0U);
}

//! Feedback pages that move outside the current fine-clip window after a
//! clipmap shift must not be remapped into unrelated local pages.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualFeedbackDropsPagesOutsideCurrentClipAfterClipmapShift)
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
    view_constants, oxygen::ViewId { 31 }, shadow_casters, synthetic_sun);
  const auto* bootstrap_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 31 });
  ASSERT_NE(bootstrap_introspection, nullptr);
  ASSERT_FALSE(bootstrap_introspection->directional_virtual_metadata.empty());

  const auto& bootstrap_metadata
    = bootstrap_introspection->directional_virtual_metadata.front();
  const auto inverse_light_view = glm::inverse(bootstrap_metadata.light_view);
  const float fine_page_world
    = bootstrap_metadata.clip_metadata[0].origin_page_scale.z;
  const glm::vec3 world_origin
    = glm::vec3(inverse_light_view * glm::vec4(0.0F, 0.0F, 0.0F, 1.0F));
  const glm::vec3 world_shift_x = glm::vec3(
    inverse_light_view * glm::vec4(fine_page_world, 0.0F, 0.0F, 1.0F))
    - world_origin;

  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 31 });
  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 31 },
    MakeVirtualRequestFeedback(layout, SequenceNumber { 1 }, { 0U }));

  view_constants.SetCameraPosition(
    view_constants.GetCameraPosition() + world_shift_x);
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 31 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 31 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_GT(second_virtual_introspection->mapped_page_count, 0U);
  ASSERT_GT(second_virtual_introspection->page_table_entries.size(), 0U);
  EXPECT_EQ(second_virtual_introspection->page_table_entries[0], 0U);
}

//! Incompatible request feedback must be rejected and the backend must fall
//! back to current-frame receiver bootstrap instead of silently skipping both
//! feedback refinement and bootstrap seeding.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualIncompatibleFeedbackRebootsReceiverBootstrap)
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
    glm::vec4(0.0F, 0.0F, 0.0F, 0.08F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 132 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 132 });
  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(first_virtual_introspection->directional_virtual_metadata.empty());
  ASSERT_GT(first_virtual_introspection->receiver_bootstrap_page_count, 0U);

  const auto first_layout = BuildVirtualFeedbackLayout(
    first_virtual_introspection->directional_virtual_metadata.front());
  ASSERT_GT(first_layout.pages_per_axis, 0U);
  std::optional<std::uint32_t> requested_page {};
  for (std::uint32_t page_index = 0U;
    page_index < first_layout.pages_per_level; ++page_index) {
    if (page_index >= first_virtual_introspection->page_table_entries.size()) {
      break;
    }
    if (first_virtual_introspection->page_table_entries[page_index] != 0U) {
      requested_page = page_index;
      break;
    }
  }
  ASSERT_TRUE(requested_page.has_value());

  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 132 });
  auto incompatible_feedback = MakeVirtualRequestFeedback(
    first_layout, SequenceNumber { 1 }, { *requested_page });
  ++incompatible_feedback.directional_address_space_hash;
  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 132 },
    incompatible_feedback);

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 132 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 132 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_FALSE(second_virtual_introspection->directional_virtual_metadata.empty());
  EXPECT_FALSE(second_virtual_introspection->used_request_feedback);
  EXPECT_EQ(second_virtual_introspection->feedback_requested_page_count, 0U);
  EXPECT_EQ(second_virtual_introspection->feedback_refinement_page_count, 0U);
  EXPECT_GT(second_virtual_introspection->receiver_bootstrap_page_count, 0U);
  EXPECT_EQ(second_virtual_introspection->current_frame_reinforcement_page_count,
    0U);
}

//! Directional feedback/address-space identity must track snapped XY light-view
//! translation, but it should still ignore pure Z pull-back padding changes.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualFeedbackAddressSpaceTracksSnappedXYTranslationButIgnoresZPullback)
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
    glm::vec4(0.0F, 0.0F, 0.0F, 0.08F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 210 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 210 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(first_virtual_introspection->directional_virtual_metadata.empty());

  const auto& first_metadata
    = first_virtual_introspection->directional_virtual_metadata.front();
  auto xy_translated_metadata = first_metadata;
  xy_translated_metadata.light_view[3][0] += 17.0F;
  xy_translated_metadata.light_view[3][1] -= 9.0F;

  auto z_translated_metadata = first_metadata;
  z_translated_metadata.light_view[3][2] += 5.0F;

  const auto first_hash
    = oxygen::renderer::internal::shadow_detail::
      HashDirectionalVirtualFeedbackAddressSpace(first_metadata);
  const auto xy_translated_hash
    = oxygen::renderer::internal::shadow_detail::
      HashDirectionalVirtualFeedbackAddressSpace(xy_translated_metadata);
  const auto z_translated_hash
    = oxygen::renderer::internal::shadow_detail::
      HashDirectionalVirtualFeedbackAddressSpace(z_translated_metadata);

  EXPECT_NE(first_hash, xy_translated_hash);
  EXPECT_EQ(first_hash, z_translated_hash);
  EXPECT_FALSE(
    oxygen::renderer::internal::shadow_detail::DirectionalCacheMat4Equal(
      oxygen::renderer::internal::shadow_detail::
        BuildDirectionalAddressSpaceComparableLightView(
          first_metadata.light_view),
      oxygen::renderer::internal::shadow_detail::
        BuildDirectionalAddressSpaceComparableLightView(
          xy_translated_metadata.light_view)));
  EXPECT_TRUE(
    oxygen::renderer::internal::shadow_detail::DirectionalCacheMat4Equal(
      oxygen::renderer::internal::shadow_detail::
        BuildDirectionalAddressSpaceComparableLightView(
          first_metadata.light_view),
      oxygen::renderer::internal::shadow_detail::
        BuildDirectionalAddressSpaceComparableLightView(
          z_translated_metadata.light_view)));
}

//! Directional page contents must rerasterize when the light-space depth basis
//! changes, even if the XY lattice is unchanged and the resident page keys
//! remain valid.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualInvalidatesCleanPagesWhenDepthMappingChanges)
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
    glm::vec4(0.0F, 0.0F, 0.0F, 0.08F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 211 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 211 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(first_virtual_introspection->directional_virtual_metadata.empty());
  ASSERT_GT(first_virtual_introspection->mapped_page_count, 0U);

  const auto first_clip
    = first_virtual_introspection->directional_virtual_metadata.front()
        .clip_metadata[0];
  const float first_light_view_depth_basis
    = first_virtual_introspection->directional_virtual_metadata.front()
        .light_view[3][2];
  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 211 });

  view_constants.SetCameraPosition(
    view_constants.GetCameraPosition() + synthetic_sun.direction_ws * 2.0F);
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 211 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 211 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_FALSE(second_virtual_introspection->directional_virtual_metadata.empty());
  ASSERT_GT(second_virtual_introspection->mapped_page_count, 0U);

  const auto second_clip
    = second_virtual_introspection->directional_virtual_metadata.front()
        .clip_metadata[0];
  const float second_light_view_depth_basis
    = second_virtual_introspection->directional_virtual_metadata.front()
        .light_view[3][2];
  EXPECT_NEAR(first_clip.origin_page_scale.x, second_clip.origin_page_scale.x,
    1.0e-4F);
  EXPECT_NEAR(first_clip.origin_page_scale.y, second_clip.origin_page_scale.y,
    1.0e-4F);
  EXPECT_NE(first_light_view_depth_basis, second_light_view_depth_basis);
  EXPECT_GT(second_virtual_introspection->rerasterized_page_count, 0U);
  EXPECT_EQ(second_virtual_introspection->pending_page_count,
    second_virtual_introspection->rerasterized_page_count);
}

//! Coarse fallback pages must track the visible receiver depth range, not the
//! camera far plane. A tiny near receiver should require fewer coarse pages
//! than an unbounded frustum with no receiver guidance.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualCoarseBackboneUsesVisibleReceiverDepthRange)
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
  const std::array<glm::vec4, 1> near_receiver {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.08F),
  };

  (void)shadow_manager->PublishForView(oxygen::ViewId { 211 }, view_constants,
    manager, shadow_casters, near_receiver, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* bounded_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 211 });
  ASSERT_NE(bounded_introspection, nullptr);

  (void)shadow_manager->PublishForView(oxygen::ViewId { 212 }, view_constants,
    manager, shadow_casters, {}, &synthetic_sun, std::chrono::milliseconds(16));
  const auto* unbounded_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 212 });
  ASSERT_NE(unbounded_introspection, nullptr);

  EXPECT_GT(unbounded_introspection->coarse_backbone_page_count, 0U);
  EXPECT_LT(bounded_introspection->coarse_backbone_page_count,
    unbounded_introspection->coarse_backbone_page_count);
}

//! Current-frame visible-receiver bootstrap must stay sparse. Two far-apart
//! receivers in the same fine clip must not be merged into one dense page box.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualBootstrapKeepsSparseReceiverPagesSparse)
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
    view_constants, oxygen::ViewId { 133 }, shadow_casters, synthetic_sun);
  ASSERT_GT(layout.pages_per_axis, 8U);
  const auto* bootstrap_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 133 });
  ASSERT_NE(bootstrap_introspection, nullptr);
  ASSERT_FALSE(bootstrap_introspection->directional_virtual_metadata.empty());

  const auto& metadata
    = bootstrap_introspection->directional_virtual_metadata.front();
  const auto inverse_light_view = glm::inverse(metadata.light_view);
  const float page_world = metadata.clip_metadata[0].origin_page_scale.z;
  const float origin_x = metadata.clip_metadata[0].origin_page_scale.x;
  const float origin_y = metadata.clip_metadata[0].origin_page_scale.y;
  const auto receiver_world_center = [&](const std::uint32_t page_x,
                                       const std::uint32_t page_y) {
    const glm::vec3 light_space_point(
      origin_x + (static_cast<float>(page_x) + 0.5F) * page_world,
      origin_y + (static_cast<float>(page_y) + 0.5F) * page_world, 0.1F);
    return glm::vec3(
      inverse_light_view * glm::vec4(light_space_point, 1.0F));
  };

  const auto world_a = receiver_world_center(0U, 0U);
  const auto world_b = receiver_world_center(7U, 7U);
  const std::array<glm::vec4, 2> visible_receivers {
    glm::vec4(world_a, page_world * 0.1F),
    glm::vec4(world_b, page_world * 0.1F),
  };
  const std::uint32_t sparse_page_a = 0U;
  const std::uint32_t sparse_page_b = 7U * layout.pages_per_axis + 7U;
  const std::uint32_t gap_page = 4U * layout.pages_per_axis + 4U;

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto publication = shadow_manager->PublishForView(oxygen::ViewId { 34 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 34 });

  ASSERT_NE(
    publication.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(virtual_introspection, nullptr);
  ASSERT_GT(virtual_introspection->page_table_entries.size(), sparse_page_b);
  ASSERT_GT(virtual_introspection->page_table_entries.size(), gap_page);

  std::uint32_t mapped_clip0_pages = 0U;
  for (std::uint32_t i = 0U; i < layout.pages_per_level; ++i) {
    if (virtual_introspection->page_table_entries[i] != 0U) {
      ++mapped_clip0_pages;
    }
  }

  EXPECT_NE(virtual_introspection->page_table_entries[sparse_page_a], 0U);
  EXPECT_NE(virtual_introspection->page_table_entries[sparse_page_b], 0U);
  EXPECT_EQ(virtual_introspection->page_table_entries[gap_page], 0U);
  EXPECT_LT(mapped_clip0_pages, 50U);
}

//! Once request feedback is live, the backend should add only a tightly
//! bounded current-frame delta band on the nearest fine clips. It must not
//! fall back to the old dense bootstrap or whole-frustum reinforcement, but
//! motion still needs some current-frame fine coverage so shading does not
//! outrun delayed feedback.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualAcceptedFeedbackUsesBoundedDeltaReinforcementDuringClipShift)
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

  const auto make_view_constants = [](const glm::vec3& camera_position,
                                     const glm::vec3& /*target*/,
                                     const SequenceNumber sequence,
                                     const Slot slot) {
    ViewConstants view_constants;
    view_constants
      .SetProjectionMatrix(glm::perspectiveRH_ZO(
        glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
      .SetCameraPosition(camera_position);
    view_constants.SetFrameSequenceNumber(sequence, ViewConstants::kRenderer);
    view_constants.SetFrameSlot(slot, ViewConstants::kRenderer);
    return view_constants;
  };

  auto view_constants = make_view_constants(
    glm::vec3(0.0F, -8.0F, 5.0F), glm::vec3(0.0F, 0.0F, 0.5F),
    SequenceNumber { 1 }, Slot { 0 });

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
  const std::array<glm::vec4, 2> shadow_casters {
    glm::vec4(-0.9F, 0.0F, 0.5F, 0.5F),
    glm::vec4(0.9F, 0.0F, 1.5F, 1.5F),
  };
  const std::array<glm::vec4, 3> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 6.5F),
    glm::vec4(-0.9F, 0.0F, 0.5F, 0.5F),
    glm::vec4(0.9F, 0.0F, 1.5F, 1.5F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 138 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 138 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(first_virtual_introspection->directional_virtual_metadata.empty());
  const auto first_receiver_bootstrap_pages
    = first_virtual_introspection->receiver_bootstrap_page_count;
  ASSERT_GT(first_receiver_bootstrap_pages, 0U);
  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 138 });

  const auto first_layout = BuildVirtualFeedbackLayout(
    first_virtual_introspection->directional_virtual_metadata.front());
  std::vector<std::uint32_t> requested_pages {};
  requested_pages.reserve(64U);
  const auto fine_page_limit = std::min<std::uint32_t>(
    first_layout.pages_per_level,
    static_cast<std::uint32_t>(
      first_virtual_introspection->page_table_entries.size()));
  for (std::uint32_t page_index = 0U; page_index < fine_page_limit; ++page_index) {
    if (first_virtual_introspection->page_table_entries[page_index] == 0U) {
      continue;
    }
    requested_pages.push_back(page_index);
    if (requested_pages.size() == 64U) {
      break;
    }
  }
  ASSERT_FALSE(requested_pages.empty());

  const auto& first_metadata
    = first_virtual_introspection->directional_virtual_metadata.front();
  const auto inverse_light_view = glm::inverse(first_metadata.light_view);
  const float fine_page_world
    = first_metadata.clip_metadata[0].origin_page_scale.z;
  const glm::vec3 world_origin
    = glm::vec3(inverse_light_view * glm::vec4(0.0F, 0.0F, 0.0F, 1.0F));
  const glm::vec3 world_shift_x = glm::vec3(
    inverse_light_view * glm::vec4(fine_page_world, 0.0F, 0.0F, 1.0F))
    - world_origin;

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 138 },
    MakeVirtualRequestFeedback(first_layout, SequenceNumber { 1 },
      std::span<const std::uint32_t>(
        requested_pages.data(), requested_pages.size())));

  view_constants = make_view_constants(
    glm::vec3(0.0F, -8.0F, 5.0F) + world_shift_x,
    glm::vec3(0.0F, 0.0F, 0.5F), SequenceNumber { 2 }, Slot { 1 });
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 138 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 138 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_FALSE(second_virtual_introspection->directional_virtual_metadata.empty());
  EXPECT_TRUE(second_virtual_introspection->used_request_feedback);
  EXPECT_GT(second_virtual_introspection->feedback_requested_page_count, 0U);
  EXPECT_GT(second_virtual_introspection->feedback_refinement_page_count, 0U);
  EXPECT_EQ(second_virtual_introspection->receiver_bootstrap_page_count, 0U);
  EXPECT_GT(second_virtual_introspection->current_frame_reinforcement_page_count, 0U);
  EXPECT_LT(second_virtual_introspection->current_frame_reinforcement_page_count,
    first_receiver_bootstrap_pages);

  const auto second_layout = BuildVirtualFeedbackLayout(
    second_virtual_introspection->directional_virtual_metadata.front());
  for (const auto resident_key :
    RequestedResidentKeys(first_layout,
      std::span<const std::uint32_t>(
        requested_pages.data(), requested_pages.size()))) {
    const auto translated_page_index
      = LocalPageIndexForResidentKey(second_layout, resident_key);
    if (!translated_page_index.has_value()) {
      continue;
    }

    ASSERT_LT(*translated_page_index,
      second_virtual_introspection->page_table_entries.size());
    EXPECT_NE(second_virtual_introspection->page_table_entries[*translated_page_index],
      0U);
  }
}

//! Accepted feedback is sourced from an older request frame. The current-frame
//! reinforcement band must therefore be measured against that source frame's
//! region, not only against the immediately previous publication.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualAcceptedFeedbackDeltaTracksFeedbackSourceFrame)
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

  const auto make_view_constants = [](const glm::vec3& camera_position,
                                     const glm::vec3& /*target*/,
                                     const SequenceNumber sequence,
                                     const Slot slot) {
    ViewConstants view_constants;
    view_constants
      .SetProjectionMatrix(glm::perspectiveRH_ZO(
        glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
      .SetCameraPosition(camera_position);
    view_constants.SetFrameSequenceNumber(sequence, ViewConstants::kRenderer);
    view_constants.SetFrameSlot(slot, ViewConstants::kRenderer);
    return view_constants;
  };

  const glm::vec3 base_camera = glm::vec3(0.0F, -8.0F, 5.0F);
  const glm::vec3 base_target = glm::vec3(0.0F, 0.0F, 0.5F);
  auto view_constants = make_view_constants(
    base_camera, base_target, SequenceNumber { 1 }, Slot { 0 });

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
  const std::array<glm::vec4, 2> shadow_casters {
    glm::vec4(-0.9F, 0.0F, 0.5F, 0.5F),
    glm::vec4(0.9F, 0.0F, 1.5F, 1.5F),
  };
  const std::array<glm::vec4, 3> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 6.5F),
    glm::vec4(-0.9F, 0.0F, 0.5F, 0.5F),
    glm::vec4(0.9F, 0.0F, 1.5F, 1.5F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 139 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 139 });
  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(first_virtual_introspection->directional_virtual_metadata.empty());
  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 139 });

  const auto first_layout = BuildVirtualFeedbackLayout(
    first_virtual_introspection->directional_virtual_metadata.front());
  std::vector<std::uint32_t> requested_pages {};
  requested_pages.reserve(64U);
  const auto fine_page_limit = std::min<std::uint32_t>(
    first_layout.pages_per_level,
    static_cast<std::uint32_t>(
      first_virtual_introspection->page_table_entries.size()));
  for (std::uint32_t page_index = 0U; page_index < fine_page_limit; ++page_index) {
    if (first_virtual_introspection->page_table_entries[page_index] == 0U) {
      continue;
    }
    requested_pages.push_back(page_index);
    if (requested_pages.size() == 64U) {
      break;
    }
  }
  ASSERT_FALSE(requested_pages.empty());

  const auto& first_metadata
    = first_virtual_introspection->directional_virtual_metadata.front();
  const auto inverse_light_view = glm::inverse(first_metadata.light_view);
  const float fine_page_world
    = first_metadata.clip_metadata[0].origin_page_scale.z;
  const glm::vec3 world_origin
    = glm::vec3(inverse_light_view * glm::vec4(0.0F, 0.0F, 0.0F, 1.0F));
  const glm::vec3 world_shift_x = glm::vec3(
    inverse_light_view * glm::vec4(fine_page_world, 0.0F, 0.0F, 1.0F))
    - world_origin;

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 139 },
    MakeVirtualRequestFeedback(first_layout, SequenceNumber { 1 },
      std::span<const std::uint32_t>(
        requested_pages.data(), requested_pages.size())));

  view_constants = make_view_constants(base_camera + world_shift_x,
    base_target + world_shift_x, SequenceNumber { 2 }, Slot { 1 });
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });
  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 139 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 139 });
  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_TRUE(second_virtual_introspection->used_request_feedback);

  const glm::vec3 far_shift = world_shift_x * 4.0F;
  view_constants = make_view_constants(base_camera + far_shift,
    base_target + far_shift, SequenceNumber { 5 }, Slot { 1 });
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 5 }, Slot { 1 });
  const auto fifth = shadow_manager->PublishForView(oxygen::ViewId { 139 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* fifth_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 139 });
  ASSERT_NE(fifth.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(fifth_virtual_introspection, nullptr);
  ASSERT_TRUE(fifth_virtual_introspection->used_request_feedback);
  EXPECT_EQ(fifth_virtual_introspection->current_frame_reinforcement_reference_frame,
    SequenceNumber { 1 }.get());
  EXPECT_GT(fifth_virtual_introspection->current_frame_reinforcement_page_count, 0U);
}

//! Caster movement should dirty only overlapping resident pages, leaving the
//! rest of the requested working set clean and reusable.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanSpatiallyInvalidatesDirtyPagesOnly)
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
  const std::array<glm::vec4, 2> initial_shadow_casters {
    glm::vec4(-8.0F, 0.0F, 0.5F, 0.5F),
    glm::vec4(8.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 2> moved_shadow_casters {
    glm::vec4(-8.0F, 0.0F, 0.5F, 0.5F),
    glm::vec4(10.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 2> visible_receivers {
    glm::vec4(-8.0F, 0.0F, 0.0F, 0.1F),
    glm::vec4(8.0F, 0.0F, 0.0F, 0.1F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 32 },
    view_constants, manager, initial_shadow_casters, visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 32 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_GT(first_virtual_introspection->resident_page_count, 0U);
  shadow_manager->MarkVirtualRenderPlanExecuted(oxygen::ViewId { 32 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 32 },
    view_constants, manager, moved_shadow_casters, visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16), 0x9001U);
  const auto* second_virtual_plan
    = shadow_manager->TryGetVirtualRenderPlan(oxygen::ViewId { 32 });
  const auto* second_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 32 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_plan, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  EXPECT_GT(second_virtual_plan->jobs.size(), 0U);
  EXPECT_LT(second_virtual_plan->jobs.size(),
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
    MakeVirtualRequestFeedback(layout, SequenceNumber { 1 },
      std::span<const std::uint32_t>(
        initial_requested_pages.data(), initial_requested_pages.size())));

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
    MakeVirtualRequestFeedback(layout, SequenceNumber { 2 },
      std::span<const std::uint32_t>(
        shifted_requested_pages.data(), shifted_requested_pages.size())));

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
    MakeVirtualRequestFeedback(layout, SequenceNumber { 1 }, { requested_page }));

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
