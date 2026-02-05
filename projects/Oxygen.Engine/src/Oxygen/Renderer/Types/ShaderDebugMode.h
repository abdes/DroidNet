//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen::engine {

//! Debug visualization mode for shading passes.
//!
//! These modes correspond to boolean defines in ForwardDebug_PS.hlsl.
//! The shader is compiled with different defines to create specialized
//! visualization variants.
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
};

} // namespace oxygen::engine
