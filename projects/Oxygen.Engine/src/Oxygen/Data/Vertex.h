//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>

namespace oxygen::data {

//! Defines a single vertex with common attributes for mesh geometry.
/*!
 Defines the per-vertex attributes used for mesh geometry in the Oxygen Engine.
 This structure is standalone and reusable across engine systems (procedural
 generation, import/export, GPU upload, or physics).

 Two vertices are equal when every component is equal, so `==` is exact: +0
 and -0 are equal and a NaN component equals nothing. AlmostEqual compares
 with a tolerance.

 @see Mesh, MeshView
*/
struct Vertex {
  glm::vec3 position; //!< Object-space position
  glm::vec3 normal; //!< Object-space normal
  glm::vec2 texcoord; //!< Texture coordinates (UV)
  glm::vec3 tangent; //!< Tangent vector (optional, for normal mapping)
  glm::vec3 bitangent; //!< Bitangent vector (optional, for normal mapping)
  glm::vec4 color; //!< Vertex color (optional, for per-vertex tinting)
  // Skinning is not part of the vertex: a skinned mesh carries its joint
  // indices and weights in streams of their own (pak::geometry::
  // SkinnedMeshInfo). Morph targets have no stream yet; theirs take a new
  // MeshDesc info arm, which the union has room for without a format break.

  [[nodiscard]] auto operator==(const Vertex&) const -> bool = default;
};

//! Default tolerance of AlmostEqual.
/*!
 Chosen larger than std::numeric_limits<float>::epsilon() to account for
 accumulated floating-point error across typical mesh processing (imports,
 tangent generation, minor transforms), while not collapsing distinct vertices
 under normal scale (~1 unit world space).
*/
inline constexpr float kVertexEpsilon = 1e-5F; // domain-tuned tolerance

//! Approximate component-wise equality.
/*!
 Tolerates small floating-point perturbations, uniformly across all
 components.

 @warning Not an equivalence relation: transitivity fails near the tolerance,
 so it cannot key a hashed or ordered container.

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

} // namespace oxygen::data
