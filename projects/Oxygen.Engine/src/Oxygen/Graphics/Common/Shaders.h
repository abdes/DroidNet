//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

enum class ShaderStageFlags : uint32_t { // NOLINT(performance-enum-size)
  kNone = 0, //!< No shader stages set.

  //! @{
  //! Graphics.
  kAmplification
  = OXYGEN_FLAG(0), //!< Amplification Shader stage (mesh pipeline).
  kMesh = OXYGEN_FLAG(1), //!< Mesh Shader stage (mesh pipeline).
  kVertex = OXYGEN_FLAG(2), //!< Vertex Shader stage.
  kHull = OXYGEN_FLAG(3), //!< Hull (Tessellation Control) Shader stage.
  kDomain = OXYGEN_FLAG(4), //!< Domain (Tessellation Evaluation) Shader stage.
  kGeometry = OXYGEN_FLAG(5), //!< Geometry Shader stage.
  kPixel = OXYGEN_FLAG(6), //!< Pixel (Fragment) Shader stage.
  //! @}

  kCompute = OXYGEN_FLAG(7), //!< Compute Shader stage.

  //! @{
  //! Ray tracing.
  kRayGen = OXYGEN_FLAG(8), //!< Ray Generation Shader stage (ray tracing).
  kIntersection = OXYGEN_FLAG(9), //!< Intersection Shader stage (ray tracing).
  kAnyHit = OXYGEN_FLAG(10), //!< Any-Hit Shader stage (ray tracing).
  kClosestHit = OXYGEN_FLAG(11), //!< Closest-Hit Shader stage (ray tracing).
  kMiss = OXYGEN_FLAG(12), //!< Miss Shader stage (ray tracing).
  kCallable = OXYGEN_FLAG(13), //!< Callable Shader stage (ray tracing).
  //! @}

  kMaxShaderStage = kCallable, //!< Maximum value sentinel for shader stages.

  kAllGraphics = kAmplification | kMesh | kVertex | kHull | kDomain | kGeometry
    | kPixel, //!< All graphics shader stages.
  kAllRayTracing = kRayGen | kIntersection | kAnyHit | kClosestHit | kMiss
    | kCallable, //!< All ray tracing shader stages.
  kAll = kAllGraphics | kAllRayTracing | kCompute //!< All shader stages.
};
OXYGEN_DEFINE_FLAGS_OPERATORS(ShaderStageFlags)

//! String representation of enum values in `ShaderStageFlags`.
OXGN_GFX_API auto to_string(ShaderStageFlags value) -> std::string;

//! Information describing a shader for pipeline creation.
/*!
 \note The shader name is the file name component of the path including the
       extension.
*/
struct ShaderInfo {
  ShaderType type; //!< Shader type.
  std::string relative_path; //!< Path to the shader source file, relative to
                             //!< the engine shaders directory.
  std::string entry_point { "main" }; //!< Entry point function name.
};

//! Generates a unique identifier string for a shader.
/*!
 \param shader_type The type of the shader.
 \param relative_path The path to the shader source file, relative to the engine
        shaders directory.
 \return A unique string identifier for the shader.
 \see MakeShaderIdentifier(const ShaderInfo&)
*/
OXGN_GFX_NDAPI auto MakeShaderIdentifier(
  ShaderType shader_type, const std::string& relative_path) -> std::string;

//! Generates a unique identifier string for a shader using ShaderInfo.
/*!
 \see MakeShaderIdentifier(ShaderType, const std::string&)
*/
[[nodiscard]] inline auto MakeShaderIdentifier(const ShaderInfo& shader)
{
  return MakeShaderIdentifier(shader.type, shader.relative_path);
}

} // namespace oxygen::graphics
