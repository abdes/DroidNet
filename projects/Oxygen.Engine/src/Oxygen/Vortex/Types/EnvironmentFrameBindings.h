//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentAmbientBridgeBindings.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentEvaluationParameters.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentProbeBindings.h>

namespace oxygen::vortex {

inline constexpr std::uint32_t kEnvironmentContractFlagAtmosphereLight0Enabled
  = 1U << 0U;
inline constexpr std::uint32_t kEnvironmentContractFlagAtmosphereLight1Enabled
  = 1U << 1U;
inline constexpr std::uint32_t kEnvironmentContractFlagShadowAuthoritySlot0Only
  = 1U << 2U;

//! Bindless environment-system routing payload for a single view.
struct alignas(packing::kShaderDataFieldAlignment) EnvironmentFrameBindings {
  ShaderVisibleIndex environment_static_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex environment_view_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex atmosphere_model_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex height_fog_model_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex sky_light_model_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex volumetric_fog_model_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex environment_view_products_slot {
    kInvalidShaderVisibleIndex
  };
  std::uint32_t contract_flags { 0U };
  ShaderVisibleIndex transmittance_lut_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex multi_scattering_lut_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex sky_view_lut_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex camera_aerial_perspective_srv {
    kInvalidShaderVisibleIndex
  };
  float atmosphere_light0_direction_angular_size[4] { 0.0F, 0.0F, 1.0F, 0.0F };
  float atmosphere_light0_disk_luminance_rgb[4] { 0.0F, 0.0F, 0.0F, 0.0F };
  float atmosphere_light1_direction_angular_size[4] { 0.0F, 0.0F, 1.0F, 0.0F };
  float atmosphere_light1_disk_luminance_rgb[4] { 0.0F, 0.0F, 0.0F, 0.0F };
  EnvironmentProbeBindings probes {};
  EnvironmentEvaluationParameters evaluation {};
  EnvironmentAmbientBridgeBindings ambient_bridge {};
};

static_assert(sizeof(EnvironmentFrameBindings) == 176);
static_assert(
  alignof(EnvironmentFrameBindings) == packing::kShaderDataFieldAlignment);
static_assert(sizeof(EnvironmentFrameBindings) % 16 == 0);

} // namespace oxygen::vortex
