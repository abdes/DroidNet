//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <memory>
#include <optional>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Vortex/Types/DrawMetadata.h>

namespace oxygen::vortex {

//! CPU source dependency before GPU instancing combines separate casters.
struct ShadowCasterSource {
  DrawMetadata draw {};
  glm::vec4 bounds { 0.0F };
  scene::NodeHandle node;
  data::AssetKey geometry_asset_key;
  std::uint32_t lod_index { 0U };
  std::uint32_t geometry_generation { 0U };
  std::uint64_t geometry_content_revision { 0U };
  std::uint32_t material_generation { 0U };
};

//! Exact immutable inputs affecting masked depth coverage.
struct ShadowMaskedCasterRecord {
  std::uint32_t material_generation { 0 };
  std::uint32_t flags { 0 };
  float alpha { 1 };
  float cutoff { 0.5F };
  ShaderVisibleIndex texture { kInvalidShaderVisibleIndex };
  glm::vec2 uv_scale { 1 };
  glm::vec2 uv_offset { 0 };
  float uv_rotation { 0 };
  std::uint32_t uv_set { 0 };
  std::uint64_t texture_revision { 0 };
  auto operator==(const ShadowMaskedCasterRecord&) const -> bool = default;
};

//! Canonical producer inputs; view/selection/placement indices are excluded.
struct ShadowCasterRecord {
  scene::NodeHandle node;
  data::AssetKey geometry;
  std::uint32_t lod { 0 };
  std::uint32_t geometry_generation { 0 };
  std::uint64_t geometry_revision { 0 };
  ShaderVisibleIndex vertices { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex indices { kInvalidShaderVisibleIndex };
  std::uint32_t first_index { 0 };
  std::int32_t base_vertex { 0 };
  std::uint32_t indexed { 0 };
  std::uint32_t index_count { 0 };
  std::uint32_t vertex_count { 0 };
  std::uint32_t transform_generation { 0 };
  std::uint32_t submesh { 0 };
  std::array<float, 16> world {};
  bool double_sided { false };
  bool reverse_winding { false };
  std::optional<ShadowMaskedCasterRecord> masked;
  auto operator==(const ShadowCasterRecord&) const -> bool = default;
};

struct ShadowCasterDependency {
  glm::vec4 bounds { 0.0F };
  std::uint64_t fingerprint { 0U };
  bool reusable { false };
  std::shared_ptr<const ShadowCasterRecord> record;
};

} // namespace oxygen::vortex
