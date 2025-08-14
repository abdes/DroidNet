//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>

#include <glm/glm.hpp>

#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine {

//! View frustum defined by 6 planes with outward normals.
/*!
 Extracted from a view-projection matrix. Supports intersection tests with
 axis-aligned bounding boxes and bounding spheres.

 ### Notes
 - Follow Gribb & Hartmann plane extraction.
 - For reverse-Z, near/far planes are swapped.

 @see View
 */
struct Frustum {
  //! Plane equation in the form ax + by + cz + d = 0, normal points outward.
  struct Plane {
    glm::vec3 normal { 0.0F, 0.0F, 1.0F };
    float d = 0.0F;
  };

  // Order: left, right, bottom, top, near, far
  static constexpr int kPlaneCount = 6;
  std::array<Plane, kPlaneCount> planes {};

  //! Build a frustum from a view-projection matrix.
  OXGN_RNDR_NDAPI static auto FromViewProj(
    const glm::mat4& view_proj, bool reverse_z) -> Frustum;

  //! Test intersection with an axis-aligned bounding box (world space).
  OXGN_RNDR_NDAPI auto IntersectsAABB(
    const glm::vec3& bmin, const glm::vec3& bmax) const -> bool;

  //! Test intersection with a sphere (world space).
  OXGN_RNDR_NDAPI auto IntersectsSphere(
    const glm::vec3& center, float radius) const -> bool;
};

} // namespace oxygen::engine
