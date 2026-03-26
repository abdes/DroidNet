//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>

#include <glm/vec4.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemData.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerSeam.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmProjectionTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmSceneInvalidationCoordinator.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpace.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine {
struct RenderContext;
struct VsmHzbUpdaterPassConfig;
struct VsmInvalidationPassConfig;
struct VsmPageFlagPropagationPassConfig;
struct VsmPageInitializationPassConfig;
struct VsmPageManagementPassConfig;
struct VsmPageRequestGeneratorPassConfig;
struct VsmProjectionPassConfig;
struct VsmShadowRasterizerPassConfig;
struct VsmStaticDynamicMergePassConfig;
class VsmHzbUpdaterPass;
class VsmInvalidationPass;
class VsmPageFlagPropagationPass;
class VsmPageInitializationPass;
class VsmPageManagementPass;
class VsmPageRequestGeneratorPass;
class VsmProjectionPass;
class VsmShadowRasterizerPass;
class VsmStaticDynamicMergePass;
} // namespace oxygen::engine

namespace oxygen::engine::upload {
class InlineTransfersCoordinator;
class StagingProvider;
}

namespace oxygen::graphics {
class CommandRecorder;
class Texture;
}

namespace oxygen::scene {
class Scene;
}

namespace oxygen::renderer::vsm {

class VsmShadowRenderer {
public:
  using ProviderT = engine::upload::StagingProvider;
  using CoordinatorT = engine::upload::InlineTransfersCoordinator;

  struct PreparedViewState {
    ViewId view_id {};
    frame::SequenceNumber frame_sequence { 0U };
    frame::Slot frame_slot { frame::kInvalidSlot };
    observer_ptr<scene::Scene> active_scene { nullptr };
    engine::ViewConstants::GpuData view_constants_snapshot {};
    float camera_viewport_width { 0.0F };
    std::vector<engine::DirectionalShadowCandidate>
      directional_shadow_candidates {};
    std::vector<engine::PositionalLightData> positional_lights {};
    std::vector<engine::PositionalShadowCandidate>
      positional_shadow_candidates {};
    std::vector<VsmScenePrimitiveHistoryRecord> scene_primitive_history {};
    std::vector<glm::vec4> shadow_caster_bounds {};
    std::vector<glm::vec4> visible_receiver_bounds {};
    std::chrono::milliseconds gpu_budget { 16 };
    std::uint64_t shadow_caster_content_hash { 0U };
    bool has_virtual_shadow_work { false };
  };

  OXGN_RNDR_API VsmShadowRenderer(observer_ptr<Graphics> gfx,
    observer_ptr<ProviderT> staging_provider,
    observer_ptr<CoordinatorT> inline_transfers,
    oxygen::ShadowQualityTier quality_tier);
  OXGN_RNDR_API ~VsmShadowRenderer();

  OXYGEN_MAKE_NON_COPYABLE(VsmShadowRenderer)
  OXYGEN_MAKE_NON_MOVABLE(VsmShadowRenderer)

  OXGN_RNDR_API auto OnFrameStart(
    RendererTag tag, frame::SequenceNumber sequence, frame::Slot slot) -> void;
  OXGN_RNDR_API auto ResetCachedState() -> void;

  OXGN_RNDR_API auto PrepareView(ViewId view_id,
    const engine::ViewConstants& view_constants, const LightManager& lights,
    observer_ptr<scene::Scene> active_scene, float camera_viewport_width,
    std::span<const engine::sceneprep::RenderItemData> rendered_items = {},
    std::span<const glm::vec4> shadow_caster_bounds = {},
    std::span<const glm::vec4> visible_receiver_bounds = {},
    std::chrono::milliseconds gpu_budget = std::chrono::milliseconds(16),
    std::uint64_t shadow_caster_content_hash = 0U) -> bool;
  OXGN_RNDR_API auto ExecutePreparedViewShell(
    const engine::RenderContext& render_context,
    graphics::CommandRecorder& recorder,
    observer_ptr<const graphics::Texture> scene_depth_texture) -> co::Co<>;
  OXGN_RNDR_API auto ExecutePageRequestReadbackBridge(
    const engine::RenderContext& render_context,
    const VsmCacheManagerSeam& seam,
    std::span<const VsmPageRequestProjection> current_projection_records,
    std::shared_ptr<const graphics::Buffer> physical_page_meta_seed_buffer = {})
    -> co::Co<bool>;

  [[nodiscard]] OXGN_RNDR_NDAPI auto TryGetPreparedViewState(
    ViewId view_id) const noexcept -> const PreparedViewState*;

  [[nodiscard]] OXGN_RNDR_NDAPI auto GetCacheManager() noexcept
    -> VsmCacheManager&;
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetPhysicalPagePoolManager() noexcept
    -> VsmPhysicalPagePoolManager&;
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetVirtualAddressSpace() noexcept
    -> VsmVirtualAddressSpace&;
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetSceneInvalidationCoordinator() noexcept
    -> VsmSceneInvalidationCoordinator&;

  [[nodiscard]] OXGN_RNDR_NDAPI auto GetPageRequestGeneratorPass() noexcept
    -> observer_ptr<engine::VsmPageRequestGeneratorPass>;
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetInvalidationPass() noexcept
    -> observer_ptr<engine::VsmInvalidationPass>;
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetPageManagementPass() noexcept
    -> observer_ptr<engine::VsmPageManagementPass>;
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetPageFlagPropagationPass() noexcept
    -> observer_ptr<engine::VsmPageFlagPropagationPass>;
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetPageInitializationPass() noexcept
    -> observer_ptr<engine::VsmPageInitializationPass>;
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetShadowRasterizerPass() noexcept
    -> observer_ptr<engine::VsmShadowRasterizerPass>;
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetStaticDynamicMergePass() noexcept
    -> observer_ptr<engine::VsmStaticDynamicMergePass>;
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetHzbUpdaterPass() noexcept
    -> observer_ptr<engine::VsmHzbUpdaterPass>;
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetProjectionPass() noexcept
    -> observer_ptr<engine::VsmProjectionPass>;

private:
  [[nodiscard]] auto BuildCurrentVirtualFrame(
    const PreparedViewState& prepared_view)
    -> const VsmVirtualAddressSpaceFrame&;
  [[nodiscard]] auto BuildCurrentSeam(
    const VsmVirtualAddressSpaceFrame& current_frame) -> VsmCacheManagerSeam;
  [[nodiscard]] auto BuildSceneLightRemapBindings(
    const PreparedViewState& prepared_view,
    const VsmVirtualAddressSpaceFrame& current_frame) const
    -> std::vector<VsmSceneLightRemapBinding>;

  observer_ptr<Graphics> gfx_ { nullptr };
  observer_ptr<ProviderT> staging_provider_ { nullptr };
  observer_ptr<CoordinatorT> inline_transfers_ { nullptr };
  oxygen::ShadowQualityTier quality_tier_ { oxygen::ShadowQualityTier::kHigh };
  frame::SequenceNumber current_frame_sequence_ { 0 };
  frame::Slot current_frame_slot_ { frame::kInvalidSlot };

  VsmPhysicalPagePoolManager physical_page_pool_manager_;
  VsmVirtualAddressSpace virtual_address_space_ {};
  VsmCacheManager cache_manager_;
  VsmSceneInvalidationCoordinator invalidation_coordinator_ {};

  std::shared_ptr<engine::VsmPageRequestGeneratorPassConfig>
    page_request_generator_config_;
  std::shared_ptr<engine::VsmInvalidationPassConfig> invalidation_pass_config_;
  std::shared_ptr<engine::VsmPageManagementPassConfig>
    page_management_pass_config_;
  std::shared_ptr<engine::VsmPageFlagPropagationPassConfig>
    page_flag_propagation_pass_config_;
  std::shared_ptr<engine::VsmPageInitializationPassConfig>
    page_initialization_pass_config_;
  std::shared_ptr<engine::VsmShadowRasterizerPassConfig>
    shadow_rasterizer_pass_config_;
  std::shared_ptr<engine::VsmStaticDynamicMergePassConfig>
    static_dynamic_merge_pass_config_;
  std::shared_ptr<engine::VsmHzbUpdaterPassConfig> hzb_updater_pass_config_;
  std::shared_ptr<engine::VsmProjectionPassConfig> projection_pass_config_;

  std::shared_ptr<engine::VsmPageRequestGeneratorPass>
    page_request_generator_pass_;
  std::shared_ptr<engine::VsmInvalidationPass> invalidation_pass_;
  std::shared_ptr<engine::VsmPageManagementPass> page_management_pass_;
  std::shared_ptr<engine::VsmPageFlagPropagationPass>
    page_flag_propagation_pass_;
  std::shared_ptr<engine::VsmPageInitializationPass> page_initialization_pass_;
  std::shared_ptr<engine::VsmShadowRasterizerPass> shadow_rasterizer_pass_;
  std::shared_ptr<engine::VsmStaticDynamicMergePass> static_dynamic_merge_pass_;
  std::shared_ptr<engine::VsmHzbUpdaterPass> hzb_updater_pass_;
  std::shared_ptr<engine::VsmProjectionPass> projection_pass_;

  std::unordered_map<ViewId, PreparedViewState> prepared_views_ {};
};

} // namespace oxygen::renderer::vsm
