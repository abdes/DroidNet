//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <unordered_set>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Types/EnvironmentFrameBindings.h>
#include <Oxygen/Renderer/Types/EnvironmentViewData.h>
#include <Oxygen/Renderer/Types/SyntheticSunData.h>
#include <Oxygen/Renderer/Types/ViewFrameBindings.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen {
class Graphics;
}

namespace oxygen::graphics {
class CommandRecorder;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::scene {
class Scene;
}

namespace oxygen::renderer::resources {
class TextureBinder;
}

namespace oxygen::engine {

class IblComputePass;
struct RenderContext;
class SkyCapturePass;
struct SkyCapturePassConfig;
class SkyAtmosphereLutComputePass;
struct SkyAtmosphereLutComputePassConfig;

namespace internal {
template <typename Payload> class PerViewStructuredPublisher;
class BrdfLutManager;
class EnvironmentStaticDataManager;
class IblManager;
class SkyAtmosphereLutManager;
} // namespace internal

namespace upload {
class InlineTransfersCoordinator;
class StagingProvider;
class UploadCoordinator;
} // namespace upload

class EnvironmentLightingService {
public:
  OXGN_RNDR_API EnvironmentLightingService(observer_ptr<Graphics> gfx,
    const RendererConfig& config,
    observer_ptr<upload::UploadCoordinator> uploader,
    observer_ptr<upload::StagingProvider> upload_staging_provider,
    observer_ptr<upload::InlineTransfersCoordinator> inline_transfers,
    observer_ptr<upload::StagingProvider> inline_staging_provider,
    observer_ptr<renderer::resources::TextureBinder> texture_binder) noexcept;
  OXGN_RNDR_API ~EnvironmentLightingService();

  OXYGEN_MAKE_NON_COPYABLE(EnvironmentLightingService)
  OXYGEN_DEFAULT_MOVABLE(EnvironmentLightingService)

  OXGN_RNDR_API auto Initialize() -> void;
  OXGN_RNDR_API auto OnFrameStart(
    frame::SequenceNumber frame_sequence, frame::Slot frame_slot) -> void;
  OXGN_RNDR_API auto UpdatePreparedViewState(ViewId view_id,
    const scene::Scene& scene, const ResolvedView& view, bool allow_atmosphere,
    const SyntheticSunData& sun, EnvironmentViewData& environment_view)
    -> void;
  OXGN_RNDR_API auto PrepareCurrentView(
    ViewId view_id, RenderContext& render_context, bool allow_atmosphere)
    -> void;
  OXGN_RNDR_API auto ExecutePerViewPasses(ViewId view_id,
    RenderContext& render_context, graphics::CommandRecorder& recorder,
    bool allow_atmosphere) -> co::Co<>;
  OXGN_RNDR_API auto PublishForView(ViewId view_id,
    ShaderVisibleIndex environment_static_slot,
    const EnvironmentViewData& environment_view,
    bool can_reuse_cached_view_bindings, ViewFrameBindings& view_bindings)
    -> void;
  OXGN_RNDR_API auto NoteViewSeen(
    ViewId view_id, frame::SequenceNumber frame_sequence) -> void;
  OXGN_RNDR_API auto OnFrameComplete() noexcept -> void;
  OXGN_RNDR_API auto EvictViewProducts(ViewId view_id) -> void;
  OXGN_RNDR_API auto EvictInactiveViewProducts(frame::SequenceNumber current_seq,
    const std::unordered_set<ViewId>& active_views) -> void;
  OXGN_RNDR_API auto Shutdown() noexcept -> void;

  [[nodiscard]] OXGN_RNDR_API auto GetSkyAtmosphereLutManagerForView(
    ViewId view_id) const noexcept -> observer_ptr<internal::SkyAtmosphereLutManager>;
  [[nodiscard]] OXGN_RNDR_API auto GetOrCreateSkyAtmosphereLutManagerForView(
    ViewId view_id) -> observer_ptr<internal::SkyAtmosphereLutManager>;
  [[nodiscard]] OXGN_RNDR_API auto GetEnvironmentStaticDataManager() const noexcept
    -> observer_ptr<internal::EnvironmentStaticDataManager>;
  [[nodiscard]] OXGN_RNDR_API auto GetIblManager() const noexcept
    -> observer_ptr<internal::IblManager>;
  [[nodiscard]] OXGN_RNDR_API auto GetIblComputePass() const noexcept
    -> observer_ptr<IblComputePass>;
  [[nodiscard]] OXGN_RNDR_API auto GetEnvironmentStaticSlot(
    ViewId view_id) const noexcept -> ShaderVisibleIndex;
  OXGN_RNDR_API auto RequestIblRegeneration() noexcept -> void;
  OXGN_RNDR_API auto RequestSkyCapture(std::span<const ViewId> known_view_ids) noexcept
    -> void;
  OXGN_RNDR_API auto SetAtmosphereBlueNoiseEnabled(bool enabled) noexcept
    -> void;

private:
  observer_ptr<Graphics> gfx_ { nullptr };
  observer_ptr<upload::UploadCoordinator> uploader_ { nullptr };
  observer_ptr<upload::StagingProvider> upload_staging_provider_ { nullptr };
  observer_ptr<upload::InlineTransfersCoordinator> inline_transfers_ { nullptr };
  observer_ptr<upload::StagingProvider> inline_staging_provider_ { nullptr };
  observer_ptr<renderer::resources::TextureBinder> texture_binder_ { nullptr };
  const RendererConfig* config_ { nullptr };

  std::unique_ptr<internal::PerViewStructuredPublisher<EnvironmentViewData>>
    environment_view_data_publisher_;
  std::unique_ptr<
    internal::PerViewStructuredPublisher<EnvironmentFrameBindings>>
    environment_frame_bindings_publisher_;
  std::unique_ptr<internal::BrdfLutManager> brdf_lut_manager_;
  std::unordered_map<ViewId, std::unique_ptr<internal::SkyAtmosphereLutManager>>
    per_view_atmo_luts_;
  std::unique_ptr<internal::IblManager> ibl_manager_;
  std::unique_ptr<SkyCapturePass> sky_capture_pass_;
  std::shared_ptr<SkyCapturePassConfig> sky_capture_pass_config_;
  std::unique_ptr<SkyAtmosphereLutComputePass> sky_atmo_lut_compute_pass_;
  std::shared_ptr<SkyAtmosphereLutComputePassConfig>
    sky_atmo_lut_compute_pass_config_;
  std::unique_ptr<IblComputePass> ibl_compute_pass_;
  std::unique_ptr<internal::EnvironmentStaticDataManager> env_static_manager_;
  uint32_t atmosphere_debug_flags_ { 0U };
  bool atmosphere_blue_noise_enabled_ { true };
  std::unordered_map<ViewId, std::uint64_t> last_atmo_generation_;
  std::unordered_map<ViewId, frame::SequenceNumber> last_seen_view_frame_seq_;
  bool sky_capture_requested_ { false };
};

} // namespace oxygen::engine
