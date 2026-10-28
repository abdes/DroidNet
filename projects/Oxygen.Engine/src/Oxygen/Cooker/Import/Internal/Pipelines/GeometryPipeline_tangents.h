//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Data/Vertex.h>

namespace oxygen::content::import::util {

//! Smallest cosine between a frame axis and the UV derivative it follows: the
//! two lie within 45 degrees, the Tangent Frame Check debug mode's bound.
inline constexpr float kFrameAgreementCosine = 0.70710678F;

namespace detail {
  //! A triangle's dP/dU and dP/dV, the image's right and down over it, or
  //! nothing when its UVs or positions are degenerate.
  struct UvDerivatives final {
    glm::vec3 dpdu;
    glm::vec3 dpdv;
  };

  [[nodiscard]] inline auto UvDerivativesOf(
    const data::Vertex& v0, const data::Vertex& v1, const data::Vertex& v2)
    -> std::optional<UvDerivatives>
  {
    const glm::vec3 e1 = v1.position - v0.position;
    const glm::vec3 e2 = v2.position - v0.position;
    const glm::vec2 d1 = v1.texcoord - v0.texcoord;
    const glm::vec2 d2 = v2.texcoord - v0.texcoord;

    const float denom = (d1.x * d2.y) - (d1.y * d2.x);
    if (!std::isfinite(denom) || std::abs(denom) < 1e-8F) {
      return std::nullopt;
    }
    const float r = 1.0F / denom;
    return UvDerivatives {
      .dpdu = (e1 * d2.y - e2 * d1.y) * r,
      .dpdv = (e2 * d1.x - e1 * d2.x) * r,
    };
  }

} // namespace detail

//! Generates MikkTSpace tangent frames for triangle corners.
/*!
 Runs the reference MikkTSpace implementation, the basis bakers such as
 Blender, Substance and xNormal bake normal maps against and glTF prescribes
 for missing tangents. It welds the corners itself, splits them by
 handedness, and gives degenerate triangles their neighbors' frames.

 Fed V-down texture coordinates, its tangent runs along +dP/dU and its
 bitangent, sign * cross(N, T), along +dP/dV: the `space::texture` frame,
 B = -cross(N, T) * w with w = -sign.

 @param vertices The corners, each used by one triangle (modified in place).
 @param triangles Corner indices, three per triangle; triangles naming a
        corner outside `vertices` are skipped.
*/
auto GenerateMikkTSpaceTangents(std::vector<data::Vertex>& vertices,
  std::span<const uint32_t> triangles) -> void;

//! Generates tangent frames for the corners the buckets' triangles index.
/*!
 @param vertices The corners, each used by one triangle (modified in place).
 @param buckets Array of index buckets, where each bucket contains
        triangle indices for a submesh.
 @see GenerateMikkTSpaceTangents
*/
template <typename BucketT>
auto GenerateTangents(std::vector<data::Vertex>& vertices,
  const std::span<const BucketT> buckets) -> void
{
  auto triangles = std::vector<uint32_t> {};
  for (const auto& bucket : buckets) {
    const auto whole = bucket.indices.size() - (bucket.indices.size() % 3);
    triangles.insert(triangles.end(), bucket.indices.begin(),
      bucket.indices.begin() + static_cast<std::ptrdiff_t>(whole));
  }
  GenerateMikkTSpaceTangents(vertices, triangles);
}

//! The share of a mesh's triangles whose tangent frames follow their UVs.
/*!
 A triangle's frame follows its UVs when its corners' mean tangent lies within
 45 degrees of its dP/dU and their mean bitangent within 45 degrees of its
 dP/dV, as the Tangent Frame Check debug mode shows. Triangles with degenerate
 UVs or positions do not count; a mesh without any counts as agreeing.

 @param vertices The vertices the buckets index.
 @param buckets Array of index buckets, where each bucket contains
        triangle indices for a submesh.
 @return The agreeing share, in [0, 1].
*/
template <typename BucketT>
[[nodiscard]] auto TangentFrameAgreement(
  const std::span<const data::Vertex> vertices,
  const std::span<const BucketT> buckets) -> float
{
  const auto follows
    = [](const glm::vec3& axis, const glm::vec3& derivative) -> bool {
    const auto lengths = glm::length(axis) * glm::length(derivative);
    return lengths > 0.0F
      && glm::dot(axis, derivative) > kFrameAgreementCosine * lengths;
  };

  size_t counted = 0;
  size_t agreeing = 0;
  for (const auto& bucket : buckets) {
    const auto tri_count = bucket.indices.size() / 3;
    for (size_t tri = 0; tri < tri_count; ++tri) {
      const auto i0 = bucket.indices[(tri * 3) + 0];
      const auto i1 = bucket.indices[(tri * 3) + 1];
      const auto i2 = bucket.indices[(tri * 3) + 2];
      if (i0 >= vertices.size() || i1 >= vertices.size()
        || i2 >= vertices.size()) {
        continue;
      }
      const auto& v0 = vertices[i0];
      const auto& v1 = vertices[i1];
      const auto& v2 = vertices[i2];
      const auto derivatives = detail::UvDerivativesOf(v0, v1, v2);
      if (!derivatives.has_value()) {
        continue;
      }
      ++counted;
      if (follows(v0.tangent + v1.tangent + v2.tangent, derivatives->dpdu)
        && follows(
          v0.bitangent + v1.bitangent + v2.bitangent, derivatives->dpdv)) {
        ++agreeing;
      }
    }
  }
  return counted == 0
    ? 1.0F
    : static_cast<float>(agreeing) / static_cast<float>(counted);
}

} // namespace oxygen::content::import::util
