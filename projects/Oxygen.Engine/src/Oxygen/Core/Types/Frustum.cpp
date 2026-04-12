//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>

#include <Oxygen/Core/Types/Frustum.h>

namespace oxygen {

namespace {
  constexpr int kLeft = 0;
  constexpr int kRight = 1;
  constexpr int kBottom = 2;
  constexpr int kTop = 3;
  constexpr int kNear = 4;
  constexpr int kFar = 5;

  inline auto NormalizePlane(Frustum::Plane& p) noexcept -> void
  {
    const float len = std::sqrt(glm::dot(p.normal, p.normal));
    if (len > 0.0F) {
      const float inv = 1.0F / len;
      p.normal *= inv;
      p.d *= inv;
    }
  }

  [[nodiscard]] auto MakePlane(const glm::vec3& a, const glm::vec3& b,
    const glm::vec3& c, const glm::vec3& inside_point) noexcept
    -> Frustum::Plane
  {
    auto plane = Frustum::Plane {};
    plane.normal = glm::normalize(glm::cross(b - a, c - a));
    plane.d = -glm::dot(plane.normal, a);

    // Orient every plane so the frustum center stays on the positive side.
    if ((glm::dot(plane.normal, inside_point) + plane.d) < 0.0F) {
      plane.normal = -plane.normal;
      plane.d = -plane.d;
    }

    return plane;
  }
} // namespace

auto Frustum::FromViewProj(const glm::mat4& vp, bool reverse_z) -> Frustum
{
  Frustum f {};
  const auto inv_vp = glm::inverse(vp);
  const float clip_near = reverse_z ? 1.0F : 0.0F;
  const float clip_far = reverse_z ? 0.0F : 1.0F;

  const auto Unproject = [&inv_vp](const float x, const float y,
                           const float z) noexcept {
    const auto clip = glm::vec4(x, y, z, 1.0F);
    const auto world = inv_vp * clip;
    return glm::vec3(world) / world.w;
  };

  const auto ntl = Unproject(-1.0F, 1.0F, clip_near);
  const auto ntr = Unproject(1.0F, 1.0F, clip_near);
  const auto nbl = Unproject(-1.0F, -1.0F, clip_near);
  const auto nbr = Unproject(1.0F, -1.0F, clip_near);
  const auto ftl = Unproject(-1.0F, 1.0F, clip_far);
  const auto ftr = Unproject(1.0F, 1.0F, clip_far);
  const auto fbl = Unproject(-1.0F, -1.0F, clip_far);
  const auto fbr = Unproject(1.0F, -1.0F, clip_far);

  const auto inside_point
    = (ntl + ntr + nbl + nbr + ftl + ftr + fbl + fbr) / 8.0F;

  f.planes[kLeft] = MakePlane(nbl, ntl, ftl, inside_point);
  f.planes[kRight] = MakePlane(ntr, nbr, fbr, inside_point);
  f.planes[kBottom] = MakePlane(nbr, nbl, fbl, inside_point);
  f.planes[kTop] = MakePlane(ntl, ntr, ftr, inside_point);
  f.planes[kNear] = MakePlane(nbl, nbr, ntr, inside_point);
  f.planes[kFar] = MakePlane(fbr, fbl, ftl, inside_point);

  return f;
}

auto Frustum::IntersectsAABB(const glm::vec3& bmin, const glm::vec3& bmax) const
  -> bool
{
  // For each plane, compute the most positive vertex (p-vertex) in the
  // direction of the plane normal. If that vertex is behind the plane, the
  // AABB is fully outside.
  for (const auto& p : planes) {
    glm::vec3 v;
    v.x = p.normal.x >= 0.0F ? bmax.x : bmin.x;
    v.y = p.normal.y >= 0.0F ? bmax.y : bmin.y;
    v.z = p.normal.z >= 0.0F ? bmax.z : bmin.z;
    const float dist = glm::dot(p.normal, v) + p.d;
    if (dist < 0.0F) {
      return false; // outside
    }
  }
  return true;
}

auto Frustum::IntersectsSphere(const glm::vec3& center, float radius) const
  -> bool
{
  // Sphere intersects frustum if it's not completely behind any plane.
  return std::ranges::all_of(planes, [&](const Plane& p) {
    const float dist = glm::dot(p.normal, center) + p.d;
    return dist >= -radius;
  });
}

} // namespace oxygen
