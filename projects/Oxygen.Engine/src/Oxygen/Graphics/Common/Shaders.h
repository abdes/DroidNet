//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

//! A single shader define used for compilation and cache identity.
struct ShaderDefine {
  std::string name;
  std::optional<std::string> value {};

  auto operator==(const ShaderDefine&) const -> bool = default;
};

//! Human-readable shader request used at PSO call sites.
/*!
 Carries shader stage, source path, entry point and optional defines.
*/
struct ShaderRequest {
  ShaderType stage { ShaderType::kVertex };
  std::string source_path;
  std::string entry_point;
  std::vector<ShaderDefine> defines {};

  auto operator==(const ShaderRequest&) const -> bool = default;
};

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

//! Canonicalizes and validates a shader request.
/*!
 Applies the shader request canonicalization rules:

 - `source_path` must be relative, normalized and use forward slashes.
 - `entry_point` must be a valid identifier.
 - `defines` are validated, de-duplicated by name, and sorted by name.

 @throw std::invalid_argument if the request is invalid.
*/
OXGN_GFX_NDAPI auto CanonicalizeShaderRequest(ShaderRequest request)
  -> ShaderRequest;

//! Computes a stable 64-bit cache key for a canonicalized shader request.
/*!
 The returned key is suitable for persistent cache identity (e.g. archive
 indexing) because it does not depend on platform-specific `std::hash`
 behavior.

 @note The key is computed from the canonicalized form of the request.
*/
OXGN_GFX_NDAPI auto ComputeShaderRequestKey(const ShaderRequest& request)
  -> uint64_t;

//! Formats a shader request for logging/debugging.
/*! Format: `<STAGE>@<source_path>:<entry_point>` */
OXGN_GFX_NDAPI auto FormatShaderLogKey(const ShaderRequest& request)
  -> std::string;

//! Formats a ShaderInfo entry for logging/debugging.
/*! Format: `<STAGE>@<relative_path>:<entry_point>` */
OXGN_GFX_NDAPI auto FormatShaderLogKey(const ShaderInfo& shader_info)
  -> std::string;

//! Hash for ShaderRequest (assumes canonicalized requests).
struct ShaderRequestHash {
  auto operator()(const ShaderRequest& request) const noexcept -> size_t;
};

//! Equality for ShaderRequest (assumes canonicalized requests).
struct ShaderRequestEq {
  auto operator()(const ShaderRequest& a, const ShaderRequest& b) const noexcept
    -> bool
  {
    return a == b;
  }
};

} // namespace oxygen::graphics
