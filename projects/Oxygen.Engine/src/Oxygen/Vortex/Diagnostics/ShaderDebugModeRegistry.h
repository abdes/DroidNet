//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <span>
#include <string_view>

#include <Oxygen/Vortex/Diagnostics/DiagnosticsTypes.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

enum class ShaderDebugModeFamily : std::uint8_t {
  kNone,
  kLightCulling,
  kIbl,
  kMaterial,
  kDirectLighting,
  kShadow,
  kSceneDepth,
  kMasked,
};

[[nodiscard]] constexpr auto to_string(
  const ShaderDebugModeFamily family) noexcept -> std::string_view
{
  switch (family) {
  case ShaderDebugModeFamily::kNone:
    return "none";
  case ShaderDebugModeFamily::kLightCulling:
    return "light-culling";
  case ShaderDebugModeFamily::kIbl:
    return "ibl";
  case ShaderDebugModeFamily::kMaterial:
    return "material";
  case ShaderDebugModeFamily::kDirectLighting:
    return "direct-lighting";
  case ShaderDebugModeFamily::kShadow:
    return "shadow";
  case ShaderDebugModeFamily::kSceneDepth:
    return "scene-depth";
  case ShaderDebugModeFamily::kMasked:
    return "masked";
  }
  return "__unknown__";
}

struct ShaderDebugModeInfo {
  ShaderDebugMode mode { ShaderDebugMode::kDisabled };
  std::string_view canonical_name;
  std::string_view display_name;
  ShaderDebugModeFamily family { ShaderDebugModeFamily::kNone };
  std::string_view shader_define;
  DiagnosticsDebugPath path { DiagnosticsDebugPath::kNone };
  CapabilitySet required_capabilities { RendererCapabilityFamily::kNone };
  bool requires_scene_color { false };
  bool requires_scene_depth { false };
  bool requires_gbuffer { false };
  bool requires_lighting_products { false };
  bool requires_shadow_products { false };
  bool supported { true };
  std::string_view unsupported_reason;
};

[[nodiscard]] OXGN_VRTX_API auto EnumerateShaderDebugModes() noexcept
  -> std::span<const ShaderDebugModeInfo>;

[[nodiscard]] OXGN_VRTX_API auto FindShaderDebugModeInfo(
  ShaderDebugMode mode) noexcept -> const ShaderDebugModeInfo*;

[[nodiscard]] OXGN_VRTX_API auto FindShaderDebugModeInfo(
  std::string_view canonical_name) noexcept -> const ShaderDebugModeInfo*;

[[nodiscard]] OXGN_VRTX_API auto ResolveShaderDebugMode(
  std::string_view canonical_name) noexcept -> std::optional<ShaderDebugMode>;

} // namespace oxygen::vortex
