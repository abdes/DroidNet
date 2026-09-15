//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Base/Hash.h>

namespace oxygen::content::import::internal {

inline constexpr auto kNoBakeIndex = std::numeric_limits<size_t>::max();

//! Source-node facts shared by geometry emission and scene construction.
struct MeshBakeNode {
  size_t mesh_index = kNoBakeIndex;
  size_t material_binding = 0;
  glm::mat4 local_transform { 1.0F };
  std::string retain_reason;
};

//! One geometry identity, optionally with a static local transform baked in.
struct MeshBakeVariant {
  size_t mesh_index = 0;
  size_t representative_node = kNoBakeIndex;
  size_t material_binding = 0;
  std::optional<glm::mat4> transform;
  std::string name_suffix;
};

struct MeshBakePlan {
  std::vector<MeshBakeVariant> variants;
  std::vector<size_t> node_variant;
  std::vector<std::string> retained_reasons;
};

struct MeshBakeVariantKey {
  size_t material_binding = 0;
  std::optional<glm::mat4> transform;
  auto operator==(const MeshBakeVariantKey&) const -> bool = default;
};

struct MeshBakeVariantKeyHash {
  auto operator()(const MeshBakeVariantKey& key) const -> size_t
  {
    size_t hash = 0;
    HashCombine(hash, key.material_binding);
    HashCombine(hash, key.transform.has_value());
    if (key.transform) {
      for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
          HashCombine(hash, (*key.transform)[column][row]);
        }
      }
    }
    return hash;
  }
};

//! Preserve non-invertible transforms on nodes rather than baking invalid
//! normals.
inline auto IsBakeTransformInvertible(const glm::mat4& transform) -> bool
{
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      if (!std::isfinite(transform[column][row])) {
        return false;
      }
    }
  }
  const auto determinant = glm::determinant(glm::mat3(transform));
  if (!std::isfinite(determinant) || determinant == 0.0F) {
    return false;
  }
  const auto inverse = glm::inverse(glm::mat3(transform));
  for (int column = 0; column < 3; ++column) {
    for (int row = 0; row < 3; ++row) {
      if (!std::isfinite(inverse[column][row])) {
        return false;
      }
    }
  }
  return true;
}

//! Build deterministic variants without changing shared source geometry.
inline auto BuildMeshBakePlan(const std::span<const MeshBakeNode> nodes,
  const std::span<const uint8_t> emit_mesh, const bool bake) -> MeshBakePlan
{
  MeshBakePlan plan;
  plan.node_variant.resize(nodes.size(), kNoBakeIndex);
  plan.retained_reasons.resize(nodes.size());
  std::vector<std::vector<size_t>> references(emit_mesh.size());
  for (size_t index = 0; index < nodes.size(); ++index) {
    if (nodes[index].mesh_index < references.size()) {
      references[nodes[index].mesh_index].push_back(index);
    }
  }
  for (size_t mesh = 0; mesh < references.size(); ++mesh) {
    if (emit_mesh[mesh] == 0) {
      continue;
    }
    std::unordered_map<MeshBakeVariantKey, size_t, MeshBakeVariantKeyHash>
      variants;
    variants.reserve(references[mesh].size());
    bool has_unbaked_name = false;
    for (const auto node_index : references[mesh]) {
      const auto& node = nodes[node_index];
      std::optional<glm::mat4> transform;
      if (bake && node.local_transform != glm::mat4(1.0F)) {
        if (!node.retain_reason.empty()) {
          plan.retained_reasons[node_index] = node.retain_reason;
        } else if (!IsBakeTransformInvertible(node.local_transform)) {
          plan.retained_reasons[node_index] = "non-invertible transform";
        } else {
          transform = node.local_transform;
        }
      }
      const MeshBakeVariantKey key { node.material_binding, transform };
      const auto match = variants.find(key);
      if (match != variants.end()) {
        plan.node_variant[node_index] = match->second;
        continue;
      }
      std::string suffix;
      if (transform) {
        suffix = "__baked_" + std::to_string(node_index);
      } else if (has_unbaked_name) {
        suffix = "__material_" + std::to_string(node_index);
      } else {
        has_unbaked_name = true;
      }
      plan.node_variant[node_index] = plan.variants.size();
      variants.emplace(key, plan.variants.size());
      plan.variants.push_back(MeshBakeVariant {
        .mesh_index = mesh,
        .representative_node = node_index,
        .material_binding = node.material_binding,
        .transform = transform,
        .name_suffix = std::move(suffix),
      });
    }
    if (references[mesh].empty()) {
      plan.variants.push_back(MeshBakeVariant { .mesh_index = mesh });
    }
  }
  return plan;
}

} // namespace oxygen::content::import::internal
