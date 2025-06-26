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

//! Defines a single vertex with common attributes for mesh geometry.
/*!
 Defines the per-vertex attributes used for mesh geometry in the Oxygen Engine.
 This structure is standalone and reusable across engine systems (procedural
 generation, import/export, GPU upload, or physics).

 @see MeshAsset, MeshView
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

//! Strict bitwise equality for Vertex (all fields, no epsilon tolerance)
inline bool StrictlyEqual(const Vertex& a, const Vertex& b) noexcept
{
  return a.position == b.position && a.normal == b.normal
    && a.texcoord == b.texcoord && a.tangent == b.tangent
    && a.bitangent == b.bitangent && a.color == b.color;
}

//! Epsilon-based equality operator for Vertex (quantized, matches
//! QuantizedVertexHash)
inline bool operator==(const Vertex& lhs, const Vertex& rhs) noexcept
{
  constexpr float epsilon = 1e-5f;
  return glm::all(glm::epsilonEqual(lhs.position, rhs.position, epsilon))
    && glm::all(glm::epsilonEqual(lhs.normal, rhs.normal, epsilon))
    && glm::all(glm::epsilonEqual(lhs.texcoord, rhs.texcoord, epsilon))
    && glm::all(glm::epsilonEqual(lhs.tangent, rhs.tangent, epsilon))
    && glm::all(glm::epsilonEqual(lhs.bitangent, rhs.bitangent, epsilon))
    && glm::all(glm::epsilonEqual(lhs.color, rhs.color, epsilon));
}

//! Hash functor for Vertex using quantization compatible with AlmostEqual.
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
namespace oxygen::data {
struct QuantizedVertexHash {
  float epsilon = 1e-5f;
  std::size_t operator()(const Vertex& v) const noexcept
  {
    auto quantize = [e = epsilon](float x) -> int32_t {
      return static_cast<int32_t>(std::round(x / e));
    };
    std::size_t h = 0;
    // clang-format off
    for (int i = 0; i < 3; ++i) oxygen::HashCombine(h, quantize(v.position[i]));
    for (int i = 0; i < 3; ++i) oxygen::HashCombine(h, quantize(v.normal[i]));
    for (int i = 0; i < 2; ++i) oxygen::HashCombine(h, quantize(v.texcoord[i]));
    for (int i = 0; i < 3; ++i) oxygen::HashCombine(h, quantize(v.tangent[i]));
    for (int i = 0; i < 3; ++i) oxygen::HashCombine(h, quantize(v.bitangent[i]));
    for (int i = 0; i < 4; ++i) oxygen::HashCombine(h, quantize(v.color[i]));
    // clang-format on
    return h;
  }
};
} // namespace oxygen::data

namespace std {
//! std::hash specialization for Vertex using quantized hash (epsilon = 1e-5f)
template <> struct hash<::Vertex> {
  std::size_t operator()(const ::Vertex& v) const noexcept
  {
    return ::oxygen::data::QuantizedVertexHash {}(v);
  }
};
} // namespace std
