//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <type_traits>

#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! Enum representing the different types of shaders.
enum class ShaderType : uint32_t {
  kUnknown
  = 0, //!< Unknown Shader Type: Used for error handling or uninitialized state.

  //! @{
  //! Graphics pipeline shaders.
  kAmplification,
  kMesh,
  kVertex,
  kHull,
  kDomain,
  kGeometry,
  kPixel,
  //! @}

  kCompute,

  //! @{
  //! Ray tracing shaders.
  kRayGen,
  kIntersection,
  kAnyHit,
  kClosestHit,
  kMiss,
  kCallable,
  //! @}

  kMaxShaderType //!< Maximum value sentinel.
};

// For binary encoding, material assets use a 32-bit integer, as flags, to
// encode which shaders it used for each shader type.
static_assert(
  static_cast<std::underlying_type_t<ShaderType>>(ShaderType::kMaxShaderType)
    < 32,
  "ShaderType enum values must be < 32 (for shader assets binary encoding)");

//! String representation of enum values in `ShaderType`.
OXGN_DATA_NDAPI auto to_string(ShaderType value) -> const char*;

} // namespace oxygen::data
