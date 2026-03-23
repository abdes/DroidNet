//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Renderer/Upload/InlineTransfersCoordinator.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
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

using oxygen::observer_ptr;
using oxygen::ViewId;
using oxygen::engine::ViewConstants;
using oxygen::engine::upload::DefaultUploadPolicy;
using oxygen::engine::upload::InlineTransfersCoordinator;
using oxygen::engine::upload::StagingProvider;
using oxygen::engine::upload::UploadCoordinator;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::QueueRole;
using oxygen::graphics::SingleQueueStrategy;
using oxygen::renderer::LightManager;
using oxygen::renderer::ShadowManager;
using oxygen::renderer::internal::RendererTagFactory;
using oxygen::renderer::testing::FakeGraphics;
using oxygen::scene::Scene;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;

class ShadowManagerVirtualHistoryTest : public testing::Test {
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

    light_manager_ = std::make_unique<LightManager>(observer_ptr { gfx_.get() },
      observer_ptr { staging_provider_.get() },
      observer_ptr { inline_transfers_.get() });
    shadow_manager_ = std::make_unique<ShadowManager>(
      observer_ptr { gfx_.get() }, observer_ptr { staging_provider_.get() },
      observer_ptr { inline_transfers_.get() },
      oxygen::ShadowQualityTier::kHigh,
      oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);

    scene_
      = std::make_shared<Scene>("ShadowManagerVirtualHistoryTestScene", 64);
    directional_node_ = CreateNode("sun", true, true);
    auto impl = directional_node_.GetImpl();
    ASSERT_TRUE(impl.has_value());
    impl->get().AddComponent<oxygen::scene::DirectionalLight>();
    auto& light = impl->get().GetComponent<oxygen::scene::DirectionalLight>();
    light.Common().casts_shadows = true;
    UpdateTransforms(directional_node_);

    shadow_caster_bounds_.push_back(glm::vec4(0.0F, 0.0F, 0.0F, 4.0F));
    visible_receiver_bounds_.push_back(glm::vec4(0.0F, 0.0F, 0.0F, 6.0F));
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

  auto BeginFrame(const std::uint64_t sequence) -> void
  {
    light_manager_->OnFrameStart(
      RendererTagFactory::Get(), SequenceNumber { sequence }, Slot { 0 });
    shadow_manager_->OnFrameStart(
      RendererTagFactory::Get(), SequenceNumber { sequence }, Slot { 0 });

    auto impl = directional_node_.GetImpl();
    ASSERT_TRUE(impl.has_value());
    light_manager_->CollectFromNode(impl->get());
    light_manager_->EnsureFrameResources();
  }

  [[nodiscard]] auto MakeViewConstants(const glm::vec3& camera_position,
    const std::uint64_t sequence) const -> ViewConstants
  {
    ViewConstants constants {};
    const auto view_matrix = glm::lookAtRH(camera_position,
      glm::vec3(0.0F, 0.0F, 0.0F), glm::vec3(0.0F, 1.0F, 0.0F));
    const auto projection_matrix
      = glm::perspectiveRH_ZO(glm::radians(60.0F), 1.0F, 0.1F, 100.0F);
    constants.SetViewMatrix(view_matrix)
      .SetProjectionMatrix(projection_matrix)
      .SetStableProjectionMatrix(projection_matrix)
      .SetCameraPosition(camera_position)
      .SetFrameSequenceNumber(
        SequenceNumber { sequence }, ViewConstants::kRenderer)
      .SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);
    return constants;
  }

  auto Publish(const ViewId view_id, const glm::vec3& camera_position,
    const std::uint64_t sequence,
    const std::uint64_t shadow_caster_content_hash = 0x1234ULL)
    -> oxygen::renderer::ShadowFramePublication
  {
    return shadow_manager_->PublishForView(view_id,
      MakeViewConstants(camera_position, sequence), *light_manager_, 1280.0F,
      shadow_caster_bounds_, visible_receiver_bounds_, nullptr,
      std::chrono::milliseconds(16), shadow_caster_content_hash);
  }

  auto FinalizeResolveState(const ViewId view_id) -> void
  {
    shadow_manager_->ResolveVirtualCurrentFrame(view_id);
    const auto queue_key = gfx_->QueueKeyFor(QueueRole::kGraphics);
    auto recorder = gfx_->AcquireCommandRecorder(
      queue_key, "ShadowManagerVirtualHistory", false);
    ASSERT_TRUE(static_cast<bool>(recorder));
    shadow_manager_->PrepareVirtualPageTableResources(view_id, *recorder);
    shadow_manager_->PrepareVirtualPageManagementOutputsForGpuWrite(
      view_id, *recorder);
    shadow_manager_->FinalizeVirtualPageManagementOutputs(view_id, *recorder);
  }

  auto RegisterExtractedSchedule(const ViewId view_id,
    const std::uint64_t source_sequence,
    const std::uint32_t scheduled_page_count, const Slot slot = Slot { 0 })
    -> void
  {
    const auto buffer = gfx_->CreateBuffer(BufferDesc {
      .size_bytes = sizeof(std::uint32_t),
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kReadBack,
      .debug_name = "ShadowManagerVirtualHistoryTest.ScheduleReadback",
    });
    ASSERT_TRUE(static_cast<bool>(buffer));
    auto* mapped
      = static_cast<std::uint32_t*>(buffer->Map(0U, sizeof(std::uint32_t)));
    ASSERT_NE(mapped, nullptr);
    *mapped = scheduled_page_count;
    shadow_manager_->RegisterVirtualScheduleExtraction(
      view_id, buffer, mapped, SequenceNumber { source_sequence }, slot);
  }

  auto RegisterExtractedResolveStats(const ViewId view_id,
    const std::uint64_t source_sequence,
    const std::uint32_t scheduled_raster_page_count,
    const std::uint32_t rasterized_page_count,
    const std::uint32_t cached_page_transition_count,
    const Slot slot = Slot { 0 },
    const std::uint32_t requested_page_count = 0U,
    const std::uint32_t pages_requiring_schedule_count = 0U) -> void
  {
    const auto buffer = gfx_->CreateBuffer(BufferDesc {
      .size_bytes = sizeof(oxygen::renderer::VirtualShadowResolveStats),
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kReadBack,
      .debug_name = "ShadowManagerVirtualHistoryTest.ResolveStatsReadback",
    });
    ASSERT_TRUE(static_cast<bool>(buffer));
    auto* mapped = static_cast<oxygen::renderer::VirtualShadowResolveStats*>(
      buffer->Map(0U, sizeof(oxygen::renderer::VirtualShadowResolveStats)));
    ASSERT_NE(mapped, nullptr);
    *mapped = oxygen::renderer::VirtualShadowResolveStats {
      .scheduled_raster_page_count = scheduled_raster_page_count,
      .allocated_page_count = 0U,
      .requested_page_count = requested_page_count,
      .resident_dirty_page_count = 0U,
      .resident_clean_page_count = 0U,
      .pages_requiring_schedule_count = pages_requiring_schedule_count,
      .available_page_list_count = 0U,
      .rasterized_page_count = rasterized_page_count,
      .cached_page_transition_count = cached_page_transition_count,
    };
    shadow_manager_->RegisterVirtualResolveStatsExtraction(
      view_id, buffer, mapped, SequenceNumber { source_sequence }, slot);
  }

  auto RotateDirectionalLight(const glm::quat& rotation) -> void
  {
    auto transform = directional_node_.GetTransform();
    ASSERT_TRUE(transform.SetLocalRotation(rotation));
    UpdateTransforms(directional_node_);
  }

  [[nodiscard]] auto GetPageManagementState(const ViewId view_id) const
    -> std::optional<oxygen::renderer::VirtualShadowPageManagementStateSnapshot>
  {
    return shadow_manager_->TryGetVirtualPageManagementStateSnapshot(view_id);
  }

  auto ResetShadowCacheState() -> void { shadow_manager_->ResetCachedState(); }

  auto SubmitGpuRasterInputs(
    const ViewId view_id, const std::uint64_t source_sequence) -> void
  {
    shadow_manager_->SubmitVirtualGpuRasterInputs(
      view_id, oxygen::renderer::VirtualShadowGpuRasterInputs {
                 .source_frame_sequence = SequenceNumber { source_sequence },
               });
  }

  [[nodiscard]] auto GetGpuRasterInputs(const ViewId view_id) const
    -> const oxygen::renderer::VirtualShadowGpuRasterInputs*
  {
    return shadow_manager_->TryGetVirtualGpuRasterInputs(view_id);
  }

  [[nodiscard]] auto GetVirtualFramePacket(const ViewId view_id) const
    -> const oxygen::renderer::VirtualShadowFramePacket*
  {
    return shadow_manager_->TryGetVirtualFramePacket(view_id);
  }

  [[nodiscard]] auto GetVirtualDirectionalMetadata(const ViewId view_id) const
    -> const oxygen::engine::DirectionalVirtualShadowMetadata*
  {
    return shadow_manager_->TryGetVirtualDirectionalMetadata(view_id);
  }

  [[nodiscard]] auto GetPageFlagsBuffer(const ViewId view_id) const
    -> std::shared_ptr<oxygen::graphics::Buffer>
  {
    return shadow_manager_->TryGetVirtualPageFlagsBuffer(view_id);
  }

  [[nodiscard]] auto GetPhysicalPageMetadataBuffer(const ViewId view_id) const
    -> std::shared_ptr<oxygen::graphics::Buffer>
  {
    return shadow_manager_->TryGetVirtualPhysicalPageMetadataBuffer(view_id);
  }

  [[nodiscard]] auto GetResolveStatsBuffer(const ViewId view_id) const
    -> std::shared_ptr<oxygen::graphics::Buffer>
  {
    return shadow_manager_->TryGetVirtualResolveStatsBuffer(view_id);
  }

private:
  std::shared_ptr<FakeGraphics> gfx_;
  std::unique_ptr<UploadCoordinator> uploader_;
  std::shared_ptr<StagingProvider> staging_provider_;
  std::unique_ptr<InlineTransfersCoordinator> inline_transfers_;
  std::unique_ptr<LightManager> light_manager_;
  std::unique_ptr<ShadowManager> shadow_manager_;
  std::shared_ptr<Scene> scene_;
  SceneNode directional_node_;
  std::vector<glm::vec4> shadow_caster_bounds_;
  std::vector<glm::vec4> visible_receiver_bounds_;
};

TEST_F(ShadowManagerVirtualHistoryTest,
  PreviousUnrenderedFrameDoesNotReuseDirectionalCache)
{
  constexpr ViewId kViewId { 7U };

  BeginFrame(1U);
  const auto first_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 1U);
  EXPECT_TRUE(first_publication.shadow_instance_metadata_srv.IsValid());

  BeginFrame(2U);
  const auto second_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 5.0F), 2U);
  EXPECT_TRUE(second_publication.shadow_instance_metadata_srv.IsValid());

  const auto bindings = GetPageManagementState(kViewId);
  ASSERT_TRUE(bindings.has_value());
  EXPECT_TRUE(bindings->reset_page_management_state);
}

TEST_F(ShadowManagerVirtualHistoryTest,
  ResolvedButUnrenderedFrameDoesNotReuseDirectionalCache)
{
  constexpr ViewId kViewId { 9U };

  BeginFrame(1U);
  const auto first_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 1U);
  EXPECT_TRUE(first_publication.shadow_instance_metadata_srv.IsValid());
  FinalizeResolveState(kViewId);

  BeginFrame(2U);
  const auto second_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 2U);
  EXPECT_TRUE(second_publication.shadow_instance_metadata_srv.IsValid());
  EXPECT_FALSE(second_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_FALSE(second_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_FALSE(second_publication.virtual_shadow_physical_pool_srv.IsValid());
  EXPECT_FALSE(
    second_publication.virtual_shadow_physical_page_metadata_srv.IsValid());
  EXPECT_FALSE(
    second_publication.virtual_shadow_physical_page_lists_srv.IsValid());

  const auto bindings = GetPageManagementState(kViewId);
  ASSERT_TRUE(bindings.has_value());
  EXPECT_FALSE(bindings->reset_page_management_state);
}

TEST_F(
  ShadowManagerVirtualHistoryTest, RenderedFrameEnablesDirectionalCacheReuse)
{
  constexpr ViewId kViewId { 11U };

  BeginFrame(1U);
  const auto first_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 1U);
  EXPECT_TRUE(first_publication.shadow_instance_metadata_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 1U, 1U, 1U, 1U);

  BeginFrame(2U);
  const auto second_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 2U);
  EXPECT_TRUE(second_publication.shadow_instance_metadata_srv.IsValid());
  EXPECT_TRUE(second_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_TRUE(second_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_TRUE(second_publication.virtual_shadow_physical_pool_srv.IsValid());
  EXPECT_TRUE(
    second_publication.virtual_shadow_physical_page_metadata_srv.IsValid());
  EXPECT_TRUE(
    second_publication.virtual_shadow_physical_page_lists_srv.IsValid());

  const auto bindings = GetPageManagementState(kViewId);
  ASSERT_TRUE(bindings.has_value());
  EXPECT_FALSE(bindings->reset_page_management_state);

  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 2U, 1U, 1U, 1U);

  BeginFrame(3U);
  const auto third_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 3U);
  EXPECT_TRUE(third_publication.shadow_instance_metadata_srv.IsValid());
  EXPECT_TRUE(third_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_TRUE(third_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_TRUE(third_publication.virtual_shadow_physical_pool_srv.IsValid());
  EXPECT_TRUE(
    third_publication.virtual_shadow_physical_page_metadata_srv.IsValid());
  EXPECT_TRUE(
    third_publication.virtual_shadow_physical_page_lists_srv.IsValid());

  const auto second_bindings = GetPageManagementState(kViewId);
  ASSERT_TRUE(second_bindings.has_value());
  EXPECT_FALSE(second_bindings->reset_page_management_state);
}

TEST_F(ShadowManagerVirtualHistoryTest,
  ChangedShadowContentKeepsAddressSpaceButMarksResidentsDirty)
{
  constexpr ViewId kViewId { 12U };

  BeginFrame(1U);
  const auto first_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 1U, 0x1234ULL);
  EXPECT_TRUE(first_publication.shadow_instance_metadata_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 1U, 1U, 1U, 1U);

  BeginFrame(2U);
  const auto second_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 2U, 0xABCDULL);
  EXPECT_TRUE(second_publication.shadow_instance_metadata_srv.IsValid());

  const auto bindings = GetPageManagementState(kViewId);
  ASSERT_TRUE(bindings.has_value());
  EXPECT_FALSE(bindings->reset_page_management_state);
  EXPECT_TRUE(bindings->global_dirty_resident_contents);
}

TEST_F(ShadowManagerVirtualHistoryTest,
  FinalizedWithoutExtractedFeedbackDropsPreviouslyReusableDirectionalCache)
{
  constexpr ViewId kViewId { 13U };

  BeginFrame(1U);
  const auto first_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 1U);
  EXPECT_TRUE(first_publication.shadow_instance_metadata_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 1U, 1U, 1U, 1U);

  BeginFrame(2U);
  const auto second_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 2U);
  EXPECT_TRUE(second_publication.shadow_instance_metadata_srv.IsValid());
  FinalizeResolveState(kViewId);

  BeginFrame(3U);
  const auto third_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 3U);
  EXPECT_TRUE(third_publication.shadow_instance_metadata_srv.IsValid());
  EXPECT_FALSE(third_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_FALSE(third_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_FALSE(third_publication.virtual_shadow_physical_pool_srv.IsValid());
  EXPECT_FALSE(
    third_publication.virtual_shadow_physical_page_metadata_srv.IsValid());
  EXPECT_FALSE(
    third_publication.virtual_shadow_physical_page_lists_srv.IsValid());

  const auto bindings = GetPageManagementState(kViewId);
  ASSERT_TRUE(bindings.has_value());
  EXPECT_FALSE(bindings->reset_page_management_state);
}

TEST_F(ShadowManagerVirtualHistoryTest,
  ExtractedZeroRasterizedFramePreservesPriorDirectionalCache)
{
  constexpr ViewId kViewId { 14U };

  BeginFrame(1U);
  const auto first_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 1U);
  EXPECT_TRUE(first_publication.shadow_instance_metadata_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 1U, 1U, 1U, 1U);

  BeginFrame(2U);
  const auto second_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 2U);
  EXPECT_TRUE(second_publication.shadow_instance_metadata_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 2U, 0U, 0U, 0U);

  BeginFrame(3U);
  const auto third_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 3U);
  EXPECT_TRUE(third_publication.shadow_instance_metadata_srv.IsValid());

  const auto bindings = GetPageManagementState(kViewId);
  ASSERT_TRUE(bindings.has_value());
  EXPECT_FALSE(bindings->reset_page_management_state);
}

TEST_F(ShadowManagerVirtualHistoryTest,
  PostResetZeroScheduledFrameWithUnsatisfiedRequestsStaysWarmup)
{
  constexpr ViewId kViewId { 25U };

  BeginFrame(1U);
  const auto first_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 1U);
  EXPECT_TRUE(first_publication.shadow_instance_metadata_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 1U, 1U, 1U, 1U);

  BeginFrame(2U);
  const auto second_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 2U);
  EXPECT_TRUE(second_publication.virtual_shadow_page_table_srv.IsValid());

  ResetShadowCacheState();

  BeginFrame(3U);
  const auto third_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 3U);
  EXPECT_FALSE(third_publication.virtual_shadow_page_table_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 3U, 1U, 1U, 1U);

  BeginFrame(4U);
  const auto fourth_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 4U);
  EXPECT_FALSE(fourth_publication.virtual_shadow_page_table_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(
    kViewId, 4U, 0U, 0U, 0U, Slot { 0 }, 16U, 16U);

  BeginFrame(5U);
  const auto fifth_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 5U);
  EXPECT_FALSE(fifth_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_FALSE(fifth_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_FALSE(fifth_publication.virtual_shadow_physical_pool_srv.IsValid());

  const auto bindings = GetPageManagementState(kViewId);
  ASSERT_TRUE(bindings.has_value());
  EXPECT_FALSE(bindings->reset_page_management_state);
}

TEST_F(ShadowManagerVirtualHistoryTest,
  PendingResolveStatsExtractionsAcrossSlotsDoNotOverwriteEarlierFrames)
{
  constexpr ViewId kViewId { 24U };

  BeginFrame(1U);
  const auto first_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 1U);
  EXPECT_TRUE(first_publication.shadow_instance_metadata_srv.IsValid());
  FinalizeResolveState(kViewId);

  RegisterExtractedResolveStats(kViewId, 1U, 1U, 1U, 1U, Slot { 0 });
  RegisterExtractedResolveStats(kViewId, 2U, 0U, 0U, 0U, Slot { 1 });

  BeginFrame(2U);
  const auto second_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 2U);
  EXPECT_TRUE(second_publication.shadow_instance_metadata_srv.IsValid());

  const auto bindings = GetPageManagementState(kViewId);
  ASSERT_TRUE(bindings.has_value());
  EXPECT_FALSE(bindings->reset_page_management_state);

  BeginFrame(3U);
  const auto third_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 3U);
  EXPECT_TRUE(third_publication.shadow_instance_metadata_srv.IsValid());
  EXPECT_FALSE(third_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_FALSE(third_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_FALSE(third_publication.virtual_shadow_physical_pool_srv.IsValid());
  EXPECT_FALSE(
    third_publication.virtual_shadow_physical_page_metadata_srv.IsValid());
  EXPECT_FALSE(
    third_publication.virtual_shadow_physical_page_lists_srv.IsValid());

  const auto second_bindings = GetPageManagementState(kViewId);
  ASSERT_TRUE(second_bindings.has_value());
  EXPECT_FALSE(second_bindings->reset_page_management_state);
}

TEST_F(ShadowManagerVirtualHistoryTest,
  UnfinalizedFrameDropsPreviouslyReusableDirectionalCache)
{
  constexpr ViewId kViewId { 15U };

  BeginFrame(1U);
  const auto first_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 1U);
  EXPECT_TRUE(first_publication.shadow_instance_metadata_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 1U, 1U, 1U, 1U);

  BeginFrame(2U);
  const auto second_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 2U);
  EXPECT_TRUE(second_publication.shadow_instance_metadata_srv.IsValid());

  BeginFrame(3U);
  const auto third_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 3U);
  EXPECT_TRUE(third_publication.shadow_instance_metadata_srv.IsValid());
  EXPECT_FALSE(third_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_FALSE(third_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_FALSE(third_publication.virtual_shadow_physical_pool_srv.IsValid());
  EXPECT_FALSE(
    third_publication.virtual_shadow_physical_page_metadata_srv.IsValid());
  EXPECT_FALSE(
    third_publication.virtual_shadow_physical_page_lists_srv.IsValid());

  const auto bindings = GetPageManagementState(kViewId);
  ASSERT_TRUE(bindings.has_value());
  EXPECT_FALSE(bindings->reset_page_management_state);
}

TEST_F(ShadowManagerVirtualHistoryTest,
  LightBasisChangeInvalidatesRenderedDirectionalCache)
{
  constexpr ViewId kViewId { 17U };

  BeginFrame(1U);
  const auto first_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 1U);
  EXPECT_TRUE(first_publication.shadow_instance_metadata_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 1U, 1U, 1U, 1U);

  RotateDirectionalLight(
    glm::angleAxis(glm::radians(25.0F), glm::vec3(0.0F, 1.0F, 0.0F)));

  BeginFrame(2U);
  const auto second_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 2U);
  EXPECT_TRUE(second_publication.shadow_instance_metadata_srv.IsValid());
  EXPECT_FALSE(second_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_FALSE(second_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_FALSE(second_publication.virtual_shadow_physical_pool_srv.IsValid());
  EXPECT_FALSE(
    second_publication.virtual_shadow_physical_page_metadata_srv.IsValid());
  EXPECT_FALSE(
    second_publication.virtual_shadow_physical_page_lists_srv.IsValid());

  const auto bindings = GetPageManagementState(kViewId);
  ASSERT_TRUE(bindings.has_value());
  EXPECT_TRUE(bindings->reset_page_management_state);
}

TEST_F(ShadowManagerVirtualHistoryTest,
  ResetCachedStateDropsPreviouslyReusableDirectionalCache)
{
  constexpr ViewId kViewId { 18U };

  BeginFrame(1U);
  const auto first_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 1U);
  EXPECT_TRUE(first_publication.shadow_instance_metadata_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 1U, 1U, 1U, 1U);

  BeginFrame(2U);
  const auto second_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 2U);
  EXPECT_TRUE(second_publication.shadow_instance_metadata_srv.IsValid());

  const auto bindings = GetPageManagementState(kViewId);
  ASSERT_TRUE(bindings.has_value());
  EXPECT_FALSE(bindings->reset_page_management_state);

  ResetShadowCacheState();

  BeginFrame(3U);
  const auto third_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 3U);
  EXPECT_TRUE(third_publication.shadow_instance_metadata_srv.IsValid());
  EXPECT_FALSE(third_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_FALSE(third_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_FALSE(third_publication.virtual_shadow_physical_pool_srv.IsValid());
  EXPECT_FALSE(
    third_publication.virtual_shadow_physical_page_metadata_srv.IsValid());
  EXPECT_FALSE(
    third_publication.virtual_shadow_physical_page_lists_srv.IsValid());

  const auto second_bindings = GetPageManagementState(kViewId);
  ASSERT_TRUE(second_bindings.has_value());
  EXPECT_TRUE(second_bindings->reset_page_management_state);
}

TEST_F(ShadowManagerVirtualHistoryTest,
  VirtualFramePacketOwnsPublicationMetadataAndRasterInputs)
{
  constexpr ViewId kViewId { 31U };

  BeginFrame(1U);
  const auto first_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 1U);
  ASSERT_TRUE(first_publication.virtual_directional_shadow_metadata_srv.IsValid());

  const auto* first_packet = GetVirtualFramePacket(kViewId);
  ASSERT_NE(first_packet, nullptr);
  EXPECT_TRUE(first_packet->has_directional_metadata);
  EXPECT_EQ(first_packet->virtual_directional_shadow_metadata_srv,
    first_publication.virtual_directional_shadow_metadata_srv);
  EXPECT_EQ(first_packet->publication.virtual_directional_shadow_metadata_srv,
    first_publication.virtual_directional_shadow_metadata_srv);
  EXPECT_FALSE(first_packet->has_gpu_raster_inputs);

  SubmitGpuRasterInputs(kViewId, 1U);

  const auto* second_packet = GetVirtualFramePacket(kViewId);
  ASSERT_NE(second_packet, nullptr);
  EXPECT_TRUE(second_packet->has_gpu_raster_inputs);
  EXPECT_EQ(second_packet->gpu_raster_inputs.source_frame_sequence,
    SequenceNumber { 1U });
  EXPECT_EQ(second_packet->virtual_directional_shadow_metadata_srv,
    first_publication.virtual_directional_shadow_metadata_srv);

  ResetShadowCacheState();
  EXPECT_EQ(GetVirtualFramePacket(kViewId), nullptr);
}

TEST_F(ShadowManagerVirtualHistoryTest,
  ResetCachedStateRequiresStableValidationFrameBeforeReuse)
{
  constexpr ViewId kViewId { 21U };

  BeginFrame(1U);
  const auto first_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 1U);
  EXPECT_TRUE(first_publication.shadow_instance_metadata_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 1U, 1U, 1U, 1U);

  BeginFrame(2U);
  const auto second_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 2U);
  EXPECT_TRUE(second_publication.virtual_shadow_page_table_srv.IsValid());

  ResetShadowCacheState();

  BeginFrame(3U);
  const auto third_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 3U);
  EXPECT_FALSE(third_publication.virtual_shadow_page_table_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 3U, 1U, 1U, 1U);

  BeginFrame(4U);
  const auto fourth_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 4U);
  EXPECT_FALSE(fourth_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_FALSE(fourth_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_FALSE(fourth_publication.virtual_shadow_physical_pool_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 4U, 1U, 1U, 1U);

  BeginFrame(5U);
  const auto fifth_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 5U);
  EXPECT_FALSE(fifth_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_FALSE(fifth_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_FALSE(fifth_publication.virtual_shadow_physical_pool_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 5U, 0U, 0U, 0U);

  BeginFrame(6U);
  const auto sixth_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 6U);
  EXPECT_TRUE(sixth_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_TRUE(sixth_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_TRUE(sixth_publication.virtual_shadow_physical_pool_srv.IsValid());
}

TEST_F(ShadowManagerVirtualHistoryTest,
  ResetCachedStatePublishesLiveOnLastRasterFrameStableValidation)
{
  constexpr ViewId kViewId { 29U };

  BeginFrame(1U);
  const auto first_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 1U);
  EXPECT_TRUE(first_publication.shadow_instance_metadata_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 1U, 1U, 1U, 1U);

  BeginFrame(2U);
  const auto second_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 2U);
  EXPECT_TRUE(second_publication.virtual_shadow_page_table_srv.IsValid());

  ResetShadowCacheState();

  BeginFrame(3U);
  const auto third_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 3U);
  EXPECT_FALSE(third_publication.virtual_shadow_page_table_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 3U, 40U, 40U, 40U);

  BeginFrame(4U);
  const auto fourth_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 4U);
  EXPECT_FALSE(fourth_publication.virtual_shadow_page_table_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 4U, 13U, 13U, 13U, Slot { 0 }, 51U);

  BeginFrame(5U);
  const auto fifth_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 5U);
  EXPECT_FALSE(fifth_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_FALSE(fifth_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_FALSE(fifth_publication.virtual_shadow_physical_pool_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 5U, 0U, 0U, 0U, Slot { 0 }, 51U);

  BeginFrame(6U);
  const auto sixth_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 6U);
  EXPECT_TRUE(sixth_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_TRUE(sixth_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_TRUE(sixth_publication.virtual_shadow_physical_pool_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedSchedule(kViewId, 6U, 0U);
  RegisterExtractedResolveStats(kViewId, 6U, 0U, 0U, 0U);

  BeginFrame(7U);
  const auto seventh_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 7U);
  EXPECT_TRUE(seventh_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_TRUE(seventh_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_TRUE(seventh_publication.virtual_shadow_physical_pool_srv.IsValid());
}

TEST_F(ShadowManagerVirtualHistoryTest,
  ResetWarmupFramesDoNotSeedDirectionalBasisStabilization)
{
  constexpr ViewId kViewId { 43U };
  const glm::vec3 kCameraPosition(
    -0.8322786F, -11.695879F, 11.369722F);

  BeginFrame(1U);
  const auto first_publication = Publish(kViewId, kCameraPosition, 1U);
  EXPECT_TRUE(first_publication.shadow_instance_metadata_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 1U, 1U, 1U, 1U);

  BeginFrame(2U);
  const auto second_publication = Publish(kViewId, kCameraPosition, 2U);
  EXPECT_TRUE(second_publication.virtual_shadow_page_table_srv.IsValid());
  const auto* baseline_metadata = GetVirtualDirectionalMetadata(kViewId);
  ASSERT_NE(baseline_metadata, nullptr);
  const auto baseline = *baseline_metadata;

  ResetShadowCacheState();

  BeginFrame(3U);
  const auto third_publication = Publish(kViewId, kCameraPosition, 3U);
  EXPECT_FALSE(third_publication.virtual_shadow_page_table_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 3U, 40U, 40U, 40U, Slot { 0 }, 40U);

  BeginFrame(4U);
  const auto fourth_publication = Publish(kViewId, kCameraPosition, 4U);
  EXPECT_FALSE(fourth_publication.virtual_shadow_page_table_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 4U, 0U, 0U, 0U, Slot { 0 }, 40U);

  BeginFrame(5U);
  const auto fifth_publication = Publish(kViewId, kCameraPosition, 5U);
  EXPECT_TRUE(fifth_publication.virtual_shadow_page_table_srv.IsValid());
  const auto* reloaded_metadata = GetVirtualDirectionalMetadata(kViewId);
  ASSERT_NE(reloaded_metadata, nullptr);

  EXPECT_NEAR(reloaded_metadata->clip_metadata[0].origin_page_scale.x,
    baseline.clip_metadata[0].origin_page_scale.x, 1.0e-4F);
  EXPECT_NEAR(reloaded_metadata->clip_metadata[0].origin_page_scale.y,
    baseline.clip_metadata[0].origin_page_scale.y, 1.0e-4F);
  EXPECT_NEAR(reloaded_metadata->clipmap_receiver_origin_lod_bias.x,
    baseline.clipmap_receiver_origin_lod_bias.x, 1.0e-4F);
  EXPECT_NEAR(reloaded_metadata->clipmap_receiver_origin_lod_bias.y,
    baseline.clipmap_receiver_origin_lod_bias.y, 1.0e-4F);
  EXPECT_NEAR(reloaded_metadata->clipmap_receiver_origin_lod_bias.z,
    baseline.clipmap_receiver_origin_lod_bias.z, 1.0e-4F);
  EXPECT_NEAR(
    reloaded_metadata->light_view[3][0], baseline.light_view[3][0], 1.0e-4F);
  EXPECT_NEAR(
    reloaded_metadata->light_view[3][1], baseline.light_view[3][1], 1.0e-4F);
  EXPECT_NEAR(
    reloaded_metadata->light_view[3][2], baseline.light_view[3][2], 1.0e-3F);

  FinalizeResolveState(kViewId);
  RegisterExtractedSchedule(kViewId, 5U, 0U);
  RegisterExtractedResolveStats(kViewId, 5U, 0U, 0U, 0U);

  BeginFrame(6U);
  const auto sixth_publication = Publish(kViewId, kCameraPosition, 6U);
  EXPECT_TRUE(sixth_publication.virtual_shadow_page_table_srv.IsValid());
}

TEST_F(ShadowManagerVirtualHistoryTest,
  ZeroRasterValidationPreservesLastRenderedDirectionalBasis)
{
  constexpr ViewId kViewId { 44U };
  const glm::vec3 kCameraPosition(
    0.63979197F, -11.054302F, 7.881915F);

  BeginFrame(1U);
  const auto first_publication = Publish(kViewId, kCameraPosition, 1U);
  EXPECT_TRUE(first_publication.shadow_instance_metadata_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedSchedule(kViewId, 1U, 90U);
  RegisterExtractedResolveStats(kViewId, 1U, 90U, 90U, 90U, Slot { 0 }, 90U);

  BeginFrame(2U);
  const auto second_publication = Publish(kViewId, kCameraPosition, 2U);
  EXPECT_TRUE(second_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_TRUE(second_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_TRUE(second_publication.virtual_shadow_physical_pool_srv.IsValid());
  const auto* baseline_metadata = GetVirtualDirectionalMetadata(kViewId);
  ASSERT_NE(baseline_metadata, nullptr);
  const auto baseline = *baseline_metadata;

  FinalizeResolveState(kViewId);
  RegisterExtractedSchedule(kViewId, 2U, 0U);
  RegisterExtractedResolveStats(kViewId, 2U, 0U, 0U, 0U, Slot { 0 }, 90U);

  BeginFrame(3U);
  const auto third_publication = Publish(kViewId, kCameraPosition, 3U);
  EXPECT_TRUE(third_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_TRUE(third_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_TRUE(third_publication.virtual_shadow_physical_pool_srv.IsValid());

  const auto* preserved_metadata = GetVirtualDirectionalMetadata(kViewId);
  ASSERT_NE(preserved_metadata, nullptr);
  EXPECT_NEAR(preserved_metadata->clip_metadata[0].origin_page_scale.x,
    baseline.clip_metadata[0].origin_page_scale.x, 1.0e-4F);
  EXPECT_NEAR(preserved_metadata->clip_metadata[0].origin_page_scale.y,
    baseline.clip_metadata[0].origin_page_scale.y, 1.0e-4F);
  EXPECT_NEAR(preserved_metadata->clip_metadata[0].origin_page_scale.w,
    baseline.clip_metadata[0].origin_page_scale.w, 1.0e-4F);
  EXPECT_NEAR(preserved_metadata->clip_metadata[0].bias_reserved.x,
    baseline.clip_metadata[0].bias_reserved.x, 1.0e-4F);
  EXPECT_NEAR(
    preserved_metadata->light_view[3][2], baseline.light_view[3][2], 1.0e-4F);
}

TEST_F(ShadowManagerVirtualHistoryTest,
  ResetWarmupRasterFeedbackPreservesRenderedDirectionalBasisIntoValidation)
{
  constexpr ViewId kViewId { 46U };
  const glm::vec3 kCameraPosition(
    0.63979197F, -11.054302F, 7.881915F);

  BeginFrame(1U);
  const auto first_publication = Publish(kViewId, kCameraPosition, 1U);
  EXPECT_TRUE(first_publication.shadow_instance_metadata_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 1U, 1U, 1U, 1U);

  BeginFrame(2U);
  const auto second_publication = Publish(kViewId, kCameraPosition, 2U);
  EXPECT_TRUE(second_publication.virtual_shadow_page_table_srv.IsValid());

  ResetShadowCacheState();

  BeginFrame(3U);
  const auto third_publication = Publish(kViewId, kCameraPosition, 3U);
  EXPECT_FALSE(third_publication.virtual_shadow_page_table_srv.IsValid());
  const auto* rendered_basis_metadata = GetVirtualDirectionalMetadata(kViewId);
  ASSERT_NE(rendered_basis_metadata, nullptr);
  const auto rendered_basis = *rendered_basis_metadata;

  FinalizeResolveState(kViewId);
  RegisterExtractedSchedule(kViewId, 3U, 90U);
  RegisterExtractedResolveStats(kViewId, 3U, 90U, 90U, 90U, Slot { 0 }, 90U);

  BeginFrame(4U);
  const auto fourth_publication = Publish(kViewId, kCameraPosition, 4U);
  EXPECT_FALSE(fourth_publication.virtual_shadow_page_table_srv.IsValid());
  const auto* preserved_metadata = GetVirtualDirectionalMetadata(kViewId);
  ASSERT_NE(preserved_metadata, nullptr);
  EXPECT_NEAR(preserved_metadata->clip_metadata[0].origin_page_scale.x,
    rendered_basis.clip_metadata[0].origin_page_scale.x, 1.0e-4F);
  EXPECT_NEAR(preserved_metadata->clip_metadata[0].origin_page_scale.y,
    rendered_basis.clip_metadata[0].origin_page_scale.y, 1.0e-4F);
  EXPECT_NEAR(preserved_metadata->clip_metadata[0].origin_page_scale.w,
    rendered_basis.clip_metadata[0].origin_page_scale.w, 1.0e-4F);
  EXPECT_NEAR(preserved_metadata->clip_metadata[0].bias_reserved.x,
    rendered_basis.clip_metadata[0].bias_reserved.x, 1.0e-4F);
  EXPECT_NEAR(preserved_metadata->clipmap_receiver_origin_lod_bias.z,
    rendered_basis.clipmap_receiver_origin_lod_bias.z, 1.0e-4F);
  EXPECT_NEAR(
    preserved_metadata->light_view[3][2], rendered_basis.light_view[3][2], 1.0e-4F);
}

TEST_F(ShadowManagerVirtualHistoryTest,
  RequestedPagesAlreadyCachedDoNotBlockResetStableValidationOrImmediateReuse)
{
  constexpr ViewId kViewId { 47U };
  const glm::vec3 kCameraPosition(
    0.63979197F, -11.054302F, 7.881915F);

  BeginFrame(1U);
  const auto first_publication = Publish(kViewId, kCameraPosition, 1U);
  EXPECT_TRUE(first_publication.shadow_instance_metadata_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 1U, 1U, 1U, 1U);

  BeginFrame(2U);
  const auto second_publication = Publish(kViewId, kCameraPosition, 2U);
  EXPECT_TRUE(second_publication.virtual_shadow_page_table_srv.IsValid());

  ResetShadowCacheState();

  BeginFrame(3U);
  const auto third_publication = Publish(kViewId, kCameraPosition, 3U);
  EXPECT_FALSE(third_publication.virtual_shadow_page_table_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedSchedule(kViewId, 3U, 90U);
  RegisterExtractedResolveStats(kViewId, 3U, 90U, 90U, 90U, Slot { 0 }, 90U, 90U);

  BeginFrame(4U);
  const auto fourth_publication = Publish(kViewId, kCameraPosition, 4U);
  EXPECT_FALSE(fourth_publication.virtual_shadow_page_table_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedSchedule(kViewId, 4U, 0U);
  RegisterExtractedResolveStats(kViewId, 4U, 0U, 0U, 0U, Slot { 0 }, 90U, 0U);

  BeginFrame(5U);
  const auto fifth_publication = Publish(kViewId, kCameraPosition, 5U);
  EXPECT_TRUE(fifth_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_TRUE(fifth_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_TRUE(fifth_publication.virtual_shadow_physical_pool_srv.IsValid());
  const auto* live_metadata = GetVirtualDirectionalMetadata(kViewId);
  ASSERT_NE(live_metadata, nullptr);
  const auto baseline = *live_metadata;

  const auto bindings = GetPageManagementState(kViewId);
  ASSERT_TRUE(bindings.has_value());
  EXPECT_FALSE(bindings->reset_page_management_state);
  FinalizeResolveState(kViewId);
  RegisterExtractedSchedule(kViewId, 5U, 0U);
  RegisterExtractedResolveStats(kViewId, 5U, 0U, 0U, 0U);

  BeginFrame(6U);
  const auto sixth_publication = Publish(kViewId, kCameraPosition, 6U);
  EXPECT_TRUE(sixth_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_TRUE(sixth_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_TRUE(sixth_publication.virtual_shadow_physical_pool_srv.IsValid());

  const auto* reused_metadata = GetVirtualDirectionalMetadata(kViewId);
  ASSERT_NE(reused_metadata, nullptr);
  EXPECT_NEAR(reused_metadata->clip_metadata[0].origin_page_scale.x,
    baseline.clip_metadata[0].origin_page_scale.x, 1.0e-4F);
  EXPECT_NEAR(reused_metadata->clip_metadata[0].origin_page_scale.y,
    baseline.clip_metadata[0].origin_page_scale.y, 1.0e-4F);
  EXPECT_NEAR(reused_metadata->clip_metadata[0].origin_page_scale.w,
    baseline.clip_metadata[0].origin_page_scale.w, 1.0e-4F);
  EXPECT_NEAR(reused_metadata->clipmap_receiver_origin_lod_bias.z,
    baseline.clipmap_receiver_origin_lod_bias.z, 1.0e-4F);
  EXPECT_NEAR(
    reused_metadata->light_view[3][2], baseline.light_view[3][2], 1.0e-4F);

  const auto reused_bindings = GetPageManagementState(kViewId);
  ASSERT_TRUE(reused_bindings.has_value());
  EXPECT_FALSE(reused_bindings->reset_page_management_state);
}

TEST_F(ShadowManagerVirtualHistoryTest,
  ResetZeroRasterValidationKeepsLivePublicationOnLastRenderedBasis)
{
  constexpr ViewId kViewId { 45U };
  const glm::vec3 kCameraPosition(
    0.63979197F, -11.054302F, 7.881915F);

  BeginFrame(1U);
  const auto first_publication = Publish(kViewId, kCameraPosition, 1U);
  EXPECT_TRUE(first_publication.shadow_instance_metadata_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 1U, 1U, 1U, 1U);

  BeginFrame(2U);
  const auto second_publication = Publish(kViewId, kCameraPosition, 2U);
  EXPECT_TRUE(second_publication.virtual_shadow_page_table_srv.IsValid());
  const auto* pre_reset_metadata = GetVirtualDirectionalMetadata(kViewId);
  ASSERT_NE(pre_reset_metadata, nullptr);
  const auto baseline = *pre_reset_metadata;

  ResetShadowCacheState();

  BeginFrame(3U);
  const auto third_publication = Publish(kViewId, kCameraPosition, 3U);
  EXPECT_FALSE(third_publication.virtual_shadow_page_table_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 3U, 88U, 88U, 88U, Slot { 0 }, 90U);

  BeginFrame(4U);
  const auto fourth_publication = Publish(kViewId, kCameraPosition, 4U);
  EXPECT_FALSE(fourth_publication.virtual_shadow_page_table_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 4U, 6U, 6U, 6U, Slot { 0 }, 90U);

  BeginFrame(5U);
  const auto fifth_publication = Publish(kViewId, kCameraPosition, 5U);
  EXPECT_FALSE(fifth_publication.virtual_shadow_page_table_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedSchedule(kViewId, 5U, 0U);
  RegisterExtractedResolveStats(kViewId, 5U, 0U, 0U, 0U, Slot { 0 }, 90U);

  BeginFrame(6U);
  const auto sixth_publication = Publish(kViewId, kCameraPosition, 6U);
  EXPECT_TRUE(sixth_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_TRUE(sixth_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_TRUE(sixth_publication.virtual_shadow_physical_pool_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedSchedule(kViewId, 6U, 0U);
  RegisterExtractedResolveStats(kViewId, 6U, 0U, 0U, 0U);

  BeginFrame(7U);
  const auto seventh_publication = Publish(kViewId, kCameraPosition, 7U);
  EXPECT_TRUE(seventh_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_TRUE(seventh_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_TRUE(seventh_publication.virtual_shadow_physical_pool_srv.IsValid());

  const auto* live_metadata = GetVirtualDirectionalMetadata(kViewId);
  ASSERT_NE(live_metadata, nullptr);
  EXPECT_NEAR(live_metadata->clip_metadata[0].origin_page_scale.x,
    baseline.clip_metadata[0].origin_page_scale.x, 1.0e-4F);
  EXPECT_NEAR(live_metadata->clip_metadata[0].origin_page_scale.y,
    baseline.clip_metadata[0].origin_page_scale.y, 1.0e-4F);
  EXPECT_NEAR(live_metadata->clip_metadata[0].origin_page_scale.w,
    baseline.clip_metadata[0].origin_page_scale.w, 1.0e-4F);
  EXPECT_NEAR(live_metadata->clip_metadata[0].bias_reserved.x,
    baseline.clip_metadata[0].bias_reserved.x, 1.0e-4F);
  EXPECT_NEAR(
    live_metadata->light_view[3][0], baseline.light_view[3][0], 1.0e-4F);
  EXPECT_NEAR(
    live_metadata->light_view[3][1], baseline.light_view[3][1], 1.0e-4F);
  EXPECT_NEAR(
    live_metadata->light_view[3][2], baseline.light_view[3][2], 1.0e-3F);
}

TEST_F(ShadowManagerVirtualHistoryTest,
  ResetZeroScheduleWithUnsatisfiedRequestsStaysWarmupAndKeepsRenderedBasis)
{
  constexpr ViewId kViewId { 46U };
  const glm::vec3 kCameraPosition(
    0.63979197F, -11.054302F, 7.881915F);

  BeginFrame(1U);
  const auto first_publication = Publish(kViewId, kCameraPosition, 1U);
  EXPECT_TRUE(first_publication.shadow_instance_metadata_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 1U, 1U, 1U, 1U);

  BeginFrame(2U);
  const auto second_publication = Publish(kViewId, kCameraPosition, 2U);
  EXPECT_TRUE(second_publication.virtual_shadow_page_table_srv.IsValid());
  const auto* baseline_metadata = GetVirtualDirectionalMetadata(kViewId);
  ASSERT_NE(baseline_metadata, nullptr);
  const auto baseline = *baseline_metadata;

  ResetShadowCacheState();

  BeginFrame(3U);
  const auto third_publication = Publish(kViewId, kCameraPosition, 3U);
  EXPECT_FALSE(third_publication.virtual_shadow_page_table_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 3U, 89U, 89U, 89U, Slot { 0 }, 89U);

  BeginFrame(4U);
  const auto fourth_publication = Publish(kViewId, kCameraPosition, 4U);
  EXPECT_FALSE(fourth_publication.virtual_shadow_page_table_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 4U, 6U, 6U, 6U, Slot { 0 }, 90U);

  BeginFrame(5U);
  const auto fifth_publication = Publish(kViewId, kCameraPosition, 5U);
  EXPECT_FALSE(fifth_publication.virtual_shadow_page_table_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedSchedule(kViewId, 5U, 0U);
  RegisterExtractedResolveStats(
    kViewId, 5U, 0U, 0U, 0U, Slot { 0 }, 90U, 90U);

  BeginFrame(6U);
  const auto sixth_publication = Publish(kViewId, kCameraPosition, 6U);
  EXPECT_FALSE(sixth_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_FALSE(sixth_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_FALSE(sixth_publication.virtual_shadow_physical_pool_srv.IsValid());

  const auto* warmup_metadata = GetVirtualDirectionalMetadata(kViewId);
  ASSERT_NE(warmup_metadata, nullptr);
  EXPECT_NEAR(warmup_metadata->clip_metadata[0].origin_page_scale.x,
    baseline.clip_metadata[0].origin_page_scale.x, 1.0e-4F);
  EXPECT_NEAR(warmup_metadata->clip_metadata[0].origin_page_scale.y,
    baseline.clip_metadata[0].origin_page_scale.y, 1.0e-4F);
  EXPECT_NEAR(warmup_metadata->clip_metadata[0].origin_page_scale.w,
    baseline.clip_metadata[0].origin_page_scale.w, 1.0e-4F);
  EXPECT_NEAR(warmup_metadata->clip_metadata[0].bias_reserved.x,
    baseline.clip_metadata[0].bias_reserved.x, 1.0e-4F);
  EXPECT_NEAR(
    warmup_metadata->light_view[3][0], baseline.light_view[3][0], 1.0e-4F);
  EXPECT_NEAR(
    warmup_metadata->light_view[3][1], baseline.light_view[3][1], 1.0e-4F);
  EXPECT_NEAR(
    warmup_metadata->light_view[3][2], baseline.light_view[3][2], 1.0e-3F);
}

TEST_F(ShadowManagerVirtualHistoryTest,
  ResetCachedStateFirstPostClearRasterFrameDoesNotBecomeReusable)
{
  constexpr ViewId kViewId { 30U };

  BeginFrame(1U);
  const auto first_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 1U);
  EXPECT_TRUE(first_publication.shadow_instance_metadata_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 1U, 1U, 1U, 1U);

  BeginFrame(2U);
  const auto second_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 2U);
  EXPECT_TRUE(second_publication.virtual_shadow_page_table_srv.IsValid());

  ResetShadowCacheState();

  BeginFrame(3U);
  const auto third_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 3U);
  EXPECT_FALSE(third_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_FALSE(third_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_FALSE(third_publication.virtual_shadow_physical_pool_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedSchedule(kViewId, 3U, 51U);
  RegisterExtractedResolveStats(kViewId, 3U, 51U, 51U, 51U, Slot { 0 }, 51U);

  BeginFrame(4U);
  const auto fourth_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 4U);
  EXPECT_FALSE(fourth_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_FALSE(fourth_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_FALSE(fourth_publication.virtual_shadow_physical_pool_srv.IsValid());

  const auto bindings = GetPageManagementState(kViewId);
  ASSERT_TRUE(bindings.has_value());
  EXPECT_FALSE(bindings->reset_page_management_state);

  FinalizeResolveState(kViewId);
  RegisterExtractedSchedule(kViewId, 4U, 0U);
  RegisterExtractedResolveStats(kViewId, 4U, 0U, 0U, 0U);

  BeginFrame(5U);
  const auto fifth_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 5U);
  EXPECT_TRUE(fifth_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_TRUE(fifth_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_TRUE(fifth_publication.virtual_shadow_physical_pool_srv.IsValid());
}

TEST_F(ShadowManagerVirtualHistoryTest,
  ResetCachedStateRetiresPerViewPageManagementResources)
{
  constexpr ViewId kViewId { 26U };

  BeginFrame(1U);
  const auto first_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 1U);
  EXPECT_TRUE(first_publication.shadow_instance_metadata_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 1U, 1U, 1U, 1U);

  BeginFrame(2U);
  const auto second_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 2U);
  EXPECT_TRUE(second_publication.virtual_shadow_page_table_srv.IsValid());

  const auto old_page_flags_buffer = GetPageFlagsBuffer(kViewId);
  const auto old_physical_page_metadata_buffer
    = GetPhysicalPageMetadataBuffer(kViewId);
  const auto old_resolve_stats_buffer = GetResolveStatsBuffer(kViewId);
  ASSERT_NE(old_page_flags_buffer, nullptr);
  ASSERT_NE(old_physical_page_metadata_buffer, nullptr);
  ASSERT_NE(old_resolve_stats_buffer, nullptr);

  ResetShadowCacheState();

  EXPECT_EQ(GetPageFlagsBuffer(kViewId), nullptr);
  EXPECT_EQ(GetPhysicalPageMetadataBuffer(kViewId), nullptr);
  EXPECT_EQ(GetResolveStatsBuffer(kViewId), nullptr);

  BeginFrame(3U);
  const auto third_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 3U);
  EXPECT_FALSE(third_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_NE(GetPageFlagsBuffer(kViewId), nullptr);
  EXPECT_NE(GetPageFlagsBuffer(kViewId), old_page_flags_buffer);
}

TEST_F(ShadowManagerVirtualHistoryTest,
  ResetCachedStateAdvancesShadowManagerViewGenerationForNewGpuInputs)
{
  constexpr ViewId kViewId { 27U };

  SubmitGpuRasterInputs(kViewId, 1U);
  const auto* first_inputs = GetGpuRasterInputs(kViewId);
  ASSERT_NE(first_inputs, nullptr);
  EXPECT_EQ(first_inputs->cache_epoch, 1U);
  EXPECT_EQ(first_inputs->view_generation, 1U);

  ResetShadowCacheState();
  EXPECT_EQ(GetGpuRasterInputs(kViewId), nullptr);

  SubmitGpuRasterInputs(kViewId, 2U);
  const auto* second_inputs = GetGpuRasterInputs(kViewId);
  ASSERT_NE(second_inputs, nullptr);
  EXPECT_EQ(second_inputs->cache_epoch, 2U);
  EXPECT_EQ(second_inputs->view_generation, 2U);
}

TEST_F(ShadowManagerVirtualHistoryTest,
  ScheduledWithoutRenderedFrameDoesNotReuseDirectionalCache)
{
  constexpr ViewId kViewId { 19U };

  BeginFrame(1U);
  const auto first_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 1U);
  EXPECT_TRUE(first_publication.shadow_instance_metadata_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedSchedule(kViewId, 1U, 8U);

  BeginFrame(2U);
  const auto second_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 2U);
  EXPECT_TRUE(second_publication.shadow_instance_metadata_srv.IsValid());
  EXPECT_FALSE(second_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_FALSE(second_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_FALSE(second_publication.virtual_shadow_physical_pool_srv.IsValid());
  EXPECT_FALSE(
    second_publication.virtual_shadow_physical_page_metadata_srv.IsValid());
  EXPECT_FALSE(
    second_publication.virtual_shadow_physical_page_lists_srv.IsValid());

  const auto bindings = GetPageManagementState(kViewId);
  ASSERT_TRUE(bindings.has_value());
  EXPECT_FALSE(bindings->reset_page_management_state);
}

TEST_F(ShadowManagerVirtualHistoryTest,
  IncompleteRasterizedResolveStatsDoNotEnableDirectionalCacheReuse)
{
  constexpr ViewId kViewId { 20U };

  BeginFrame(1U);
  const auto first_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 1U);
  EXPECT_TRUE(first_publication.shadow_instance_metadata_srv.IsValid());
  FinalizeResolveState(kViewId);
  RegisterExtractedResolveStats(kViewId, 1U, 8U, 4U, 4U);

  BeginFrame(2U);
  const auto second_publication
    = Publish(kViewId, glm::vec3(0.0F, 0.0F, 10.0F), 2U);
  EXPECT_TRUE(second_publication.shadow_instance_metadata_srv.IsValid());
  EXPECT_FALSE(second_publication.virtual_shadow_page_table_srv.IsValid());
  EXPECT_FALSE(second_publication.virtual_shadow_page_flags_srv.IsValid());
  EXPECT_FALSE(second_publication.virtual_shadow_physical_pool_srv.IsValid());
  EXPECT_FALSE(
    second_publication.virtual_shadow_physical_page_metadata_srv.IsValid());
  EXPECT_FALSE(
    second_publication.virtual_shadow_physical_page_lists_srv.IsValid());

  const auto bindings = GetPageManagementState(kViewId);
  ASSERT_TRUE(bindings.has_value());
  EXPECT_FALSE(bindings->reset_page_management_state);
}

} // namespace
