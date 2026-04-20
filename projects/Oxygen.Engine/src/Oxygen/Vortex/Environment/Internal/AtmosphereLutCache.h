//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

#include <glm/vec3.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {
class Renderer;
}

namespace oxygen::vortex::environment::internal {

struct StableAtmosphereState;

class AtmosphereLutCache {
public:
  struct InternalParameters {
    std::uint32_t transmittance_width { 256U };
    std::uint32_t transmittance_height { 64U };
    std::uint32_t multi_scattering_width { 32U };
    std::uint32_t multi_scattering_height { 32U };
    std::uint32_t sky_view_width { 192U };
    std::uint32_t sky_view_height { 104U };
    std::uint32_t camera_aerial_width { 32U };
    std::uint32_t camera_aerial_height { 32U };
    std::uint32_t camera_aerial_depth_resolution { 16U };
    float camera_aerial_depth_km { 96.0F };
    float camera_aerial_depth_slice_length_km { 6.0F };
    float camera_aerial_sample_count_per_slice { 2.0F };
    float transmittance_sample_count { 10.0F };
    float multi_scattering_sample_count { 15.0F };
    float distant_sky_light_sample_altitude_km { 6.0F };
    glm::vec3 sky_luminance_factor_rgb { 1.0F, 1.0F, 1.0F };
    glm::vec3 sky_and_aerial_perspective_luminance_factor_rgb {
      1.0F,
      1.0F,
      1.0F,
    };
    float aerial_perspective_distance_scale { 1.0F };
    float aerial_scattering_strength { 1.0F };
  };

  struct State {
    bool atmosphere_enabled { false };
    bool dual_light_participating { false };
    bool transmittance_lut_valid { false };
    bool multi_scattering_lut_valid { false };
    bool distant_sky_light_lut_valid { false };
    std::uint32_t valid_lut_mask { 0U };
    std::uint64_t atmosphere_revision { 0U };
    std::uint64_t atmosphere_light_revision { 0U };
    std::uint64_t scattering_lut_family_revision { 0U };
    std::uint64_t distant_sky_light_lut_revision { 0U };
    std::uint64_t cache_revision { 0U };
    ShaderVisibleIndex transmittance_lut_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex transmittance_lut_uav { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex multi_scattering_lut_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex multi_scattering_lut_uav { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex distant_sky_light_lut_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex distant_sky_light_lut_uav { kInvalidShaderVisibleIndex };
    InternalParameters internal_parameters {};
  };

  OXGN_VRTX_API explicit AtmosphereLutCache(Renderer& renderer);
  OXGN_VRTX_API ~AtmosphereLutCache();

  AtmosphereLutCache(const AtmosphereLutCache&) = delete;
  auto operator=(const AtmosphereLutCache&) -> AtmosphereLutCache& = delete;
  AtmosphereLutCache(AtmosphereLutCache&&) = delete;
  auto operator=(AtmosphereLutCache&&) -> AtmosphereLutCache& = delete;

  OXGN_VRTX_API auto OnFrameStart(
    frame::SequenceNumber sequence, frame::Slot slot) -> void;
  OXGN_VRTX_API auto RefreshForState(const StableAtmosphereState& stable_state)
    -> void;

  [[nodiscard]] OXGN_VRTX_NDAPI auto GetState() const noexcept -> const State&
  {
    return state_;
  }

  [[nodiscard]] OXGN_VRTX_NDAPI auto NeedsTransmittanceBuild() const noexcept
    -> bool
  {
    return state_.atmosphere_enabled && !state_.transmittance_lut_valid;
  }

  [[nodiscard]] OXGN_VRTX_NDAPI auto NeedsMultiScatteringBuild() const noexcept
    -> bool
  {
    return state_.atmosphere_enabled && state_.transmittance_lut_valid
      && !state_.multi_scattering_lut_valid;
  }

  [[nodiscard]] OXGN_VRTX_NDAPI auto NeedsDistantSkyLightBuild() const noexcept
    -> bool
  {
    return state_.atmosphere_enabled && state_.transmittance_lut_valid
      && state_.multi_scattering_lut_valid
      && !state_.distant_sky_light_lut_valid;
  }

  [[nodiscard]] OXGN_VRTX_NDAPI auto IsFullyValid() const noexcept -> bool
  {
    return state_.atmosphere_enabled && state_.transmittance_lut_valid
      && state_.multi_scattering_lut_valid
      && state_.distant_sky_light_lut_valid;
  }

  [[nodiscard]] OXGN_VRTX_API auto EnsureResources() -> bool;
  OXGN_VRTX_API auto MarkTransmittanceValid() -> void;
  OXGN_VRTX_API auto MarkMultiScatteringValid() -> void;
  OXGN_VRTX_API auto MarkDistantSkyLightValid() -> void;

  [[nodiscard]] OXGN_VRTX_NDAPI auto GetTransmittanceTexture() const noexcept
    -> const std::shared_ptr<graphics::Texture>&
  {
    return transmittance_texture_;
  }

  [[nodiscard]] OXGN_VRTX_NDAPI auto GetMultiScatteringTexture() const noexcept
    -> const std::shared_ptr<graphics::Texture>&
  {
    return multi_scattering_texture_;
  }

  [[nodiscard]] OXGN_VRTX_NDAPI auto GetDistantSkyLightBuffer() const noexcept
    -> const std::shared_ptr<graphics::Buffer>&
  {
    return distant_sky_light_buffer_;
  }

private:
  struct TextureSlots {
    ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex uav { kInvalidShaderVisibleIndex };
  };

  struct BufferSlots {
    ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex uav { kInvalidShaderVisibleIndex };
  };

  auto ResetResources() -> void;
  auto InvalidateScatteringLutFamily() -> void;
  auto InvalidateDistantSkyLightLut() -> void;
  auto EnsureTexture(std::shared_ptr<graphics::Texture>& texture,
    TextureSlots& slots, std::string_view debug_name, std::uint32_t width,
    std::uint32_t height) -> bool;
  auto EnsureStructuredBuffer(std::shared_ptr<graphics::Buffer>& buffer,
    BufferSlots& slots, std::string_view debug_name, std::uint64_t size_bytes,
    std::uint32_t stride) -> bool;
  [[nodiscard]] static auto BuildInternalParameters(
    const StableAtmosphereState& stable_state) -> InternalParameters;
  [[nodiscard]] static auto HashInternalParameters(
    const InternalParameters& params) -> std::uint64_t;

  Renderer& renderer_;
  frame::SequenceNumber current_sequence_ { 0U };
  frame::Slot current_slot_ { frame::kInvalidSlot };
  State state_ {};
  std::shared_ptr<graphics::Texture> transmittance_texture_ {};
  std::shared_ptr<graphics::Texture> multi_scattering_texture_ {};
  std::shared_ptr<graphics::Buffer> distant_sky_light_buffer_ {};
  TextureSlots transmittance_slots_ {};
  TextureSlots multi_scattering_slots_ {};
  BufferSlots distant_sky_light_slots_ {};
};

} // namespace oxygen::vortex::environment::internal
