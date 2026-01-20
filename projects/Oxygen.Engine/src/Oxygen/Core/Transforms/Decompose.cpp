//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#define GLM_ENABLE_EXPERIMENTAL
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <Oxygen/Core/Transforms/Decompose.h>

namespace oxygen::transforms {

auto TryDecomposeTransform(const glm::mat4& transform, glm::vec3& translation,
  glm::quat& rotation, glm::vec3& scale) -> bool
{
  if (!IsFinite(transform)) {
    return false;
  }

  glm::vec3 skew {};
  glm::vec4 perspective {};
  auto rotation_out = glm::quat { 1.0F, 0.0F, 0.0F, 0.0F };
  auto scale_out = glm::vec3 { 1.0F, 1.0F, 1.0F };
  auto translation_out = glm::vec3 { 0.0F, 0.0F, 0.0F };

  const bool decomposed = glm::decompose(
    transform, scale_out, rotation_out, translation_out, skew, perspective);
  if (!decomposed) {
    return false;
  }

  rotation_out = glm::normalize(rotation_out);
  if (!IsFinite(translation_out) || !IsFinite(rotation_out)
    || !IsFinite(scale_out)) {
    return false;
  }

  translation = translation_out;
  rotation = rotation_out;
  scale = scale_out;
  return true;
}

auto DecomposeTransformOrFallback(const glm::mat4& transform,
  glm::vec3& translation, glm::quat& rotation, glm::vec3& scale) -> bool
{
  if (TryDecomposeTransform(transform, translation, rotation, scale)) {
    return false;
  }

  if (!IsFinite(transform)) {
    translation = glm::vec3 { 0.0F, 0.0F, 0.0F };
    rotation = glm::quat { 1.0F, 0.0F, 0.0F, 0.0F };
    scale = glm::vec3 { 1.0F, 1.0F, 1.0F };
    return true;
  }

  translation = glm::vec3(transform[3]);

  const glm::vec3 basis_x { transform[0] };
  const glm::vec3 basis_y { transform[1] };
  const glm::vec3 basis_z { transform[2] };
  scale = glm::vec3 { glm::length(basis_x), glm::length(basis_y),
    glm::length(basis_z) };

  if (!IsFinite(translation) || !IsFinite(scale)) {
    translation = glm::vec3 { 0.0F, 0.0F, 0.0F };
    rotation = glm::quat { 1.0F, 0.0F, 0.0F, 0.0F };
    scale = glm::vec3 { 1.0F, 1.0F, 1.0F };
    return true;
  }

  constexpr float kMinScale = 1e-6F;
  if (scale.x < kMinScale || scale.y < kMinScale || scale.z < kMinScale) {
    rotation = glm::quat { 1.0F, 0.0F, 0.0F, 0.0F };
    return false;
  }

  glm::mat3 rotation_basis {};
  rotation_basis[0] = basis_x / scale.x;
  rotation_basis[1] = basis_y / scale.y;
  rotation_basis[2] = basis_z / scale.z;
  rotation = glm::normalize(glm::quat_cast(rotation_basis));
  if (!IsFinite(rotation)) {
    rotation = glm::quat { 1.0F, 0.0F, 0.0F, 0.0F };
  }
  return true;
}

auto IsUniformScale(const glm::vec3& scale, const float epsilon) noexcept
  -> bool
{
  if (!IsFinite(scale)) {
    return false;
  }
  return std::abs(scale.x - scale.y) <= epsilon
    && std::abs(scale.y - scale.z) <= epsilon;
}

auto IsIdentityRotation(const glm::quat& rotation, const float epsilon) noexcept
  -> bool
{
  if (!IsFinite(rotation)) {
    return false;
  }

  const auto normalized = glm::normalize(rotation);
  const float vector_len = glm::length(glm::vec3 {
    normalized.x,
    normalized.y,
    normalized.z,
  });
  return vector_len <= epsilon && std::abs(normalized.w - 1.0F) <= epsilon;
}

} // namespace oxygen::transforms
