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
  // Gribb & Hartmann plane extraction from row-major glm::mat4 (column-major
  // storage, but indices refer to elements as m[col][row]). We'll write in
  // terms of matrix elements for clarity.
  const float m00 = vp[0][0];
  const float m01 = vp[1][0];
  const float m02 = vp[2][0];
  const float m03 = vp[3][0];
  const float m10 = vp[0][1];
  const float m11 = vp[1][1];
  const float m12 = vp[2][1];
  const float m13 = vp[3][1];
  const float m20 = vp[0][2];
  const float m21 = vp[1][2];
  const float m22 = vp[2][2];
  const float m23 = vp[3][2];
  const float m30 = vp[0][3];
  const float m31 = vp[1][3];
  const float m32 = vp[2][3];
  const float m33 = vp[3][3];

  // Left:  m3 + m0
  f.planes[kLeft].normal = { m03 + m00, m13 + m10, m23 + m20 };
  f.planes[kLeft].d = m33 + m30;

  // Right: m3 - m0
  f.planes[kRight].normal = { m03 - m00, m13 - m10, m23 - m20 };
  f.planes[kRight].d = m33 - m30;

  // Bottom: m3 + m1
  f.planes[kBottom].normal = { m03 + m01, m13 + m11, m23 + m21 };
  f.planes[kBottom].d = m33 + m31;

  // Top: m3 - m1
  f.planes[kTop].normal = { m03 - m01, m13 - m11, m23 - m21 };
  f.planes[kTop].d = m33 - m31;

  // Near/Far: handle reverse-Z swap
  if (!reverse_z) {
    // Near: m3 + m2
    f.planes[kNear].normal = { m03 + m02, m13 + m12, m23 + m22 };
    f.planes[kNear].d = m33 + m32;

    // Far: m3 - m2
    f.planes[kFar].normal = { m03 - m02, m13 - m12, m23 - m22 };
    f.planes[kFar].d = m33 - m32;
  } else {
    // Reverse-Z: swap meaning
    f.planes[kNear].normal = { m03 - m02, m13 - m12, m23 - m22 };
    f.planes[kNear].d = m33 - m32;

    f.planes[kFar].normal = { m03 + m02, m13 + m12, m23 + m22 };
    f.planes[kFar].d = m33 + m32;
  }

  for (auto& p : f.planes) {
    NormalizePlane(p);
  }
  return f;
}

auto Frustum::IntersectsAABB(const glm::vec3& bmin, const glm::vec3& bmax) const
  -> bool
{
  // For each plane, compute the most negative vertex (n-vertex). If outside,
  // the AABB is fully outside.
  for (const auto& p : planes) {
    glm::vec3 v;
    v.x = p.normal.x >= 0.0F ? bmin.x : bmax.x;
    v.y = p.normal.y >= 0.0F ? bmin.y : bmax.y;
    v.z = p.normal.z >= 0.0F ? bmin.z : bmax.z;
    const float dist = glm::dot(p.normal, v) + p.d;
    if (dist > 0.0F) {
      return false; // outside
    }
  }
  return true;
}

auto Frustum::IntersectsSphere(const glm::vec3& center, float radius) const
  -> bool
{
  return std::ranges::all_of(planes, [&](const Plane& p) {
    const float dist = glm::dot(p.normal, center) + p.d;
    return dist <= radius;
  });
}

} // namespace oxygen::engine
