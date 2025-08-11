//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>

#include <Oxygen/Base/Hash.h>

namespace oxygen::data {

//! Defines a single vertex with common attributes for mesh geometry.
/*!
 Defines the per-vertex attributes used for mesh geometry in the Oxygen Engine.
 This structure is standalone and reusable across engine systems (procedural
 generation, import/export, GPU upload, or physics).

 @see Mesh, MeshView
*/
struct Vertex {
  glm::vec3 position; //!< Object-space position
  glm::vec3 normal; //!< Object-space normal
  glm::vec2 texcoord; //!< Texture coordinates (UV)
  glm::vec3 tangent; //!< Tangent vector (optional, for normal mapping)
  glm::vec3 bitangent; //!< Bitangent vector (optional, for normal mapping)
  glm::vec4 color; //!< Vertex color (optional, for per-vertex tinting)
  // Extend as needed: skin weights, bone indices, etc.
};

//! Default geometric epsilon used for vertex fuzzy comparisons & hashing.
/*!
 Chosen larger than std::numeric_limits<float>::epsilon() to account for
 accumulated floating-point error across typical mesh processing (imports,
 tangent generation, minor transforms). Keeps equality stable while avoiding
 collapsing distinct vertices under normal scale (~1 unit world space).

 @warning Adjust with care: must remain consistent with @ref operator== and
 QuantizedVertexHash to preserve equivalence relation used in unordered
 containers. Changing it requires updating tests relying on approximate equality
 semantics.
*/
inline constexpr float kVertexEpsilon = 1e-5f; // domain-tuned tolerance

//! Strict bitwise-like equality for Vertex (all fields, no epsilon tolerance)
/*!
 Compares every component exactly (delegates to glm component == operators).
 This is suitable for scenarios requiring deterministic reproducibility (e.g.,
 serialization round-trips) or hashing with a separate exact hash.

 @note Floating-point semantics: this treats +0 and -0 as equal (per glm), and
 NaN fields will compare unequal (propagating typical IEEE rules).
*/
inline auto StrictlyEqual(const Vertex& a, const Vertex& b) noexcept -> bool
{
  return a.position == b.position && a.normal == b.normal
    && a.texcoord == b.texcoord && a.tangent == b.tangent
    && a.bitangent == b.bitangent && a.color == b.color;
}

//! Approximate component-wise equality using kVertexEpsilon.
/*!
 Provides fuzzy equality tolerant to small floating-point perturbations. The
 epsilon is uniform across all components for simplicity. This matches the
 quantization grid used by QuantizedVertexHash ensuring that (a == b) implies
 equal hash codes (consistency for unordered containers).

 @warning Not a strict equivalence relation mathematically; transitivity can
 fail near tolerance boundaries. Avoid relying on chaining (a==b && b==c)
 implying (a==c) when values differ by ~epsilon.
*/
inline auto operator==(const Vertex& lhs, const Vertex& rhs) noexcept -> bool
{
  const float e = kVertexEpsilon;
  return glm::all(glm::epsilonEqual(lhs.position, rhs.position, e))
    && glm::all(glm::epsilonEqual(lhs.normal, rhs.normal, e))
    && glm::all(glm::epsilonEqual(lhs.texcoord, rhs.texcoord, e))
    && glm::all(glm::epsilonEqual(lhs.tangent, rhs.tangent, e))
    && glm::all(glm::epsilonEqual(lhs.bitangent, rhs.bitangent, e))
    && glm::all(glm::epsilonEqual(lhs.color, rhs.color, e));
}

//! Fuzzy comparison with custom epsilon overriding kVertexEpsilon.
/*!
 Useful when a caller requires stricter or looser tolerance while preserving
 semantics consistent with QuantizedVertexHash (if using same epsilon).

 @param lhs Left vertex
 @param rhs Right vertex
 @param epsilon Comparison tolerance (absolute per component)
 @return true if all components differ by <= epsilon
*/
inline auto AlmostEqual(const Vertex& lhs, const Vertex& rhs,
  float epsilon = kVertexEpsilon) noexcept -> bool
{
  return glm::all(glm::epsilonEqual(lhs.position, rhs.position, epsilon))
    && glm::all(glm::epsilonEqual(lhs.normal, rhs.normal, epsilon))
    && glm::all(glm::epsilonEqual(lhs.texcoord, rhs.texcoord, epsilon))
    && glm::all(glm::epsilonEqual(lhs.tangent, rhs.tangent, epsilon))
    && glm::all(glm::epsilonEqual(lhs.bitangent, rhs.bitangent, epsilon))
    && glm::all(glm::epsilonEqual(lhs.color, rhs.color, epsilon));
}

//! Hash functor for Vertex using quantization compatible with operator== /
//! AlmostEqual.
/*!
 Computes a hash value for a Vertex by quantizing all floating-point fields to
 a grid defined by the given epsilon. This ensures that vertices considered
 equal by AlmostEqual will also hash to the same value, making this suitable
 for use in hash-based containers for deduplication or mesh optimization.

 @param v Vertex to hash
 @param epsilon Quantization grid size (should match AlmostEqual epsilon)
 @return Hash value

 @see Vertex, AlmostEqual
*/
struct QuantizedVertexHash {
  float epsilon = kVertexEpsilon;
  auto operator()(const Vertex& v) const noexcept -> std::size_t
  {
    auto quantize = [e = epsilon](const float x) -> int32_t {
      return static_cast<int32_t>(std::round(x / e));
    };
    std::size_t h = 0;
    // clang-format off
    for (int i = 0; i < 3; ++i) {HashCombine(h, quantize(v.position[i]));}
    for (int i = 0; i < 3; ++i) {HashCombine(h, quantize(v.normal[i]));}
    for (int i = 0; i < 2; ++i) {HashCombine(h, quantize(v.texcoord[i]));}
    for (int i = 0; i < 3; ++i) {HashCombine(h, quantize(v.tangent[i]));}
    for (int i = 0; i < 3; ++i) {HashCombine(h, quantize(v.bitangent[i]));}
    for (int i = 0; i < 4; ++i) {HashCombine(h, quantize(v.color[i]));}
    // clang-format on
    return h;
  }
};

} // namespace oxygen::data

//! std::hash specialization for Vertex using quantized hash (epsilon =
//! kVertexEpsilon)
template <> struct std::hash<oxygen::data::Vertex> {
  auto operator()(const oxygen::data::Vertex& v) const noexcept -> std::size_t
  {
    return oxygen::data::QuantizedVertexHash {}(v);
  }
};
