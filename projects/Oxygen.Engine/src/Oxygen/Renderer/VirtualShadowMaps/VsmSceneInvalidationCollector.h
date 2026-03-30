// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>
#include <unordered_set>
#include <vector>

#include <glm/vec4.hpp>

#include <Oxygen/Core/Types/Bool32.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerTypes.h>
#include <Oxygen/Renderer/api_export.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::renderer::vsm {

// Scene-facing history record used to translate scene-node mutations into the
// renderer-side primitive identity consumed by VSM invalidation.
struct VsmScenePrimitiveHistoryRecord {
  scene::NodeHandle node_handle {};
  VsmPrimitiveIdentity primitive {};
  glm::vec4 world_bounding_sphere { 0.0F, 0.0F, 0.0F, 0.0F };
  Bool32 static_shadow_caster { false };

  auto operator==(const VsmScenePrimitiveHistoryRecord&) const -> bool
    = default;
};

// Scene-facing mapping from a light node to the VSM remap keys currently owned
// by that light.
struct VsmSceneLightRemapBinding {
  scene::NodeHandle node_handle {};
  VsmLightCacheKind kind { VsmLightCacheKind::kLocal };
  VsmRemapKeyList remap_keys {};

  auto operator==(const VsmSceneLightRemapBinding&) const -> bool = default;
};

class VsmSceneInvalidationCollector final : public scene::ISceneObserver {
public:
  OXGN_RNDR_API VsmSceneInvalidationCollector() = default;
  OXGN_RNDR_API ~VsmSceneInvalidationCollector() override = default;

  OXYGEN_DEFAULT_COPYABLE(VsmSceneInvalidationCollector)
  OXYGEN_DEFAULT_MOVABLE(VsmSceneInvalidationCollector)

  [[nodiscard]] static constexpr auto ObserverMutationMask() noexcept
    -> scene::SceneMutationMask
  {
    return scene::SceneMutationMask::kTransformChanged
      | scene::SceneMutationMask::kNodeDestroyed
      | scene::SceneMutationMask::kLightChanged;
  }

  OXGN_RNDR_API auto Reset() -> void;
  OXGN_RNDR_API auto PublishScenePrimitiveHistory(
    std::span<const VsmScenePrimitiveHistoryRecord> history) -> void;
  OXGN_RNDR_API auto PublishSceneLightRemapBindings(
    std::span<const VsmSceneLightRemapBinding> bindings) -> void;
  OXGN_RNDR_API auto DrainPrimitiveInvalidationRecords()
    -> std::vector<VsmPrimitiveInvalidationRecord>;
  OXGN_RNDR_API auto DrainLightInvalidationRequests()
    -> std::vector<VsmLightInvalidationRequest>;

  OXGN_RNDR_API auto OnLightChanged(
    const scene::NodeHandle& node_handle) noexcept -> void override;
  OXGN_RNDR_API auto OnTransformChanged(
    const scene::NodeHandle& node_handle) noexcept -> void override;
  OXGN_RNDR_API auto OnNodeDestroyed(
    const scene::NodeHandle& node_handle) noexcept -> void override;

private:
  std::vector<VsmScenePrimitiveHistoryRecord>
    published_scene_primitive_history_ {};
  std::vector<VsmSceneLightRemapBinding>
    published_scene_light_remap_bindings_ {};
  std::unordered_set<scene::NodeHandle> pending_transform_changed_nodes_ {};
  std::unordered_set<scene::NodeHandle> pending_destroyed_nodes_ {};
  std::unordered_set<scene::NodeHandle> pending_light_changed_nodes_ {};
};

} // namespace oxygen::renderer::vsm
