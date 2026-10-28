//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Transforms/NodeFrame.h>

namespace oxygen::transforms {

namespace {
  // Below this length a direction has no usable orientation.
  constexpr float kMinDirectionLength = 1e-6F;
} // namespace

auto LookRotation(const LookDirection& direction) noexcept -> Quat
{
  const auto& forward = direction.forward;
  const auto& up = direction.up;
  const auto forward_length = glm::length(forward);
  const Vec3 f = forward_length > kMinDirectionLength ? forward / forward_length
                                                      : space::node::Forward;

  auto right = glm::cross(f, up);
  if (glm::length(right) <= kMinDirectionLength) {
    right = glm::cross(f, space::move::Up);
  }
  if (glm::length(right) <= kMinDirectionLength) {
    right = glm::cross(f, space::move::Back);
  }
  right = glm::normalize(right);
  const auto u = glm::cross(right, f);

  // Columns are the images of local +X, +Y and +Z. The node frame's right,
  // forward and up are -X, -Y and +Z.
  const Mat3 basis { -right, -f, u };
  return glm::normalize(glm::quat_cast(basis));
}

auto NodeViewMatrix(const Vec3& position, const Quat& rotation) noexcept -> Mat4
{
  const Vec3 forward = rotation * space::node::Forward;
  const Vec3 up = rotation * space::node::Up;
  return glm::lookAtRH(position, position + forward, up);
}

} // namespace oxygen::transforms
