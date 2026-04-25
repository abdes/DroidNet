//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentProbeState.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentViewProducts.h>
#include <Oxygen/Vortex/Types/EnvironmentFrameBindings.h>
#include <Oxygen/Vortex/Types/EnvironmentStaticData.h>
#include <Oxygen/Vortex/Types/EnvironmentViewData.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::scene {
class Scene;
}

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
class SceneTextures;

namespace internal {
  template <typename Payload> class PerViewStructuredPublisher;
}

namespace environment {
  class SkyRenderer;
  class AtmosphereRenderer;
  class FogRenderer;
  class LocalFogVolumeTiledCullingPass;
  class LocalFogVolumeComposePass;
  class AtmosphereTransmittanceLutPass;
  class AtmosphereMultiScatteringLutPass;
  class DistantSkyLightLutPass;
  class AtmosphereSkyViewLutPass;
  class AtmosphereCameraAerialPerspectivePass;
  namespace internal {
    struct StableAtmosphereState;
    struct ResolvedAtmosphereLightState;
    class AtmosphereState;
    class AtmosphereLightState;
    class AtmosphereLutCache;
    class IblProcessor;
    class LocalFogVolumeState;
  }
} // namespace environment

class EnvironmentLightingService {
public:
  struct ProbeRefreshState {
    frame::SequenceNumber frame_sequence { 0U };
    frame::Slot frame_slot { frame::kInvalidSlot };
    bool requested { false };
    bool refreshed { false };
    bool valid { false };
    std::uint32_t probe_revision { 0U };
  };

  struct PublicationState {
    frame::SequenceNumber frame_sequence { 0U };
    frame::Slot frame_slot { frame::kInvalidSlot };
    std::uint32_t published_view_count { 0U };
    std::uint32_t published_environment_view_count { 0U };
    std::uint32_t published_environment_view_products_count { 0U };
    std::uint32_t ambient_bridge_view_count { 0U };
    std::uint32_t probe_revision { 0U };
    bool sky_light_authored_enabled { false };
    bool sky_light_ibl_valid { false };
    bool sky_light_ibl_unavailable { false };
    bool sky_light_ibl_stale { false };
    bool volumetric_fog_authored_enabled { false };
    bool integrated_light_scattering_valid { false };
    bool integrated_light_scattering_unavailable { false };
  };

  struct ViewProductGenerationState {
    ViewId view_id { kInvalidViewId };
    bool environment_view_published { false };
    ShaderVisibleIndex environment_view_slot { kInvalidShaderVisibleIndex };
    bool atmosphere_lut_cache_valid { false };
    std::uint32_t atmosphere_lut_cache_revision { 0U };
    std::uint32_t atmosphere_light_count { 0U };
    bool dual_atmosphere_lights_participating { false };
    bool transmittance_lut_requested { false };
    bool transmittance_lut_executed { false };
    ShaderVisibleIndex transmittance_lut_srv { kInvalidShaderVisibleIndex };
    std::uint32_t transmittance_width { 0U };
    std::uint32_t transmittance_height { 0U };
    std::uint32_t transmittance_dispatch_count_x { 0U };
    std::uint32_t transmittance_dispatch_count_y { 0U };
    std::uint32_t transmittance_dispatch_count_z { 0U };
    bool multi_scattering_lut_requested { false };
    bool multi_scattering_lut_executed { false };
    ShaderVisibleIndex multi_scattering_lut_srv { kInvalidShaderVisibleIndex };
    std::uint32_t multi_scattering_width { 0U };
    std::uint32_t multi_scattering_height { 0U };
    std::uint32_t multi_scattering_dispatch_count_x { 0U };
    std::uint32_t multi_scattering_dispatch_count_y { 0U };
    std::uint32_t multi_scattering_dispatch_count_z { 0U };
    bool distant_sky_light_lut_requested { false };
    bool distant_sky_light_lut_executed { false };
    ShaderVisibleIndex distant_sky_light_lut_srv { kInvalidShaderVisibleIndex };
    std::uint32_t distant_sky_light_dispatch_count_x { 0U };
    std::uint32_t distant_sky_light_dispatch_count_y { 0U };
    std::uint32_t distant_sky_light_dispatch_count_z { 0U };
    bool sky_view_lut_requested { false };
    bool sky_view_lut_executed { false };
    ShaderVisibleIndex sky_view_lut_srv { kInvalidShaderVisibleIndex };
    std::uint32_t sky_view_width { 0U };
    std::uint32_t sky_view_height { 0U };
    std::uint32_t sky_view_dispatch_count_x { 0U };
    std::uint32_t sky_view_dispatch_count_y { 0U };
    std::uint32_t sky_view_dispatch_count_z { 0U };
    bool camera_aerial_perspective_requested { false };
    bool camera_aerial_perspective_executed { false };
    ShaderVisibleIndex camera_aerial_perspective_srv {
      kInvalidShaderVisibleIndex
    };
    std::uint32_t camera_aerial_width { 0U };
    std::uint32_t camera_aerial_height { 0U };
    std::uint32_t camera_aerial_depth { 0U };
    float camera_aerial_sample_count_per_slice { 0.0F };
    std::uint32_t camera_aerial_dispatch_count_x { 0U };
    std::uint32_t camera_aerial_dispatch_count_y { 0U };
    std::uint32_t camera_aerial_dispatch_count_z { 0U };
    bool environment_view_products_published { false };
    ShaderVisibleIndex environment_view_products_slot {
      kInvalidShaderVisibleIndex
    };
    bool sky_light_authored_enabled { false };
    bool sky_light_ibl_valid { false };
    bool sky_light_ibl_unavailable { false };
    bool volumetric_fog_authored_enabled { false };
    bool integrated_light_scattering_valid { false };
    bool integrated_light_scattering_unavailable { false };
  };

  struct Stage15State {
    ViewId view_id { kInvalidViewId };
    bool requested { false };
    bool sky_requested { false };
    bool sky_executed { false };
    std::uint32_t sky_draw_count { 0U };
    bool atmosphere_requested { false };
    bool atmosphere_executed { false };
    std::uint32_t atmosphere_draw_count { 0U };
    bool fog_requested { false };
    bool fog_executed { false };
    std::uint32_t fog_draw_count { 0U };
    bool local_fog_requested { false };
    bool local_fog_executed { false };
    std::uint32_t local_fog_draw_count { 0U };
    std::uint32_t total_draw_count { 0U };
  };

  struct Stage14State {
    ViewId view_id { kInvalidViewId };
    bool requested { false };
    bool local_fog_requested { false };
    bool local_fog_executed { false };
    bool local_fog_hzb_consumed { false };
    bool local_fog_hzb_unavailable { false };
    bool local_fog_buffer_ready { false };
    bool local_fog_skipped { false };
    std::uint32_t local_fog_instance_count { 0U };
    std::uint32_t local_fog_dispatch_count_x { 0U };
    std::uint32_t local_fog_dispatch_count_y { 0U };
    std::uint32_t local_fog_dispatch_count_z { 0U };
  };

  OXGN_VRTX_API explicit EnvironmentLightingService(Renderer& renderer);
  OXGN_VRTX_API ~EnvironmentLightingService();

  EnvironmentLightingService(const EnvironmentLightingService&) = delete;
  auto operator=(const EnvironmentLightingService&)
    -> EnvironmentLightingService& = delete;
  EnvironmentLightingService(EnvironmentLightingService&&) = delete;
  auto operator=(EnvironmentLightingService&&)
    -> EnvironmentLightingService& = delete;

  OXGN_VRTX_API auto OnFrameStart(
    frame::SequenceNumber sequence, frame::Slot slot) -> void;
  OXGN_VRTX_API auto RefreshPersistentProbeState(
    bool environment_source_changed) -> void;
  [[nodiscard]] OXGN_VRTX_API auto BuildBindings(
    ShaderVisibleIndex environment_static_slot,
    ShaderVisibleIndex environment_view_slot,
    ShaderVisibleIndex environment_view_products_slot,
    const environment::EnvironmentViewProducts& view_products,
    bool enable_ambient_bridge) const -> EnvironmentFrameBindings;
  OXGN_VRTX_API auto PublishEnvironmentBindings(RenderContext& ctx,
    ShaderVisibleIndex environment_static_slot = kInvalidShaderVisibleIndex,
    ShaderVisibleIndex environment_view_slot = kInvalidShaderVisibleIndex,
    bool enable_ambient_bridge = false) -> ShaderVisibleIndex;
  OXGN_VRTX_API auto RenderSkyAndFog(
    RenderContext& ctx, const SceneTextures& scene_textures) -> void;

  [[nodiscard]] OXGN_VRTX_API auto InspectBindings(ViewId view_id) const
    -> const EnvironmentFrameBindings*;
  [[nodiscard]] OXGN_VRTX_API auto InspectEnvironmentViewData(
    ViewId view_id) const -> const EnvironmentViewData*;
  [[nodiscard]] OXGN_VRTX_API auto InspectEnvironmentStaticData(
    ViewId view_id) const -> const EnvironmentStaticData*;
  [[nodiscard]] OXGN_VRTX_API auto InspectEnvironmentViewProducts(
    ViewId view_id) const -> const environment::EnvironmentViewProducts*;
  [[nodiscard]] OXGN_VRTX_API auto ResolveEnvironmentFrameSlot(
    ViewId view_id) const -> ShaderVisibleIndex;
  [[nodiscard]] OXGN_VRTX_NDAPI auto InspectProbeState() const noexcept
    -> const EnvironmentProbeState&
  {
    return probe_state_;
  }
  [[nodiscard]] OXGN_VRTX_NDAPI auto GetLastProbeRefreshState() const noexcept
    -> const ProbeRefreshState&
  {
    return last_probe_refresh_state_;
  }
  [[nodiscard]] OXGN_VRTX_NDAPI auto GetLastPublicationState() const noexcept
    -> const PublicationState&
  {
    return last_publication_state_;
  }
  [[nodiscard]] OXGN_VRTX_NDAPI auto
  GetLastViewProductGenerationState() const noexcept
    -> const ViewProductGenerationState&
  {
    return last_view_product_generation_state_;
  }
  [[nodiscard]] OXGN_VRTX_NDAPI auto GetLastStage15State() const noexcept
    -> const Stage15State&
  {
    return last_stage15_state_;
  }
  [[nodiscard]] OXGN_VRTX_NDAPI auto GetLastStage14State() const noexcept
    -> const Stage14State&
  {
    return last_stage14_state_;
  }
  [[nodiscard]] OXGN_VRTX_API auto InspectAtmosphereState() const noexcept
    -> const environment::internal::StableAtmosphereState&;
  [[nodiscard]] OXGN_VRTX_API auto InspectAtmosphereLightState() const noexcept
    -> const environment::internal::ResolvedAtmosphereLightState&;

private:
  struct PublishedView {
    ShaderVisibleIndex slot { kInvalidShaderVisibleIndex };
    EnvironmentFrameBindings bindings {};
    EnvironmentStaticData static_data {};
    EnvironmentViewData view_data {};
    environment::EnvironmentViewProducts view_products {};
  };

  auto EnsurePublishResources() -> bool;
  [[nodiscard]] auto BuildEnvironmentStaticData(
    const environment::EnvironmentViewProducts& view_products) const
    -> EnvironmentStaticData;
  [[nodiscard]] auto BuildEnvironmentViewData(const RenderContext& ctx) const
    -> EnvironmentViewData;
  auto RefreshStableAtmosphereState(const scene::Scene* scene) -> void;

  Renderer& renderer_;
  frame::SequenceNumber current_sequence_ { 0U };
  frame::Slot current_slot_ { frame::kInvalidSlot };
  EnvironmentProbeState probe_state_ {};
  ProbeRefreshState last_probe_refresh_state_ {};
  PublicationState last_publication_state_ {};
  ViewProductGenerationState last_view_product_generation_state_ {};
  Stage14State last_stage14_state_ {};
  Stage15State last_stage15_state_ {};
  std::unique_ptr<
    internal::PerViewStructuredPublisher<EnvironmentFrameBindings>>
    bindings_publisher_ {};
  std::unique_ptr<internal::PerViewStructuredPublisher<EnvironmentStaticData>>
    static_data_publisher_ {};
  std::unique_ptr<internal::PerViewStructuredPublisher<EnvironmentViewData>>
    view_data_publisher_ {};
  std::unique_ptr<
    internal::PerViewStructuredPublisher<environment::EnvironmentViewProducts>>
    view_products_publisher_ {};
  std::unordered_map<ViewId, PublishedView> published_views_ {};
  std::unique_ptr<environment::SkyRenderer> sky_ {};
  std::unique_ptr<environment::AtmosphereRenderer> atmosphere_ {};
  std::unique_ptr<environment::FogRenderer> fog_ {};
  std::unique_ptr<environment::internal::AtmosphereLightState>
    atmosphere_light_state_ {};
  std::unique_ptr<environment::internal::AtmosphereState> atmosphere_state_ {};
  std::unique_ptr<environment::internal::LocalFogVolumeState>
    local_fog_state_ {};
  std::unique_ptr<environment::LocalFogVolumeTiledCullingPass>
    local_fog_tiled_culling_ {};
  std::unique_ptr<environment::LocalFogVolumeComposePass> local_fog_compose_ {};
  std::unique_ptr<environment::internal::AtmosphereLutCache>
    atmosphere_lut_cache_ {};
  std::unique_ptr<environment::AtmosphereTransmittanceLutPass>
    transmittance_lut_pass_ {};
  std::unique_ptr<environment::AtmosphereMultiScatteringLutPass>
    multi_scattering_lut_pass_ {};
  std::unique_ptr<environment::DistantSkyLightLutPass>
    distant_sky_light_lut_pass_ {};
  std::unique_ptr<environment::AtmosphereSkyViewLutPass> sky_view_lut_pass_ {};
  std::unique_ptr<environment::AtmosphereCameraAerialPerspectivePass>
    camera_aerial_perspective_pass_ {};
  std::unique_ptr<environment::internal::IblProcessor> ibl_ {};
};

} // namespace oxygen::vortex
