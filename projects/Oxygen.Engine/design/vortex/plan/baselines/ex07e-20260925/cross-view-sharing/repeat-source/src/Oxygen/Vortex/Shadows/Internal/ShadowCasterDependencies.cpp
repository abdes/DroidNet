//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Vortex/Internal/MeshRasterState.h>
#include <Oxygen/Vortex/Shadows/Internal/ShadowCasterDependencies.h>

namespace oxygen::vortex::shadows::internal {

auto ShadowCasterDependencies::Intern(ShadowCasterRecord record,
  std::uint64_t hash) -> std::shared_ptr<const ShadowCasterRecord>
{
  const auto [first, last] = records_.equal_range(hash);
  for (auto it = first; it != last; ++it) {
    if (auto existing = it->second.lock(); existing && *existing == record) {
      return existing;
    }
  }
  auto result = std::make_shared<const ShadowCasterRecord>(std::move(record));
  records_.emplace(hash, result);
  return result;
}
auto ShadowCasterDependencies::Prune() -> void
{
  std::erase_if(
    records_, [](const auto& entry) { return entry.second.expired(); });
}

auto ShadowCasterDependencies::Build(const PreparedSceneFrame& scene,
  std::vector<ShadowCasterDependency>& dependencies) -> void
{
  // Build transactionally while the preceding snapshot keeps equal records
  // alive. Alternate the two vectors so their capacities survive frame reuse.
  scratch_.clear();
  scratch_.reserve(scene.shadow_caster_sources.size());
  for (const auto& source : scene.shadow_caster_sources) {
    auto& dependency = scratch_.emplace_back();
    dependency.bounds = source.bounds;
    const auto& draw = source.draw;
    const auto transform_offset = std::size_t { draw.transform_index } * 16U;
    if (transform_offset + 16U > scene.world_matrices.size()
      || source.geometry_content_revision == 0U) {
      continue;
    }
    ShadowCasterRecord record {
      .node = source.node,
      .geometry = source.geometry_asset_key,
      .lod = source.lod_index,
      .geometry_generation = source.geometry_generation,
      .geometry_revision = source.geometry_content_revision,
      .vertices = draw.vertex_buffer_index,
      .indices = draw.index_buffer_index,
      .first_index = draw.first_index,
      .base_vertex = draw.base_vertex,
      .indexed = draw.is_indexed,
      .index_count = draw.index_count,
      .vertex_count = draw.vertex_count,
      .transform_generation = draw.transform_generation,
      .submesh = draw.submesh_index,
    };
    std::copy_n(
      scene.world_matrices.data() + transform_offset, 16, record.world.begin());
    if (!std::ranges::all_of(
          record.world, [](float value) { return std::isfinite(value); })) {
      continue;
    }
    auto hash = std::size_t { 0U };
    HashCombine(hash, source.node);
    HashCombine(hash, source.geometry_asset_key);
    HashCombine(hash, source.lod_index);
    HashCombine(hash, source.geometry_generation);
    HashCombine(hash, source.geometry_content_revision);
    HashCombine(hash, draw.vertex_buffer_index.get());
    HashCombine(hash, draw.index_buffer_index.get());
    HashCombine(hash, draw.first_index);
    HashCombine(hash, draw.base_vertex);
    HashCombine(hash, draw.is_indexed);
    HashCombine(hash, draw.index_count);
    HashCombine(hash, draw.vertex_count);
    HashCombine(hash, draw.transform_generation);
    HashCombine(hash, draw.submesh_index);
    HashCombine(hash,
      ComputeFNV1a64(
        scene.world_matrices.data() + transform_offset, 16U * sizeof(float)));
    const auto raster
      = vortex::internal::ResolveMeshRasterState({ &draw, 1U }, 0U);
    record.double_sided = raster.double_sided;
    record.reverse_winding = raster.reverse_winding;
    HashCombine(hash, raster.alpha_test);
    HashCombine(hash, raster.double_sided);
    HashCombine(hash, raster.reverse_winding);
    if (raster.alpha_test) {
      if (draw.material_handle >= scene.shadow_materials.size()) {
        continue;
      }
      const auto& material = scene.shadow_materials[draw.material_handle];
      const auto samples_texture = material.base_color_texture_index.IsValid()
        && (material.flags & data::pak::render::kMaterialFlag_NoTextureSampling)
          == 0;
      const auto texture_revision
        = draw.material_handle < scene.shadow_texture_revisions.size()
        ? scene.shadow_texture_revisions[draw.material_handle]
        : 0U;
      if (samples_texture && texture_revision == 0U) {
        continue;
      }
      record.masked = ShadowMaskedCasterRecord {
        .material_generation = source.material_generation,
        .flags = material.flags,
        .alpha = material.base_color.w,
        .cutoff = material.alpha_cutoff,
        .texture = material.base_color_texture_index,
        .uv_scale = material.uv_scale,
        .uv_offset = material.uv_offset,
        .uv_rotation = material.uv_rotation_radians,
        .uv_set = material.uv_set,
        .texture_revision = samples_texture ? texture_revision : 0U,
      };
      HashCombine(hash, source.material_generation);
      HashCombine(hash, material.flags);
      HashCombine(hash, material.base_color.w);
      HashCombine(hash, material.alpha_cutoff);
      HashCombine(hash, material.base_color_texture_index.get());
      HashCombine(hash, material.uv_scale.x);
      HashCombine(hash, material.uv_scale.y);
      HashCombine(hash, material.uv_offset.x);
      HashCombine(hash, material.uv_offset.y);
      HashCombine(hash, material.uv_rotation_radians);
      HashCombine(hash, material.uv_set);
      HashCombine(hash, record.masked->texture_revision);
    }
    dependency.record = Intern(std::move(record), hash);
    dependency.fingerprint = hash;
    dependency.reusable = true;
  }
  dependencies.swap(scratch_);
  scratch_.clear();
}

} // namespace oxygen::vortex::shadows::internal
