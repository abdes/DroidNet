//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Vortex/Environment/Internal/LocalFogVolumeState.h>
#include <Oxygen/Vortex/Environment/Passes/Generated.FogContracts.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class Texture;
}

namespace oxygen {
class ResolvedView;
}

namespace oxygen::graphics {
class CommandRecorder;
}

namespace oxygen::vortex {
namespace internal {
  class RetainedTexturePool;
}
namespace testing {
  struct RendererPublicationProbe;
}
namespace postprocess {
  struct FrameExposureResources;
}

struct RenderContext;
class Renderer;

namespace environment::internal {
  struct StableAtmosphereState;
}

namespace environment {

  class VolumetricFogPass {
  public:
    [[nodiscard]] OXGN_VRTX_API static auto GridExtent(const ResolvedView& view)
      -> glm::uvec3;
    struct RecordState {
      bool requested { false };
      bool executed { false };
      //! Submitted radiance resource retained for this view's consumers.
      std::shared_ptr<const graphics::Texture> texture;
      ShaderVisibleIndex integrated_light_scattering_srv {
        kInvalidShaderVisibleIndex
      };
      ShaderVisibleIndex integrated_light_scattering_uav {
        kInvalidShaderVisibleIndex
      };
      std::uint32_t width { 0U };
      std::uint32_t height { 0U };
      std::uint32_t depth { 0U };
      std::uint32_t dispatch_count_x { 0U };
      std::uint32_t dispatch_count_y { 0U };
      std::uint32_t dispatch_count_z { 0U };
      float start_distance_m { 0.0F };
      float end_distance_m { 0.0F };
      bool view_constants_bound { false };
      bool ue_log_depth_distribution { false };
      bool directional_shadowed_light_injection_requested { false };
      bool height_fog_media_requested { false };
      bool height_fog_media_executed { false };
      bool sky_light_injection_requested { false };
      bool sky_light_injection_executed { false };
      bool temporal_history_requested { false };
      bool temporal_history_reprojection_executed { false };
      bool temporal_history_reset { false };
      bool local_fog_injection_requested { false };
      bool local_fog_injection_executed { false };
      std::uint32_t local_fog_instance_count { 0U };
      std::array<float, 3> grid_z_params { 0.0F, 1.0F, 1.0F };
    };

    OXGN_VRTX_API explicit VolumetricFogPass(Renderer& renderer);
    OXGN_VRTX_API ~VolumetricFogPass();

    VolumetricFogPass(const VolumetricFogPass&) = delete;
    auto operator=(const VolumetricFogPass&) -> VolumetricFogPass& = delete;
    VolumetricFogPass(VolumetricFogPass&&) = delete;
    auto operator=(VolumetricFogPass&&) -> VolumetricFogPass& = delete;

    OXGN_VRTX_API auto OnFrameStart(
      frame::SequenceNumber sequence, frame::Slot slot) -> void;
    OXGN_VRTX_API auto RemoveViewState(ViewId view_id) -> void;
    [[nodiscard]] OXGN_VRTX_API auto Record(RenderContext& ctx,
      graphics::CommandRecorder& recorder,
      const internal::StableAtmosphereState& stable_state,
      ShaderVisibleIndex distant_sky_light_lut_srv,
      const internal::LocalFogVolumeState::ViewProducts* local_fog_products
      = nullptr) -> RecordState;

  private:
    friend struct ::oxygen::vortex::testing::RendererPublicationProbe;
    auto ClearHistory(ViewId view_id) -> void;
    using PassConstants = VolumetricFogPassConstants;

    Renderer& renderer_;
    upload::TransientStructuredBuffer pass_constants_buffer_;
    //! The integrated product consumers sample.
    std::unique_ptr<::oxygen::vortex::internal::RetainedTexturePool>
      output_pool_;
    //! Per-froxel light scattering (FP32), also the temporal history.
    std::unique_ptr<::oxygen::vortex::internal::RetainedTexturePool>
      scattering_pool_;
    std::vector<std::shared_ptr<graphics::Texture>> live_textures_;

    struct HistoryEntry {
      std::shared_ptr<graphics::Texture> texture;
      std::shared_ptr<const postprocess::FrameExposureResources> frame_exposure;
      ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
      std::uint32_t width { 0U };
      std::uint32_t height { 0U };
      std::uint32_t depth { 0U };
      float start_distance_m { 0.0F };
      float end_distance_m { 0.0F };
      std::array<float, 3> grid_z_params { 0.0F, 1.0F, 1.0F };
      bool valid { false };
    };
    std::unordered_map<ViewId, HistoryEntry> history_by_view_;
    std::optional<frame::SequenceNumber> exposure_frame_;
    std::array<
      std::vector<std::shared_ptr<const postprocess::FrameExposureResources>>,
      frame::kFramesInFlight.get()>
      exposure_readers_;
  };

} // namespace environment
} // namespace oxygen::vortex
