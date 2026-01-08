//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <string_view>
#include <vector>

#include <Oxygen/Graphics/Common/Shaders.h>

namespace oxygen::engine::permutation {

//! @name Shader Define Names
//! @{
//! Standard shader define names for material-driven permutations.
/*!
 These constants define the canonical names for shader preprocessor defines
 used to generate material permutations. All names must match HLSL `#ifdef`
 guards exactly.

 @see shader-system.md for the full permutation naming convention.
*/

//! Alpha-tested (cutout) materials. Enables `clip()` in pixel shader.
inline constexpr std::string_view kAlphaTest = "ALPHA_TEST";

//! Reserved for future use. Double-sided is currently handled via rasterizer
//! cull mode, not a shader define.
inline constexpr std::string_view kDoubleSided = "DOUBLE_SIDED";

//! (Phase 2) Emissive channel enabled.
inline constexpr std::string_view kHasEmissive = "HAS_EMISSIVE";

//! (Phase 9) Clear coat layer enabled.
inline constexpr std::string_view kHasClearcoat = "HAS_CLEARCOAT";

//! (Deferred) Transmission/refraction enabled.
inline constexpr std::string_view kHasTransmission = "HAS_TRANSMISSION";

//! (Deferred) Height/parallax mapping enabled.
inline constexpr std::string_view kHasHeightMap = "HAS_HEIGHT_MAP";
//! @}

// ---------------------------------------------------------------------------
// Constexpr Define Specification
// ---------------------------------------------------------------------------

//! A compile-time shader define specification.
/*!
 Lightweight constexpr alternative to `graphics::ShaderDefine` for use in
 compile-time permutation definitions. Convert to `ShaderDefine` at PSO
 creation time using `ToDefines()`.
*/
struct DefineSpec {
  std::string_view name;
  std::string_view value { "1" };
};

// ---------------------------------------------------------------------------
// Standard Permutation Sets
// ---------------------------------------------------------------------------

//! @name Permutation Sets
//! @{
//! Pre-defined constexpr arrays for common material permutation combinations.

//! Opaque materials - no special defines.
inline constexpr std::array<DefineSpec, 0> kOpaqueDefines {};

//! Alpha-tested (masked) materials - enables ALPHA_TEST.
inline constexpr std::array kMaskedDefines {
  DefineSpec { kAlphaTest },
};
//! @}

// ---------------------------------------------------------------------------
// Conversion Utilities
// ---------------------------------------------------------------------------

//! Converts a constexpr DefineSpec array to a vector of ShaderDefine.
/*!
 Use this at PSO creation time to convert compile-time permutation specs
 to the runtime format expected by ShaderRequest.

 @tparam N Number of defines in the array.
 @param specs Constexpr array of DefineSpec.
 @return Vector of ShaderDefine suitable for ShaderRequest::defines.

 Example:
 ```cpp
 auto ps_request = ShaderRequest {
   .stage = ShaderType::kPixel,
   .source_path = "Passes/Forward/ForwardMesh_PS.hlsl",
   .entry_point = "PS",
   .defines = ToDefines(permutation::kMaskedDefines),
 };
 ```
*/
template <std::size_t N>
[[nodiscard]] auto ToDefines(const std::array<DefineSpec, N>& specs)
  -> std::vector<graphics::ShaderDefine>
{
  std::vector<graphics::ShaderDefine> result;
  result.reserve(N);
  for (const auto& spec : specs) {
    result.emplace_back(std::string(spec.name), std::string(spec.value));
  }
  return result;
}

} // namespace oxygen::engine::permutation
