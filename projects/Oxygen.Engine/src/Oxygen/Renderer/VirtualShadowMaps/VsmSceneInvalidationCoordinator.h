//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmSceneInvalidationCollector.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::scene {
class Scene;
}

namespace oxygen::renderer::vsm {

class VsmCacheManager;

struct VsmSceneInvalidationFrameInputs {
  std::vector<VsmPrimitiveInvalidationRecord> primitive_invalidations {};
  std::vector<VsmLightInvalidationRequest> light_invalidation_requests {};

  [[nodiscard]] auto Empty() const noexcept -> bool
  {
    return primitive_invalidations.empty()
      && light_invalidation_requests.empty();
  }

  auto operator==(const VsmSceneInvalidationFrameInputs&) const -> bool
    = default;
};

class VsmSceneInvalidationCoordinator final {
public:
  OXGN_RNDR_API VsmSceneInvalidationCoordinator() = default;
  OXGN_RNDR_API ~VsmSceneInvalidationCoordinator();

  OXYGEN_MAKE_NON_COPYABLE(VsmSceneInvalidationCoordinator)
  OXYGEN_MAKE_NON_MOVABLE(VsmSceneInvalidationCoordinator)

  OXGN_RNDR_API auto Reset() -> void;
  OXGN_RNDR_API auto SyncObservedScene(observer_ptr<scene::Scene> scene)
    -> void;
  OXGN_RNDR_API auto PublishScenePrimitiveHistory(
    std::span<const VsmScenePrimitiveHistoryRecord> history) -> void;
  OXGN_RNDR_API auto PublishSceneLightRemapBindings(
    std::span<const VsmSceneLightRemapBinding> bindings) -> void;
  [[nodiscard]] OXGN_RNDR_API auto DrainFrameInputs()
    -> VsmSceneInvalidationFrameInputs;

  OXGN_RNDR_API static auto ApplyLightInvalidationRequests(
    VsmCacheManager& cache_manager,
    std::span<const VsmLightInvalidationRequest> requests) -> void;

private:
  auto UnregisterObservedScene() -> void;

  VsmSceneInvalidationCollector collector_ {};
  observer_ptr<scene::Scene> observed_scene_ { nullptr };
  std::weak_ptr<scene::Scene> observed_scene_owner_ {};
};

} // namespace oxygen::renderer::vsm
