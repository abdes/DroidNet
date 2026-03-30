// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/VirtualShadowMaps/VsmSceneInvalidationCollector.h>

#include <algorithm>
#include <tuple>

namespace oxygen::renderer::vsm {

namespace {

  auto MergeScope(const VsmCacheInvalidationScope lhs,
    const VsmCacheInvalidationScope rhs) noexcept -> VsmCacheInvalidationScope
  {
    if (lhs == rhs) {
      return lhs;
    }
    return VsmCacheInvalidationScope::kStaticAndDynamic;
  }

} // namespace

auto VsmSceneInvalidationCollector::Reset() -> void
{
  published_scene_primitive_history_.clear();
  published_scene_light_remap_bindings_.clear();
  pending_transform_changed_nodes_.clear();
  pending_destroyed_nodes_.clear();
  pending_light_changed_nodes_.clear();
}

auto VsmSceneInvalidationCollector::PublishScenePrimitiveHistory(
  const std::span<const VsmScenePrimitiveHistoryRecord> history) -> void
{
  published_scene_primitive_history_.assign(history.begin(), history.end());
}

auto VsmSceneInvalidationCollector::PublishSceneLightRemapBindings(
  const std::span<const VsmSceneLightRemapBinding> bindings) -> void
{
  published_scene_light_remap_bindings_.assign(
    bindings.begin(), bindings.end());
}

auto VsmSceneInvalidationCollector::DrainPrimitiveInvalidationRecords()
  -> std::vector<VsmPrimitiveInvalidationRecord>
{
  auto pending = std::vector<VsmPrimitiveInvalidationRecord> {};

  auto append_node
    = [&](const scene::NodeHandle& node_handle, const bool is_removed) -> void {
    for (const auto& item : published_scene_primitive_history_) {
      if (item.node_handle != node_handle) {
        continue;
      }
      pending.push_back(VsmPrimitiveInvalidationRecord {
        .primitive = item.primitive,
        .world_bounding_sphere = item.world_bounding_sphere,
        .scope = item.static_shadow_caster
          ? VsmCacheInvalidationScope::kStaticAndDynamic
          : VsmCacheInvalidationScope::kDynamicOnly,
        .is_removed = is_removed,
      });
    }
  };

  for (const auto& node_handle : pending_transform_changed_nodes_) {
    if (pending_destroyed_nodes_.contains(node_handle)) {
      continue;
    }
    append_node(node_handle, false);
  }
  for (const auto& node_handle : pending_destroyed_nodes_) {
    append_node(node_handle, true);
  }

  auto merged = std::vector<VsmPrimitiveInvalidationRecord> {};
  merged.reserve(pending.size());
  for (const auto& item : pending) {
    auto existing = std::ranges::find_if(merged,
      [&](const auto& current) { return current.primitive == item.primitive; });
    if (existing == merged.end()) {
      merged.push_back(item);
    } else {
      existing->scope = MergeScope(existing->scope, item.scope);
      existing->is_removed = existing->is_removed || item.is_removed;
      existing->world_bounding_sphere = item.world_bounding_sphere;
    }
  }

  std::ranges::sort(merged, [](const auto& lhs, const auto& rhs) {
    return std::tie(lhs.primitive.transform_index,
             lhs.primitive.transform_generation, lhs.primitive.submesh_index)
      < std::tie(rhs.primitive.transform_index,
        rhs.primitive.transform_generation, rhs.primitive.submesh_index);
  });

  pending_transform_changed_nodes_.clear();
  pending_destroyed_nodes_.clear();
  return merged;
}

auto VsmSceneInvalidationCollector::DrainLightInvalidationRequests()
  -> std::vector<VsmLightInvalidationRequest>
{
  auto requests = std::vector<VsmLightInvalidationRequest> {};

  auto append_binding = [&](const VsmSceneLightRemapBinding& binding) -> void {
    auto existing = std::ranges::find_if(
      requests, [&](const auto& item) { return item.kind == binding.kind; });
    if (existing == requests.end()) {
      requests.push_back(VsmLightInvalidationRequest {
        .kind = binding.kind,
        .remap_keys = binding.remap_keys,
      });
      return;
    }

    existing->remap_keys.insert(existing->remap_keys.end(),
      binding.remap_keys.begin(), binding.remap_keys.end());
  };

  for (const auto& node_handle : pending_light_changed_nodes_) {
    for (const auto& binding : published_scene_light_remap_bindings_) {
      if (binding.node_handle == node_handle) {
        append_binding(binding);
      }
    }
  }

  for (auto& request : requests) {
    std::ranges::sort(request.remap_keys);
    request.remap_keys.erase(
      std::unique(request.remap_keys.begin(), request.remap_keys.end()),
      request.remap_keys.end());
  }

  pending_light_changed_nodes_.clear();
  return requests;
}

auto VsmSceneInvalidationCollector::OnLightChanged(
  const scene::NodeHandle& node_handle) noexcept -> void
{
  pending_light_changed_nodes_.insert(node_handle);
}

auto VsmSceneInvalidationCollector::OnTransformChanged(
  const scene::NodeHandle& node_handle) noexcept -> void
{
  pending_transform_changed_nodes_.insert(node_handle);
}

auto VsmSceneInvalidationCollector::OnNodeDestroyed(
  const scene::NodeHandle& node_handle) noexcept -> void
{
  pending_destroyed_nodes_.insert(node_handle);
}

} // namespace oxygen::renderer::vsm
