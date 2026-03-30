//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/VirtualShadowMaps/VsmSceneInvalidationCoordinator.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::renderer::vsm {

VsmSceneInvalidationCoordinator::~VsmSceneInvalidationCoordinator() { Reset(); }

auto VsmSceneInvalidationCoordinator::Reset() -> void
{
  UnregisterObservedScene();
  collector_.Reset();
}

auto VsmSceneInvalidationCoordinator::SyncObservedScene(
  const observer_ptr<scene::Scene> scene) -> void
{
  if (scene.get() == observed_scene_.get()) {
    return;
  }

  UnregisterObservedScene();
  collector_.Reset();
  observed_scene_ = scene;

  if (observed_scene_ == nullptr) {
    return;
  }

  observed_scene_owner_ = observed_scene_->weak_from_this();
  CHECK_F(!observed_scene_owner_.expired(),
    "VsmSceneInvalidationCoordinator requires the active Scene to be "
    "shared-owned for observer rebinding");

  const auto registered = observed_scene_->RegisterObserver(
    observer_ptr<scene::ISceneObserver> { &collector_ },
    VsmSceneInvalidationCollector::ObserverMutationMask());
  CHECK_F(registered,
    "VsmSceneInvalidationCoordinator failed to register its collector with "
    "the active Scene");
}

auto VsmSceneInvalidationCoordinator::PublishScenePrimitiveHistory(
  const std::span<const VsmScenePrimitiveHistoryRecord> history) -> void
{
  collector_.PublishScenePrimitiveHistory(history);
}

auto VsmSceneInvalidationCoordinator::PublishSceneLightRemapBindings(
  const std::span<const VsmSceneLightRemapBinding> bindings) -> void
{
  collector_.PublishSceneLightRemapBindings(bindings);
}

auto VsmSceneInvalidationCoordinator::DrainFrameInputs()
  -> VsmSceneInvalidationFrameInputs
{
  return VsmSceneInvalidationFrameInputs {
    .primitive_invalidations = collector_.DrainPrimitiveInvalidationRecords(),
    .light_invalidation_requests = collector_.DrainLightInvalidationRequests(),
  };
}

auto VsmSceneInvalidationCoordinator::ApplyLightInvalidationRequests(
  VsmCacheManager& cache_manager,
  const std::span<const VsmLightInvalidationRequest> requests) -> void
{
  for (const auto& request : requests) {
    switch (request.kind) {
    case VsmLightCacheKind::kLocal:
      cache_manager.InvalidateLocalLights(
        request.remap_keys, request.scope, request.reason);
      break;
    case VsmLightCacheKind::kDirectional:
      cache_manager.InvalidateDirectionalClipmaps(
        request.remap_keys, request.scope, request.reason);
      break;
    }
  }
}

auto VsmSceneInvalidationCoordinator::UnregisterObservedScene() -> void
{
  if (const auto owner = observed_scene_owner_.lock(); owner != nullptr) {
    const auto unregistered = owner->UnregisterObserver(
      observer_ptr<scene::ISceneObserver> { &collector_ });
    CHECK_F(unregistered,
      "VsmSceneInvalidationCoordinator failed to unregister its collector "
      "from the previously observed Scene");
  }

  observed_scene_ = nullptr;
  observed_scene_owner_.reset();
}

} // namespace oxygen::renderer::vsm
