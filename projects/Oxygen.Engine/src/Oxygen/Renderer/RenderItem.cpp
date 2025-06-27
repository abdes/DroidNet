//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <limits>

#include <Oxygen/Data/MeshAsset.h>
#include <Oxygen/Renderer/RenderItem.h>

using oxygen::engine::RenderItem;

namespace {

// Compute the transformed bounding sphere in world space
auto ComputeTransformedBoundingSphere(const RenderItem& item) -> glm::vec4
{
  if (!item.mesh) {
    return RenderItem::kDefaultBoundingSphere;
  }
  const glm::vec4& local_sphere = item.mesh->BoundingSphere();
  auto center_ws = glm::vec3(item.world_transform
    * glm::vec4(local_sphere.x, local_sphere.y, local_sphere.z, 1.0f));
  float max_scale = std::max({ glm::length(glm::vec3(item.world_transform[0])),
    glm::length(glm::vec3(item.world_transform[1])),
    glm::length(glm::vec3(item.world_transform[2])) });
  float radius_ws = local_sphere.w * max_scale;
  return glm::vec4(center_ws, radius_ws);
}

auto ComputeTransformedBoundingBoxMin(const RenderItem& item) -> glm::vec3
{
  if (!item.mesh) {
    return glm::vec3(0.0f);
  }
  const glm::vec3& bb_min = item.mesh->BoundingBoxMin();
  const glm::vec3& bb_max = item.mesh->BoundingBoxMax();
  glm::vec3 local_corners[8] = {
    { bb_min.x, bb_min.y, bb_min.z },
    { bb_max.x, bb_min.y, bb_min.z },
    { bb_min.x, bb_max.y, bb_min.z },
    { bb_max.x, bb_max.y, bb_min.z },
    { bb_min.x, bb_min.y, bb_max.z },
    { bb_max.x, bb_min.y, bb_max.z },
    { bb_min.x, bb_max.y, bb_max.z },
    { bb_max.x, bb_max.y, bb_max.z },
  };
  glm::vec3 min_ws(std::numeric_limits<float>::max());
  for (int i = 0; i < 8; ++i) {
    auto ws
      = glm::vec3(item.world_transform * glm::vec4(local_corners[i], 1.0f));
    min_ws = glm::min(min_ws, ws);
  }
  return min_ws;
}

auto ComputeTransformedBoundingBoxMax(const RenderItem& item) -> glm::vec3
{
  if (!item.mesh) {
    return glm::vec3(0.0f);
  }
  const glm::vec3& bb_min = item.mesh->BoundingBoxMin();
  const glm::vec3& bb_max = item.mesh->BoundingBoxMax();
  glm::vec3 local_corners[8] = {
    { bb_min.x, bb_min.y, bb_min.z },
    { bb_max.x, bb_min.y, bb_min.z },
    { bb_min.x, bb_max.y, bb_min.z },
    { bb_max.x, bb_max.y, bb_min.z },
    { bb_min.x, bb_min.y, bb_max.z },
    { bb_max.x, bb_min.y, bb_max.z },
    { bb_min.x, bb_max.y, bb_max.z },
    { bb_max.x, bb_max.y, bb_max.z },
  };
  glm::vec3 max_ws(std::numeric_limits<float>::lowest());
  for (int i = 0; i < 8; ++i) {
    auto ws
      = glm::vec3(item.world_transform * glm::vec4(local_corners[i], 1.0f));
    max_ws = glm::max(max_ws, ws);
  }
  return max_ws;
}

auto UpdateNormalTransform(RenderItem& item) -> void
{
  auto world_3x3 = glm::mat3(item.world_transform);
  glm::mat3 normal_3x3 = glm::transpose(glm::inverse(world_3x3));
  item.normal_transform = glm::mat4(normal_3x3);
  item.normal_transform[3][3] = 1.0f;
}

} // anonymous namespace

auto RenderItem::UpdatedTransformedProperties() -> void
{
  bounding_sphere = ComputeTransformedBoundingSphere(*this);
  bounding_box_min = ComputeTransformedBoundingBoxMin(*this);
  bounding_box_max = ComputeTransformedBoundingBoxMax(*this);
  UpdateNormalTransform(*this);
}

auto RenderItem::UpdateComputedProperties() -> void
{
  // For now, same as UpdatedTransformedProperties, but can be extended for more
  // expensive/derived data
  UpdatedTransformedProperties();
}
