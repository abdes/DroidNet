//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

//! Enum representing the different types of shaders.
enum class ShaderType : uint8_t {
    kUnknown = 0, //!< Unknown Shader Type: Used for error handling or uninitialized state.

    //! @{
    //! Graphics pipeline shaders.
    kAmplification, //!< Amplification Shader: Part of the mesh shader pipeline, processes groups of vertices.
    kMesh, //!< Mesh Shader: Part of the mesh shader pipeline, processes meshlets.
    kVertex, //!< Vertex Shader: Processes each vertex and transforms vertex positions.
    kHull, //!< Hull Shader: Used in tessellation, processes control points.
    kDomain, //!< Domain Shader: Used in tessellation, processes tessellated vertices.
    kGeometry, //!< Geometry Shader: Processes entire primitives and can generate additional geometry.
    kPixel, //!< Pixel Shader: Processes each pixel and determines the final color.
    //! @}

    kCompute, //!< Compute Shader: Used for general-purpose computing tasks on the GPU.

    //! @{
    //! Ray tracing shaders.
    kRayGen, //!< Ray Generation Shader: Entry point for ray tracing pipelines.
    kIntersection, //!< Intersection Shader: Handles custom intersection logic in ray tracing.
    kAnyHit, //!< Any-Hit Shader: Invoked on potential ray-object intersections in ray tracing.
    kClosestHit, //!< Closest-Hit Shader: Invoked on the closest intersection in ray tracing.
    kMiss, //!< Miss Shader: Invoked when a ray misses all geometry in ray tracing.
    kCallable, //!< Callable Shader: User-defined callable shaders in ray tracing.
    //! @}

    kMaxShaderType //!< Maximum value sentinel.
};

//! String representation of enum values in `ShaderType`.
OXYGEN_GFX_API auto to_string(ShaderType value) -> const char*;

enum class ShaderStageFlags : uint32_t { // NOLINT(performance-enum-size)
    kNone = 0, //!< No shader stages set.

    //! @{
    //! Graphics.
    kAmplification = OXYGEN_FLAG(0), //!< Amplification Shader stage (mesh pipeline).
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

    kMaxShaderStage = OXYGEN_FLAG(14), //!< Maximum value sentinel for shader stages.

    kAllGraphics = kAmplification | kMesh | kVertex | kHull | kDomain | kGeometry | kPixel, //!< All graphics shader stages.
    kAllRayTracing = kRayGen | kIntersection | kAnyHit | kClosestHit | kMiss | kCallable, //!< All ray tracing shader stages.
    kAll = kAllGraphics | kAllRayTracing | kCompute //!< All shader stages.
};
OXYGEN_DEFINE_FLAGS_OPERATORS(ShaderStageFlags)

//! String representation of enum values in `ShaderStageFlags`.
OXYGEN_GFX_API auto to_string(ShaderStageFlags value) -> std::string;

//! Information describing a shader for pipeline creation.
/*!
 \note The shader name is the file name component of the path including the
       extension.
*/
struct ShaderInfo {
    ShaderType type; //!< Shader type.
    std::string relative_path; //!< Path to the shader source file, relative to the engine shaders directory.
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
[[nodiscard]] OXYGEN_GFX_API auto MakeShaderIdentifier(ShaderType shader_type, const std::string& relative_path) -> std::string;

//! Generates a unique identifier string for a shader using ShaderInfo.
/*!
 \see MakeShaderIdentifier(ShaderType, const std::string&)
*/
[[nodiscard]] inline auto MakeShaderIdentifier(const ShaderInfo& shader)
{
    return MakeShaderIdentifier(shader.type, shader.relative_path);
}

} // namespace oxygen::graphics
