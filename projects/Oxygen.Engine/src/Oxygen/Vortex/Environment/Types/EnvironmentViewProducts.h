//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>

#include <Oxygen/Core/Bindless/Types.h>

#include <Oxygen/Vortex/Environment/Types/AtmosphereModel.h>
#include <Oxygen/Vortex/Environment/Types/AtmosphereLightModel.h>
#include <Oxygen/Vortex/Environment/Types/HeightFogModel.h>
#include <Oxygen/Vortex/Environment/Types/SkyLightEnvironmentModel.h>
#include <Oxygen/Vortex/Environment/Types/VolumetricFogModel.h>

namespace oxygen::vortex::environment {

inline constexpr std::uint32_t kEnvironmentViewProductFlagSkyLightAuthoredEnabled
  = 1U << 0U;
inline constexpr std::uint32_t kEnvironmentViewProductFlagSkyLightIblValid
  = 1U << 1U;
inline constexpr std::uint32_t kEnvironmentViewProductFlagSkyLightIblUnavailable
  = 1U << 2U;
inline constexpr std::uint32_t
  kEnvironmentViewProductFlagVolumetricFogAuthoredEnabled
  = 1U << 3U;
inline constexpr std::uint32_t
  kEnvironmentViewProductFlagIntegratedLightScatteringValid
  = 1U << 4U;
inline constexpr std::uint32_t
  kEnvironmentViewProductFlagIntegratedLightScatteringUnavailable
  = 1U << 5U;

struct EnvironmentViewProducts {
  AtmosphereModel atmosphere {};
  AtmosphereLightSlots atmosphere_lights {};
  HeightFogModel height_fog {};
  SkyLightEnvironmentModel sky_light {};
  VolumetricFogModel volumetric_fog {};
  std::uint32_t atmosphere_light_count { 0U };
  std::uint32_t conventional_shadow_authority_slot {
    kInvalidAtmosphereLightSlot
  };
  std::uint32_t flags { 0U };
  std::uint32_t reserved0 { 0U };

  ShaderVisibleIndex transmittance_lut_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex multi_scattering_lut_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex sky_view_lut_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex camera_aerial_perspective_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex distant_sky_light_lut_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex integrated_light_scattering_srv {
    kInvalidShaderVisibleIndex
  };
};

} // namespace oxygen::vortex::environment
