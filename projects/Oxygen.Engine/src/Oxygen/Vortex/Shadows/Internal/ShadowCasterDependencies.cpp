//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Vortex/Internal/MeshRasterState.h>
#include <Oxygen/Vortex/Shadows/Internal/ShadowCasterDependencies.h>

namespace oxygen::vortex::shadows::internal {

auto BuildShadowCasterDependencies(const PreparedSceneFrame& scene)
  -> std::vector<ShadowCasterDependency>
{
  auto dependencies = std::vector<ShadowCasterDependency> {};
  dependencies.reserve(scene.shadow_caster_sources.size());
  for (const auto& source : scene.shadow_caster_sources) {
    auto& dependency = dependencies.emplace_back();
    dependency.bounds = source.bounds;
    const auto& draw = source.draw;
    const auto transform_offset = std::size_t { draw.transform_index } * 16U;
    if (transform_offset + 16U > scene.world_matrices.size()
      || source.geometry_content_revision == 0U) {
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
    HashCombine(hash, raster.alpha_test);
    HashCombine(hash, raster.double_sided);
    HashCombine(hash, raster.reverse_winding);
    if (raster.alpha_test) {
      if (draw.material_handle >= scene.shadow_materials.size()
        || draw.material_handle >= scene.shadow_texture_revisions.size()) {
        continue;
      }
      const auto& material = scene.shadow_materials[draw.material_handle];
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
      HashCombine(hash, scene.shadow_texture_revisions[draw.material_handle]);
    }
    dependency.fingerprint = hash;
    dependency.reusable = true;
  }
  return dependencies;
}

} // namespace oxygen::vortex::shadows::internal
