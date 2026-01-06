//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Data/Vertex.h>

namespace oxygen::content::import::util {

//! Generates tangent/bitangent vectors for mesh vertices.
/*!
 Uses a MikkTSpace-style algorithm to compute consistent TBN basis
 from triangles. Requires valid UVs and normals.

 @param vertices The vertex array (modified in place).
 @param buckets Array of index buckets, where each bucket contains
        triangle indices for a submesh.
*/
template <typename BucketT>
auto GenerateTangents(std::vector<oxygen::data::Vertex>& vertices,
  const std::span<const BucketT> buckets) -> void
{
  if (vertices.empty()) {
    return;
  }

  std::vector<glm::vec3> tan1(vertices.size(), glm::vec3(0.0F));
  std::vector<glm::vec3> tan2(vertices.size(), glm::vec3(0.0F));

  for (const auto& bucket : buckets) {
    const auto tri_count = bucket.indices.size() / 3;
    for (size_t tri = 0; tri < tri_count; ++tri) {
      const auto i0 = bucket.indices[tri * 3 + 0];
      const auto i1 = bucket.indices[tri * 3 + 1];
      const auto i2 = bucket.indices[tri * 3 + 2];
      if (i0 >= vertices.size() || i1 >= vertices.size()
        || i2 >= vertices.size()) {
        continue;
      }

      const auto& v0 = vertices[i0];
      const auto& v1 = vertices[i1];
      const auto& v2 = vertices[i2];

      const glm::vec3 p0 = v0.position;
      const glm::vec3 p1 = v1.position;
      const glm::vec3 p2 = v2.position;

      const glm::vec2 w0 = v0.texcoord;
      const glm::vec2 w1 = v1.texcoord;
      const glm::vec2 w2 = v2.texcoord;

      const glm::vec3 e1 = p1 - p0;
      const glm::vec3 e2 = p2 - p0;
      const glm::vec2 d1 = w1 - w0;
      const glm::vec2 d2 = w2 - w0;

      const float denom = d1.x * d2.y - d1.y * d2.x;
      if (std::abs(denom) < 1e-8F) {
        continue;
      }
      const float r = 1.0F / denom;

      const glm::vec3 t = (e1 * d2.y - e2 * d1.y) * r;
      const glm::vec3 b = (e2 * d1.x - e1 * d2.x) * r;

      tan1[i0] += t;
      tan1[i1] += t;
      tan1[i2] += t;

      tan2[i0] += b;
      tan2[i1] += b;
      tan2[i2] += b;
    }
  }

  for (size_t vi = 0; vi < vertices.size(); ++vi) {
    auto n = vertices[vi].normal;
    const auto n_len = glm::length(n);
    if (n_len > 1e-8F) {
      n /= n_len;
    } else {
      n = glm::vec3(0.0F, 1.0F, 0.0F);
    }

    glm::vec3 t = tan1[vi];
    if (glm::length(t) < 1e-8F) {
      continue;
    }

    // Gram-Schmidt orthonormalization
    t = glm::normalize(t - n * glm::dot(n, t));

    glm::vec3 b = glm::cross(n, t);
    if (glm::dot(b, tan2[vi]) < 0.0F) {
      b = -b;
    }
    b = glm::normalize(b);

    vertices[vi].normal = n;
    vertices[vi].tangent = t;
    vertices[vi].bitangent = b;
  }
}

} // namespace oxygen::content::import::util
