//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <type_traits>

#include <Oxygen/Data/api_export.h>

namespace oxygen {

//! Enum representing the different types of shaders.
enum class ShaderType : uint32_t {
  // clang-format off
  kUnknown       = 0,  //!< Unknown Shader Type (Invalid or uninitialized state)

  //! @{
  //! Graphics pipeline shaders.
  kAmplification = 1,
  kMesh          = 2,
  kVertex        = 3,
  kHull          = 4,
  kDomain        = 5,
  kGeometry      = 6,
  kPixel         = 7,
  //! @}

  kCompute       = 8,

  //! @{
  //! Ray tracing shaders.
  kRayGen        = 9,
  kIntersection  = 10,
  kAnyHit        = 11,
  kClosestHit    = 12,
  kMiss          = 13,
  kCallable      = 14,
  //! @}

  kMaxShaderType  //!< Maximum value sentinel.
  // clang-format on
};

// For binary encoding, material assets use a 32-bit integer, as flags, to
// encode which shaders it used for each shader type.
static_assert(
  static_cast<std::underlying_type_t<ShaderType>>(ShaderType::kMaxShaderType)
    < 32,
  "ShaderType enum values must be < 32 (for shader assets binary encoding)");

//! String representation of enum values in `ShaderType`.
OXGN_DATA_NDAPI auto to_string(ShaderType value) -> const char*;

} // namespace oxygen
