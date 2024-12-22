//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include "Oxygen/base/Types.h"
#include "Oxygen/Renderers/Direct3d12/api_export.h"

namespace oxygen::renderer::d3d12 {

  /// Enum representing the different types of shaders supported by Direct3D 12.
  enum class ShaderType : uint8_t
  {
    kVertex = 0, ///< Vertex Shader: Processes each vertex and transforms vertex positions.
    kPixel = 1, ///< Pixel Shader: Processes each pixel and determines the final color.
    kGeometry = 2, ///< Geometry Shader: Processes entire primitives and can generate additional geometry.
    kHull = 3, ///< Hull Shader: Used in tessellation, processes control points.
    kDomain = 4, ///< Domain Shader: Used in tessellation, processes tessellated vertices.
    kCompute = 5, ///< Compute Shader: Used for general-purpose computing tasks on the GPU.
    kAmplification = 6, ///< Amplification Shader: Part of the mesh shader pipeline, processes groups of vertices.
    kMesh = 7, ///< Mesh Shader: Part of the mesh shader pipeline, processes meshlets.

    kCount = 8 ///< Count of shader types.
  };

  enum class EngineShaderId : uint8_t
  {
    kFullscreenTriangleVS = 0,
    kFillColorPS = 1,

    kCount = 2
  };

  namespace shaders {

    OXYGEN_D3D12_API auto Initialize() -> bool;
    OXYGEN_D3D12_API void Shutdown();

    OXYGEN_D3D12_API auto GetEngineShader(EngineShaderId id) -> D3D12_SHADER_BYTECODE;

  }  // namespace shaders

  /**
   * String representation of the enum values in ShaderType and EngineShaders.
   * @{
   */
  inline const char* to_string(const ShaderType value)
  {
    switch (value)
    {
    case ShaderType::kVertex: return "Vertex Shader";
    case ShaderType::kPixel: return "Pixel Shader";
    case ShaderType::kGeometry: return "Geometry Shader";
    case ShaderType::kHull: return "Hull Shader";
    case ShaderType::kDomain: return "Domain Shader";
    case ShaderType::kCompute: return "Compute Shader";
    case ShaderType::kAmplification: return "Amplification Shader";
    case ShaderType::kMesh: return "Mesh Shader";

    case ShaderType::kCount: return "__count__";
    }
    return "unknown";
  }

  inline const char* to_string(const EngineShaderId value)
  {
    switch (value)
    {
    case EngineShaderId::kFullscreenTriangleVS: return "Fullscreen Triangle VS";
    case EngineShaderId::kFillColorPS: return "Fullscreen Triangle VS";

    case EngineShaderId::kCount: return "__count__";
    }
    return "unknown";
  }
  /** @} */

}
