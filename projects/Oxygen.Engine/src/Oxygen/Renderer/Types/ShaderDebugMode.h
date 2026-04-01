//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>

namespace oxygen::engine {

//! Debug visualization mode for shading passes.
//!
//! These modes correspond to boolean defines in ForwardDebug_PS.hlsl or
//! ForwardMesh_PS.hlsl. The shader is compiled with different defines to create
//! specialized visualization variants.
enum class ShaderDebugMode : uint8_t {
  kDisabled = 0, //!< Normal PBR rendering (default)

  // Light culling debug modes
  kLightCullingHeatMap = 1, //!< Heat map of lights per cluster
  kDepthSlice = 2, //!< Visualize depth slice (clustered mode)
  kClusterIndex = 3, //!< Visualize cluster index as checkerboard

  // IBL debug modes
  kIblSpecular = 4, //!< Visualize IBL specular (prefilter map sampling)
  kIblRawSky = 5, //!< Visualize raw sky cubemap sampling (no prefilter)
  kIblIrradiance = 6, //!< Visualize IBL irradiance (ambient term)

  // Material/UV debug modes
  kBaseColor = 7, //!< Visualize base color texture (albedo)
  kUv0 = 8, //!< Visualize UV0 coordinates
  kOpacity = 9, //!< Visualize base alpha/opacity
  kWorldNormals = 10, //!< Visualize world-space normals
  kRoughness = 11, //!< Visualize material roughness
  kMetalness = 12, //!< Visualize material metalness

  // IBL mapping debug modes
  kIblFaceIndex = 13, //!< Visualize cubemap face selection (no textures)
  kIblNoBrdfLut = 14, //!< Normal shading with BRDF LUT bypassed
  kDirectLightingOnly = 15, //!< Normal forward shader, direct-light term only
  kIblOnly = 16, //!< Normal forward shader, IBL term only
  kDirectPlusIbl = 17, //!< Normal forward shader, direct + IBL terms only
  kDirectLightingFull = 18, //!< Full direct-light term from ForwardMesh_PS
  kDirectLightGates = 19, //!< R=shadow visibility, G=sun transmittance
  kDirectBrdfCore = 20, //!< Ungated directional BRDF core from ForwardMesh_PS
  kVirtualShadowMask = 21, //!< Stage 15 VSM screen-space shadow mask
  kSceneDepthRaw = 22, //!< Published scene depth in reversed-Z space
  kSceneDepthLinear = 23, //!< Published scene depth reconstructed to eye depth
  kSceneDepthMismatch = 24, //!< Shading depth disagreement vs pre-pass depth
  kMaskedAlphaCoverage = 25, //!< Masked alpha pass/fail classification
};

constexpr auto to_string(const ShaderDebugMode mode) noexcept
  -> std::string_view
{
  switch (mode) {
    // clang-format off
      case ShaderDebugMode::kDisabled: return "Disabled";
      case ShaderDebugMode::kLightCullingHeatMap: return "LightCullingHeatMap";
      case ShaderDebugMode::kDepthSlice: return "DepthSlice";
      case ShaderDebugMode::kClusterIndex: return "ClusterIndex";
      case ShaderDebugMode::kIblSpecular: return "IblSpecular";
      case ShaderDebugMode::kIblRawSky: return "IblRawSky";
      case ShaderDebugMode::kIblIrradiance: return "IblIrradiance";
      case ShaderDebugMode::kBaseColor: return "BaseColor";
      case ShaderDebugMode::kUv0: return "Uv0";
      case ShaderDebugMode::kOpacity: return "Opacity";
      case ShaderDebugMode::kWorldNormals: return "WorldNormals";
      case ShaderDebugMode::kRoughness: return "Roughness";
      case ShaderDebugMode::kMetalness: return "Metalness";
      case ShaderDebugMode::kIblFaceIndex: return "IblFaceIndex";
      case ShaderDebugMode::kIblNoBrdfLut: return "IblNoBrdfLut";
      case ShaderDebugMode::kDirectLightingOnly: return "DirectLightingOnly";
      case ShaderDebugMode::kIblOnly: return "IblOnly";
      case ShaderDebugMode::kDirectPlusIbl: return "DirectPlusIbl";
      case ShaderDebugMode::kDirectLightingFull: return "DirectLightingFull";
      case ShaderDebugMode::kDirectLightGates: return "DirectLightGates";
      case ShaderDebugMode::kDirectBrdfCore: return "DirectBrdfCore";
      case ShaderDebugMode::kVirtualShadowMask: return "VirtualShadowMask";
      case ShaderDebugMode::kSceneDepthRaw: return "SceneDepthRaw";
      case ShaderDebugMode::kSceneDepthLinear: return "SceneDepthLinear";
      case ShaderDebugMode::kSceneDepthMismatch: return "SceneDepthMismatch";
      case ShaderDebugMode::kMaskedAlphaCoverage: return "MaskedAlphaCoverage";
    // clang-format on
  }
  return "__Unknown__";
}

constexpr auto IsLightCullingDebugMode(const ShaderDebugMode mode) noexcept
  -> bool
{
  switch (mode) {
  case ShaderDebugMode::kLightCullingHeatMap:
  case ShaderDebugMode::kDepthSlice:
  case ShaderDebugMode::kClusterIndex:
    return true;
  default:
    return false;
  }
}

constexpr auto IsIblDebugMode(const ShaderDebugMode mode) noexcept -> bool
{
  switch (mode) {
  case ShaderDebugMode::kIblSpecular:
  case ShaderDebugMode::kIblRawSky:
  case ShaderDebugMode::kIblIrradiance:
  case ShaderDebugMode::kIblFaceIndex:
    return true;
  default:
    return false;
  }
}

constexpr auto IsNonIblDebugMode(const ShaderDebugMode mode) noexcept -> bool
{
  switch (mode) {
  case ShaderDebugMode::kLightCullingHeatMap:
  case ShaderDebugMode::kDepthSlice:
  case ShaderDebugMode::kClusterIndex:
  case ShaderDebugMode::kBaseColor:
  case ShaderDebugMode::kUv0:
  case ShaderDebugMode::kOpacity:
  case ShaderDebugMode::kWorldNormals:
  case ShaderDebugMode::kRoughness:
  case ShaderDebugMode::kMetalness:
  case ShaderDebugMode::kDirectLightingOnly:
  case ShaderDebugMode::kIblOnly:
  case ShaderDebugMode::kDirectPlusIbl:
  case ShaderDebugMode::kDirectLightingFull:
  case ShaderDebugMode::kDirectLightGates:
  case ShaderDebugMode::kDirectBrdfCore:
  case ShaderDebugMode::kVirtualShadowMask:
  case ShaderDebugMode::kSceneDepthRaw:
  case ShaderDebugMode::kSceneDepthLinear:
  case ShaderDebugMode::kSceneDepthMismatch:
  case ShaderDebugMode::kMaskedAlphaCoverage:
    return true;
  default:
    return false;
  }
}

constexpr auto UsesForwardMeshDebugVariant(const ShaderDebugMode mode) noexcept
  -> bool
{
  switch (mode) {
  case ShaderDebugMode::kDirectLightingOnly:
  case ShaderDebugMode::kIblOnly:
  case ShaderDebugMode::kDirectPlusIbl:
  case ShaderDebugMode::kDirectLightingFull:
  case ShaderDebugMode::kDirectLightGates:
  case ShaderDebugMode::kDirectBrdfCore:
    return true;
  default:
    return false;
  }
}

constexpr auto GetShaderDebugDefineName(const ShaderDebugMode mode) noexcept
  -> std::string_view
{
  switch (mode) {
  case ShaderDebugMode::kLightCullingHeatMap:
    return "DEBUG_LIGHT_HEATMAP";
  case ShaderDebugMode::kDepthSlice:
    return "DEBUG_DEPTH_SLICE";
  case ShaderDebugMode::kClusterIndex:
    return "DEBUG_CLUSTER_INDEX";
  case ShaderDebugMode::kIblSpecular:
    return "DEBUG_IBL_SPECULAR";
  case ShaderDebugMode::kIblRawSky:
    return "DEBUG_IBL_RAW_SKY";
  case ShaderDebugMode::kIblIrradiance:
    return "DEBUG_IBL_IRRADIANCE";
  case ShaderDebugMode::kIblFaceIndex:
    return "DEBUG_IBL_FACE_INDEX";
  case ShaderDebugMode::kBaseColor:
    return "DEBUG_BASE_COLOR";
  case ShaderDebugMode::kUv0:
    return "DEBUG_UV0";
  case ShaderDebugMode::kOpacity:
    return "DEBUG_OPACITY";
  case ShaderDebugMode::kWorldNormals:
    return "DEBUG_WORLD_NORMALS";
  case ShaderDebugMode::kRoughness:
    return "DEBUG_ROUGHNESS";
  case ShaderDebugMode::kMetalness:
    return "DEBUG_METALNESS";
  case ShaderDebugMode::kDirectLightingOnly:
    return "DEBUG_DIRECT_LIGHTING_ONLY";
  case ShaderDebugMode::kIblOnly:
    return "DEBUG_IBL_ONLY";
  case ShaderDebugMode::kDirectPlusIbl:
    return "DEBUG_DIRECT_PLUS_IBL";
  case ShaderDebugMode::kDirectLightingFull:
    return "DEBUG_DIRECT_LIGHTING_FULL";
  case ShaderDebugMode::kDirectLightGates:
    return "DEBUG_DIRECT_LIGHT_GATES";
  case ShaderDebugMode::kDirectBrdfCore:
    return "DEBUG_DIRECT_BRDF_CORE";
  case ShaderDebugMode::kVirtualShadowMask:
    return "DEBUG_VIRTUAL_SHADOW_MASK";
  case ShaderDebugMode::kSceneDepthRaw:
    return "DEBUG_SCENE_DEPTH_RAW";
  case ShaderDebugMode::kSceneDepthLinear:
    return "DEBUG_SCENE_DEPTH_LINEAR";
  case ShaderDebugMode::kSceneDepthMismatch:
    return "DEBUG_SCENE_DEPTH_MISMATCH";
  case ShaderDebugMode::kMaskedAlphaCoverage:
    return "DEBUG_MASKED_ALPHA_COVERAGE";
  default:
    return {};
  }
}

} // namespace oxygen::engine
