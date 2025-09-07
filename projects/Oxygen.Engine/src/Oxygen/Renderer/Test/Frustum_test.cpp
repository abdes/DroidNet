//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/Frustum.h>

using oxygen::Frustum;

namespace {

// Simple perspective matrix helper (D3D-style depth 0..1 not required here)
static auto MakePerspective(float fov_y_deg, float aspect, float zn, float zf)
  -> glm::mat4
{
  const float fov_y = glm::radians(fov_y_deg);
  return glm::perspective(fov_y, aspect, zn, zf);
}

NOLINT_TEST(Frustum_BasicTest, ExtractPlanes_And_IntersectAabb)
{
  // Arrange: view = identity, proj = perspective, no reverse-Z
  const glm::mat4 view(1.0F);
  const glm::mat4 proj = MakePerspective(60.0F, 1.0F, 0.1F, 100.0F);
  const glm::mat4 vp = proj * view;

  // Act
  const auto fr = Frustum::FromViewProj(vp, /*reverse_z*/ false);

  // Assert: AABB at origin of size 1 should be inside
  const glm::vec3 bmin(-0.5F);
  const glm::vec3 bmax(+0.5F);
  EXPECT_TRUE(fr.IntersectsAABB(bmin, bmax));

  // AABB far away beyond far plane should be outside
  const glm::vec3 bmin_far(0.0F, 0.0F, -200.0F);
  const glm::vec3 bmax_far(1.0F, 1.0F, -150.0F);
  EXPECT_FALSE(fr.IntersectsAABB(bmin_far, bmax_far));
}

NOLINT_TEST(Frustum_BasicTest, IntersectSphere_And_ReverseZ)
{
  // Arrange: view = translate back to z=+5 looking at origin
  const glm::mat4 view
    = glm::lookAtRH(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));

  // Normal Z (near<far)
  const glm::mat4 proj = MakePerspective(70.0F, 16.0F / 9.0F, 0.1F, 50.0F);
  const auto fr_n = Frustum::FromViewProj(proj * view, /*reverse_z*/ false);

  // Sphere at origin radius 0.5 visible
  EXPECT_TRUE(fr_n.IntersectsSphere(glm::vec3(0), 0.5F));
  // Sphere far beyond far plane should be culled
  EXPECT_FALSE(fr_n.IntersectsSphere(glm::vec3(0, 0, -60), 1.0F));

  // Reverse-Z: swap near/far plane meaning; choose large far and near ~1e-2
  const glm::mat4 proj_r = MakePerspective(70.0F, 16.0F / 9.0F, 0.01F, 1000.0F);
  const auto fr_r = Frustum::FromViewProj(proj_r * view, /*reverse_z*/ true);

  // Same origin sphere remains visible; a sphere extremely far in front of the
  // camera should be rejected by the far plane.
  EXPECT_TRUE(fr_r.IntersectsSphere(glm::vec3(0), 0.5F));
  EXPECT_FALSE(fr_r.IntersectsSphere(glm::vec3(0, 0, -50000.0F), 1.0F));
}

} // namespace
