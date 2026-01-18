//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/Async/MaterialReadinessTracker.h>

#include <utility>

namespace oxygen::content::import {

MaterialReadinessTracker::MaterialReadinessTracker(
  std::span<const MaterialPipeline::WorkItem> materials)
{
  deps_.resize(materials.size());

  auto add_binding_source
    = [](MaterialDep& dep,
        std::unordered_map<std::string, std::vector<size_t>>& mapping,
        const MaterialTextureBinding& binding, const size_t material_index) {
        if (!binding.assigned || binding.source_id.empty()) {
          return;
        }

        if (dep.pending_textures.insert(binding.source_id).second) {
          mapping[binding.source_id].push_back(material_index);
        }
      };

  for (size_t i = 0; i < materials.size(); ++i) {
    auto& dep = deps_[i];
    const auto& textures = materials[i].textures;

    add_binding_source(dep, texture_to_materials_, textures.base_color, i);
    add_binding_source(dep, texture_to_materials_, textures.normal, i);
    add_binding_source(dep, texture_to_materials_, textures.metallic, i);
    add_binding_source(dep, texture_to_materials_, textures.roughness, i);
    add_binding_source(
      dep, texture_to_materials_, textures.ambient_occlusion, i);
    add_binding_source(dep, texture_to_materials_, textures.emissive, i);
    add_binding_source(dep, texture_to_materials_, textures.specular, i);
    add_binding_source(dep, texture_to_materials_, textures.sheen_color, i);
    add_binding_source(dep, texture_to_materials_, textures.clearcoat, i);
    add_binding_source(
      dep, texture_to_materials_, textures.clearcoat_normal, i);
    add_binding_source(dep, texture_to_materials_, textures.transmission, i);
    add_binding_source(dep, texture_to_materials_, textures.thickness, i);

    if (dep.pending_textures.empty()) {
      dep.emitted = true;
      ready_without_textures_.push_back(i);
    }
  }
}

auto MaterialReadinessTracker::TakeReadyWithoutTextures() -> std::vector<size_t>
{
  auto ready = std::move(ready_without_textures_);
  ready_without_textures_.clear();
  return ready;
}

auto MaterialReadinessTracker::MarkTextureReady(std::string_view source_id)
  -> std::vector<size_t>
{
  std::vector<size_t> newly_ready;
  const auto it = texture_to_materials_.find(std::string(source_id));
  if (it == texture_to_materials_.end()) {
    return newly_ready;
  }

  for (const auto material_index : it->second) {
    if (material_index >= deps_.size()) {
      continue;
    }

    auto& dep = deps_[material_index];
    if (dep.emitted) {
      continue;
    }

    dep.pending_textures.erase(std::string(source_id));
    if (dep.pending_textures.empty()) {
      dep.emitted = true;
      newly_ready.push_back(material_index);
    }
  }

  return newly_ready;
}

} // namespace oxygen::content::import
