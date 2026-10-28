//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

#include <Oxygen/Data/Procedural/Revolution.h>
#include <Oxygen/Data/Procedural/SurfaceLayout.h>
#include <Oxygen/Data/ProceduralMeshDefaults.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/Vertex.h>

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
 It follows the revolution layout of `space::texture`: the seam at the back,
 u counter-clockwise seen from +Z and v the normalized meridian arc length
 from the +Z pole, so a texture reads upright and unmirrored from outside.
 Each pole has one vertex per segment at the segment's middle u. Tangent
 frames are orthonormal, including at the poles. Bounded segment counts limit
 allocation and keep all vertex/index counts representable by the native mesh
 format.

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

  constexpr auto pi = std::numbers::pi_v<double>;
  const auto cylinder_length = 2.0 * half_cylinder;
  const auto meridian_length = (pi * radius) + cylinder_length;
  std::vector<procedural::Latitude> latitudes;
  latitudes.reserve(row_count);
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
    latitudes.push_back(procedural::Latitude {
      .ring_radius = radius * sin_angle,
      .z = z,
      .sin_theta = sin_angle,
      .cos_theta = cos_angle,
      .v = static_cast<float>(distance / meridian_length),
    });
  }

  // Tiny radii can collapse different longitudes too: the builder rejects any
  // triangle that no longer faces outward in vertex storage.
  auto buffers = procedural::MeshBuffers {};
  if (!procedural::AppendLatitudeSurface(buffers, latitudes, radial_segments)) {
    return std::nullopt;
  }
  return std::make_pair(
    std::move(buffers.vertices), std::move(buffers.indices));
}
