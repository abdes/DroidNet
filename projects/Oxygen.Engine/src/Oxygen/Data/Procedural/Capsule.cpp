//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <numbers>

#include <Oxygen/Data/ProceduralMeshes.h>

/*!
 Generates a capsule centred at the origin, with its long axis along Z.
 Smooth hemispheres meet a cylinder at z = +/-(height/2 - radius). Longitude
 seam and pole vertices are duplicated for UVs; all emitted triangles have
 outward counter-clockwise winding and pole triangles are not degenerate.

 @param hemisphere_segments Pole-to-equator intervals in [1,64].
 @param radial_segments Intervals around the Z axis in [3,256].
 @param height Finite positive total height, including the hemispheres.
 @param radius Finite positive radius, no greater than height/2.
 @return Vertex/index buffers, or std::nullopt for invalid parameters or
 dimensions that collapse the sampled surface in float32 vertex storage.

 Height equal to the diameter produces a sphere with one shared equator.
 UVs use longitude and normalized meridian arc length. Tangent frames are
 orthonormal, including at the poles. Bounded segment counts limit allocation
 and keep all vertex/index counts representable by the native mesh format.

 @see GenerateMeshBuffers, MakeSphereMeshAsset
*/
auto oxygen::data::MakeCapsuleMeshAsset(const unsigned int hemisphere_segments,
  const unsigned int radial_segments, const float height, const float radius)
  -> std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>>
{
  const auto half_height = height / 2.0F;
  if (hemisphere_segments < 1
    || hemisphere_segments > procedural::kCapsuleMaxHemisphereSegments
    || radial_segments < 3
    || radial_segments > procedural::kCapsuleMaxRadialSegments
    || !std::isfinite(height) || !std::isfinite(radius) || height <= 0.0F
    || radius <= 0.0F || radius > half_height) {
    return std::nullopt;
  }

  const auto half_cylinder = half_height - radius;
  const auto is_sphere = half_cylinder == 0.0F;
  const auto row_count = (2U * hemisphere_segments) + (is_sphere ? 1U : 2U);
  const auto stride = radial_segments + 1U;
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  vertices.reserve(static_cast<size_t>(row_count) * stride);
  constexpr auto kIndicesPerQuad = size_t { 3 } * 2U;
  indices.reserve(kIndicesPerQuad * radial_segments * (row_count - 2U));

  constexpr auto pi = std::numbers::pi_v<double>;
  const auto cylinder_length = 2.0 * half_cylinder;
  const auto meridian_length = (pi * radius) + cylinder_length;
  auto previous_z = half_height;
  for (auto row = 0U; row < row_count; ++row) {
    const auto upper = row <= hemisphere_segments;
    const auto step
      = upper ? row : row - hemisphere_segments - (is_sphere ? 0U : 1U);
    const auto angle = (upper ? 0.0 : pi / 2.0)
      + ((pi / 2.0 * static_cast<double>(step)) / hemisphere_segments);
    const auto pole = row == 0U || row == row_count - 1U;
    const auto equator = upper ? step == hemisphere_segments : step == 0U;
    const auto sign = upper ? 1.0F : -1.0F;
    auto sin_angle = static_cast<float>(std::sin(angle));
    auto cos_angle = static_cast<float>(std::cos(angle));
    if (pole) {
      sin_angle = 0.0F;
      cos_angle = sign;
    } else if (equator) {
      sin_angle = 1.0F;
      cos_angle = 0.0F;
    }
    const auto center_z = upper ? half_cylinder : -half_cylinder;
    const auto z = pole ? sign * half_height : center_z + (radius * cos_angle);
    // Even finite dimensions can erase a hemisphere at a much larger height.
    // Every latitude must remain distinct after conversion to vertex storage.
    if (row != 0U && z >= previous_z) {
      return std::nullopt;
    }
    previous_z = z;
    const auto distance = (radius * angle) + (upper ? 0.0 : cylinder_length);
    const auto v = static_cast<float>(1.0 - (distance / meridian_length));

    for (auto longitude = 0U; longitude <= radial_segments; ++longitude) {
      // Close the seam exactly, while retaining the distinct u = 1 vertex.
      const auto phi = longitude == radial_segments
        ? 0.0
        : 2.0 * pi * static_cast<double>(longitude) / radial_segments;
      const auto cos_phi = static_cast<float>(std::cos(phi));
      const auto sin_phi = static_cast<float>(std::sin(phi));
      const auto normal
        = glm::vec3(sin_angle * cos_phi, sin_angle * sin_phi, cos_angle);
      const auto tangent = glm::vec3(-sin_phi, cos_phi, 0.0F);
      vertices.push_back(Vertex {
        .position = { radius * normal.x, radius * normal.y, z },
        .normal = normal,
        .texcoord
        = { static_cast<float>(longitude) / static_cast<float>(radial_segments),
          v },
        .tangent = tangent,
        .bitangent = glm::cross(normal, tangent),
        .color = { 1, 1, 1, 1 },
      });
    }
  }

  const auto append_triangle
    = [&](const uint32_t a, const uint32_t b, const uint32_t c) -> bool {
    const auto first = glm::dvec3(vertices.at(a).position);
    const auto second = glm::dvec3(vertices.at(b).position);
    const auto third = glm::dvec3(vertices.at(c).position);
    const auto normal = glm::dvec3(
      vertices.at(a).normal + vertices.at(b).normal + vertices.at(c).normal);
    // Test stored positions, using double to avoid overflow/underflow in the
    // validation itself. Tiny radii can collapse different longitudes too.
    if (glm::dot(glm::cross(second - first, third - first), normal) <= 0.0) {
      return false;
    }
    indices.insert(indices.end(), { a, b, c });
    return true;
  };
  for (auto row = 0U; row + 1U < row_count; ++row) {
    for (auto longitude = 0U; longitude < radial_segments; ++longitude) {
      const auto top = (row * stride) + longitude;
      const auto bottom = top + stride;
      if (row != 0U && !append_triangle(top, bottom, top + 1U)) {
        return std::nullopt;
      }
      if (row + 2U != row_count
        && !append_triangle(top + 1U, bottom, bottom + 1U)) {
        return std::nullopt;
      }
    }
  }
  return { { std::move(vertices), std::move(indices) } };
}
