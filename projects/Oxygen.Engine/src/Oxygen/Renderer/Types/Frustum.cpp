//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>

#include <Oxygen/Renderer/Types/Frustum.h>

namespace oxygen::engine {

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
} // namespace

auto Frustum::FromViewProj(const glm::mat4& vp, bool reverse_z) -> Frustum
{
  Frustum f {};
  // Gribb & Hartmann plane extraction. GLM stores matrices column-major and
  // uses indexing m[col][row]. Build explicit row vectors first, then form
  // planes as r3 +/- r{0,1,2}.
  const glm::vec4 r0 { vp[0][0], vp[1][0], vp[2][0], vp[3][0] };
  const glm::vec4 r1 { vp[0][1], vp[1][1], vp[2][1], vp[3][1] };
  const glm::vec4 r2 { vp[0][2], vp[1][2], vp[2][2], vp[3][2] };
  const glm::vec4 r3 { vp[0][3], vp[1][3], vp[2][3], vp[3][3] };

  // Left:  r3 + r0
  {
    const glm::vec4 p = r3 + r0;
    f.planes[kLeft].normal = glm::vec3(p);
    f.planes[kLeft].d = p.w;
  }
  // Right: r3 - r0
  {
    const glm::vec4 p = r3 - r0;
    f.planes[kRight].normal = glm::vec3(p);
    f.planes[kRight].d = p.w;
  }
  // Bottom: r3 + r1
  {
    const glm::vec4 p = r3 + r1;
    f.planes[kBottom].normal = glm::vec3(p);
    f.planes[kBottom].d = p.w;
  }
  // Top: r3 - r1
  {
    const glm::vec4 p = r3 - r1;
    f.planes[kTop].normal = glm::vec3(p);
    f.planes[kTop].d = p.w;
  }
  // Near/Far: handle reverse-Z swap
  if (!reverse_z) {
    // Near: r3 + r2
    {
      const glm::vec4 p = r3 + r2;
      f.planes[kNear].normal = glm::vec3(p);
      f.planes[kNear].d = p.w;
    }
    // Far: r3 - r2
    {
      const glm::vec4 p = r3 - r2;
      f.planes[kFar].normal = glm::vec3(p);
      f.planes[kFar].d = p.w;
    }
  } else {
    // Reverse-Z: swap meaning
    {
      const glm::vec4 p = r3 - r2; // near
      f.planes[kNear].normal = glm::vec3(p);
      f.planes[kNear].d = p.w;
    }
    {
      const glm::vec4 p = r3 + r2; // far
      f.planes[kFar].normal = glm::vec3(p);
      f.planes[kFar].d = p.w;
    }
  }

  for (auto& p : f.planes) {
    NormalizePlane(p);
  }
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

} // namespace oxygen::engine
