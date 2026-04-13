//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>

namespace oxygen::vortex {

enum class ShaderDebugMode : uint8_t {
  kDisabled = 0,
  kLightCullingHeatMap = 1,
  kDepthSlice = 2,
  kClusterIndex = 3,
  kIblSpecular = 4,
  kIblRawSky = 5,
  kIblIrradiance = 6,
  kBaseColor = 7,
  kUv0 = 8,
  kOpacity = 9,
  kWorldNormals = 10,
  kRoughness = 11,
  kMetalness = 12,
  kIblFaceIndex = 13,
  kIblNoBrdfLut = 14,
  kDirectLightingOnly = 15,
  kIblOnly = 16,
  kDirectPlusIbl = 17,
  kDirectLightingFull = 18,
  kDirectLightGates = 19,
  kDirectBrdfCore = 20,
  kVirtualShadowMask = 21,
  kSceneDepthRaw = 22,
  kSceneDepthLinear = 23,
  kSceneDepthMismatch = 24,
  kMaskedAlphaCoverage = 25,
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

} // namespace oxygen::vortex
