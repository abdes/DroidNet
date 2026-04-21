//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
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
  EnvironmentProbeBindings probes {};
  EnvironmentEvaluationParameters evaluation {};
  EnvironmentAmbientBridgeBindings ambient_bridge {};
};

static_assert(sizeof(EnvironmentFrameBindings) == 112);
static_assert(
  alignof(EnvironmentFrameBindings) == packing::kShaderDataFieldAlignment);
static_assert(sizeof(EnvironmentFrameBindings) % 16 == 0);
static_assert(offsetof(EnvironmentFrameBindings, environment_static_slot) == 0);
static_assert(offsetof(EnvironmentFrameBindings, environment_view_slot) == 4);
static_assert(offsetof(EnvironmentFrameBindings, atmosphere_model_slot) == 8);
static_assert(offsetof(EnvironmentFrameBindings, height_fog_model_slot) == 12);
static_assert(offsetof(EnvironmentFrameBindings, sky_light_model_slot) == 16);
static_assert(offsetof(EnvironmentFrameBindings, volumetric_fog_model_slot) == 20);
static_assert(offsetof(EnvironmentFrameBindings, environment_view_products_slot) == 24);
static_assert(offsetof(EnvironmentFrameBindings, contract_flags) == 28);
static_assert(offsetof(EnvironmentFrameBindings, transmittance_lut_srv) == 32);
static_assert(offsetof(EnvironmentFrameBindings, multi_scattering_lut_srv) == 36);
static_assert(offsetof(EnvironmentFrameBindings, sky_view_lut_srv) == 40);
static_assert(offsetof(EnvironmentFrameBindings, camera_aerial_perspective_srv) == 44);
static_assert(offsetof(EnvironmentFrameBindings, probes) == 48);
static_assert(offsetof(EnvironmentFrameBindings, evaluation) == 68);
static_assert(offsetof(EnvironmentFrameBindings, ambient_bridge) == 84);

} // namespace oxygen::vortex
