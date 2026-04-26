//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Diagnostics/ShaderDebugModeRegistry.h>

#include <array>

namespace oxygen::vortex {

namespace {

  constexpr auto kDeferredCapabilities
    = RendererCapabilityFamily::kDeferredShading;
  constexpr auto kLightingCapabilities = RendererCapabilityFamily::kLightingData;
  constexpr auto kShadowCapabilities = RendererCapabilityFamily::kShadowing;

  constexpr auto kShaderDebugModeRegistry = std::array {
    ShaderDebugModeInfo {
      .mode = ShaderDebugMode::kDisabled,
      .canonical_name = "disabled",
      .display_name = "Disabled",
    },
    ShaderDebugModeInfo {
      .mode = ShaderDebugMode::kLightCullingHeatMap,
      .canonical_name = "light-culling-heat-map",
      .display_name = "Light Culling Heat Map",
      .family = ShaderDebugModeFamily::kLightCulling,
      .shader_define = "DEBUG_LIGHT_HEATMAP",
      .path = DiagnosticsDebugPath::kForwardMeshVariant,
      .required_capabilities = kLightingCapabilities,
      .requires_lighting_products = true,
    },
    ShaderDebugModeInfo {
      .mode = ShaderDebugMode::kDepthSlice,
      .canonical_name = "depth-slice",
      .display_name = "Depth Slice",
      .family = ShaderDebugModeFamily::kLightCulling,
      .shader_define = "DEBUG_DEPTH_SLICE",
      .path = DiagnosticsDebugPath::kForwardMeshVariant,
      .required_capabilities = kLightingCapabilities,
      .requires_scene_depth = true,
      .requires_lighting_products = true,
    },
    ShaderDebugModeInfo {
      .mode = ShaderDebugMode::kClusterIndex,
      .canonical_name = "cluster-index",
      .display_name = "Cluster Index",
      .family = ShaderDebugModeFamily::kLightCulling,
      .shader_define = "DEBUG_CLUSTER_INDEX",
      .path = DiagnosticsDebugPath::kForwardMeshVariant,
      .required_capabilities = kLightingCapabilities,
      .requires_scene_depth = true,
      .requires_lighting_products = true,
    },
    ShaderDebugModeInfo {
      .mode = ShaderDebugMode::kIblSpecular,
      .canonical_name = "ibl-specular",
      .display_name = "IBL Specular",
      .family = ShaderDebugModeFamily::kIbl,
      .shader_define = "DEBUG_IBL_SPECULAR",
      .path = DiagnosticsDebugPath::kForwardMeshVariant,
      .requires_lighting_products = true,
    },
    ShaderDebugModeInfo {
      .mode = ShaderDebugMode::kIblRawSky,
      .canonical_name = "ibl-raw-sky",
      .display_name = "IBL Raw Sky",
      .family = ShaderDebugModeFamily::kIbl,
      .shader_define = "DEBUG_IBL_RAW_SKY",
      .path = DiagnosticsDebugPath::kForwardMeshVariant,
      .requires_lighting_products = true,
    },
    ShaderDebugModeInfo {
      .mode = ShaderDebugMode::kIblIrradiance,
      .canonical_name = "ibl-irradiance",
      .display_name = "IBL Irradiance",
      .family = ShaderDebugModeFamily::kIbl,
      .shader_define = "DEBUG_IBL_IRRADIANCE",
      .path = DiagnosticsDebugPath::kForwardMeshVariant,
      .requires_lighting_products = true,
    },
    ShaderDebugModeInfo {
      .mode = ShaderDebugMode::kBaseColor,
      .canonical_name = "base-color",
      .display_name = "Base Color",
      .family = ShaderDebugModeFamily::kMaterial,
      .shader_define = "DEBUG_BASE_COLOR",
      .path = DiagnosticsDebugPath::kDeferredFullscreen,
      .required_capabilities = kDeferredCapabilities,
      .requires_gbuffer = true,
    },
    ShaderDebugModeInfo {
      .mode = ShaderDebugMode::kWorldNormals,
      .canonical_name = "world-normals",
      .display_name = "World Normals",
      .family = ShaderDebugModeFamily::kMaterial,
      .shader_define = "DEBUG_WORLD_NORMALS",
      .path = DiagnosticsDebugPath::kDeferredFullscreen,
      .required_capabilities = kDeferredCapabilities,
      .requires_gbuffer = true,
    },
    ShaderDebugModeInfo {
      .mode = ShaderDebugMode::kRoughness,
      .canonical_name = "roughness",
      .display_name = "Roughness",
      .family = ShaderDebugModeFamily::kMaterial,
      .shader_define = "DEBUG_ROUGHNESS",
      .path = DiagnosticsDebugPath::kDeferredFullscreen,
      .required_capabilities = kDeferredCapabilities,
      .requires_gbuffer = true,
    },
    ShaderDebugModeInfo {
      .mode = ShaderDebugMode::kMetalness,
      .canonical_name = "metalness",
      .display_name = "Metalness",
      .family = ShaderDebugModeFamily::kMaterial,
      .shader_define = "DEBUG_METALNESS",
      .path = DiagnosticsDebugPath::kDeferredFullscreen,
      .required_capabilities = kDeferredCapabilities,
      .requires_gbuffer = true,
    },
    ShaderDebugModeInfo {
      .mode = ShaderDebugMode::kIblFaceIndex,
      .canonical_name = "ibl-face-index",
      .display_name = "IBL Face Index",
      .family = ShaderDebugModeFamily::kIbl,
      .shader_define = "DEBUG_IBL_FACE_INDEX",
      .path = DiagnosticsDebugPath::kForwardMeshVariant,
      .requires_lighting_products = true,
    },
    ShaderDebugModeInfo {
      .mode = ShaderDebugMode::kIblNoBrdfLut,
      .canonical_name = "ibl-no-brdf-lut",
      .display_name = "IBL No BRDF LUT",
      .family = ShaderDebugModeFamily::kIbl,
      .path = DiagnosticsDebugPath::kNone,
      .requires_lighting_products = true,
      .supported = false,
      .unsupported_reason = "SKIP_BRDF_LUT variant exists but runtime mode "
                            "selection is not wired yet",
    },
    ShaderDebugModeInfo {
      .mode = ShaderDebugMode::kDirectLightingOnly,
      .canonical_name = "direct-lighting-only",
      .display_name = "Direct Lighting Only",
      .family = ShaderDebugModeFamily::kDirectLighting,
      .shader_define = "DEBUG_DIRECT_LIGHTING_ONLY",
      .path = DiagnosticsDebugPath::kForwardMeshVariant,
      .required_capabilities = kLightingCapabilities,
      .requires_lighting_products = true,
    },
    ShaderDebugModeInfo {
      .mode = ShaderDebugMode::kIblOnly,
      .canonical_name = "ibl-only",
      .display_name = "IBL Only",
      .family = ShaderDebugModeFamily::kIbl,
      .shader_define = "DEBUG_IBL_ONLY",
      .path = DiagnosticsDebugPath::kForwardMeshVariant,
      .requires_lighting_products = true,
    },
    ShaderDebugModeInfo {
      .mode = ShaderDebugMode::kDirectPlusIbl,
      .canonical_name = "direct-plus-ibl",
      .display_name = "Direct + IBL",
      .family = ShaderDebugModeFamily::kDirectLighting,
      .shader_define = "DEBUG_DIRECT_PLUS_IBL",
      .path = DiagnosticsDebugPath::kForwardMeshVariant,
      .required_capabilities = kLightingCapabilities,
      .requires_lighting_products = true,
    },
    ShaderDebugModeInfo {
      .mode = ShaderDebugMode::kDirectLightingFull,
      .canonical_name = "direct-lighting-full",
      .display_name = "Direct Lighting Full",
      .family = ShaderDebugModeFamily::kDirectLighting,
      .shader_define = "DEBUG_DIRECT_LIGHTING_FULL",
      .path = DiagnosticsDebugPath::kForwardMeshVariant,
      .required_capabilities = kLightingCapabilities,
      .requires_lighting_products = true,
    },
    ShaderDebugModeInfo {
      .mode = ShaderDebugMode::kDirectLightGates,
      .canonical_name = "direct-light-gates",
      .display_name = "Direct Light Gates",
      .family = ShaderDebugModeFamily::kDirectLighting,
      .shader_define = "DEBUG_DIRECT_LIGHT_GATES",
      .path = DiagnosticsDebugPath::kForwardMeshVariant,
      .required_capabilities = kLightingCapabilities,
      .requires_lighting_products = true,
    },
    ShaderDebugModeInfo {
      .mode = ShaderDebugMode::kDirectBrdfCore,
      .canonical_name = "direct-brdf-core",
      .display_name = "Direct BRDF Core",
      .family = ShaderDebugModeFamily::kDirectLighting,
      .shader_define = "DEBUG_DIRECT_BRDF_CORE",
      .path = DiagnosticsDebugPath::kForwardMeshVariant,
      .required_capabilities = kLightingCapabilities,
      .requires_lighting_products = true,
    },
    ShaderDebugModeInfo {
      .mode = ShaderDebugMode::kDirectionalShadowMask,
      .canonical_name = "directional-shadow-mask",
      .display_name = "Directional Shadow Mask",
      .family = ShaderDebugModeFamily::kShadow,
      .shader_define = "DEBUG_DIRECTIONAL_SHADOW_MASK",
      .path = DiagnosticsDebugPath::kDeferredFullscreen,
      .required_capabilities = kDeferredCapabilities | kShadowCapabilities,
      .requires_scene_depth = true,
      .requires_gbuffer = true,
      .requires_shadow_products = true,
    },
    ShaderDebugModeInfo {
      .mode = ShaderDebugMode::kSceneDepthRaw,
      .canonical_name = "scene-depth-raw",
      .display_name = "Scene Depth Raw",
      .family = ShaderDebugModeFamily::kSceneDepth,
      .shader_define = "DEBUG_SCENE_DEPTH_RAW",
      .path = DiagnosticsDebugPath::kDeferredFullscreen,
      .required_capabilities = kDeferredCapabilities,
      .requires_scene_depth = true,
    },
    ShaderDebugModeInfo {
      .mode = ShaderDebugMode::kSceneDepthLinear,
      .canonical_name = "scene-depth-linear",
      .display_name = "Scene Depth Linear",
      .family = ShaderDebugModeFamily::kSceneDepth,
      .shader_define = "DEBUG_SCENE_DEPTH_LINEAR",
      .path = DiagnosticsDebugPath::kDeferredFullscreen,
      .required_capabilities = kDeferredCapabilities,
      .requires_scene_depth = true,
    },
    ShaderDebugModeInfo {
      .mode = ShaderDebugMode::kSceneDepthMismatch,
      .canonical_name = "scene-depth-mismatch",
      .display_name = "Scene Depth Mismatch",
      .family = ShaderDebugModeFamily::kSceneDepth,
      .shader_define = "DEBUG_SCENE_DEPTH_MISMATCH",
      .path = DiagnosticsDebugPath::kForwardMeshVariant,
      .requires_scene_depth = true,
    },
    ShaderDebugModeInfo {
      .mode = ShaderDebugMode::kMaskedAlphaCoverage,
      .canonical_name = "masked-alpha-coverage",
      .display_name = "Masked Alpha Coverage",
      .family = ShaderDebugModeFamily::kMasked,
      .shader_define = "DEBUG_MASKED_ALPHA_COVERAGE",
      .path = DiagnosticsDebugPath::kForwardMeshVariant,
    },
  };

  static_assert(kShaderDebugModeRegistry.size() == 24U);

} // namespace

auto EnumerateShaderDebugModes() noexcept -> std::span<const ShaderDebugModeInfo>
{
  return kShaderDebugModeRegistry;
}

auto FindShaderDebugModeInfo(const ShaderDebugMode mode) noexcept
  -> const ShaderDebugModeInfo*
{
  for (const auto& info : kShaderDebugModeRegistry) {
    if (info.mode == mode) {
      return &info;
    }
  }
  return nullptr;
}

auto FindShaderDebugModeInfo(const std::string_view canonical_name) noexcept
  -> const ShaderDebugModeInfo*
{
  for (const auto& info : kShaderDebugModeRegistry) {
    if (info.canonical_name == canonical_name) {
      return &info;
    }
  }
  return nullptr;
}

auto ResolveShaderDebugMode(const std::string_view canonical_name) noexcept
  -> std::optional<ShaderDebugMode>
{
  const auto* info = FindShaderDebugModeInfo(canonical_name);
  if (info == nullptr) {
    return std::nullopt;
  }
  return info->mode;
}

} // namespace oxygen::vortex
