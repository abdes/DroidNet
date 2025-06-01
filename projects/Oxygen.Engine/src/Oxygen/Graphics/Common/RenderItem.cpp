//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/RenderItem.h>
#include <algorithm>
#include <limits>

namespace oxygen::graphics {

void RenderItem::CalculateBoundingSphere()
{
    if (vertices.empty() && simple_vertices.empty()) {
        bounding_sphere = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    glm::vec3 min_point(std::numeric_limits<float>::max());
    glm::vec3 max_point(std::numeric_limits<float>::lowest());

    // Calculate bounding box from vertices
    if (!vertices.empty()) {
        for (const auto& vertex : vertices) {
            glm::vec3 pos(vertex.position[0], vertex.position[1], vertex.position[2]);
            min_point = glm::min(min_point, pos);
            max_point = glm::max(max_point, pos);
        }
    } else {
        for (const auto& vertex : simple_vertices) {
            glm::vec3 pos(vertex.position[0], vertex.position[1], vertex.position[2]);
            min_point = glm::min(min_point, pos);
            max_point = glm::max(max_point, pos);
        }
    }

    // Calculate center and radius from bounding box
    glm::vec3 center = (min_point + max_point) * 0.5f;
    float radius = glm::length(max_point - center);

    bounding_sphere = glm::vec4(center, radius);
}

void RenderItem::UpdateNormalTransform()
{
    // Normal transform is the inverse transpose of the upper-left 3x3 of world transform
    // This ensures normals are transformed correctly under non-uniform scaling
    glm::mat3 world_3x3 = glm::mat3(world_transform);
    glm::mat3 normal_3x3 = glm::transpose(glm::inverse(world_3x3));

    // Convert back to 4x4 matrix (normal transform only needs upper-left 3x3)
    normal_transform = glm::mat4(normal_3x3);
    normal_transform[3][3] = 1.0f; // Ensure homogeneous coordinate is 1
}

} // namespace oxygen::graphics
