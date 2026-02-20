//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/SceneSync/SceneObserverSyncModule.h>

namespace oxygen::scenesync {

SceneObserverSyncModule::SceneObserverSyncModule(
  const engine::ModulePriority priority)
  : priority_(priority)
{
}

auto SceneObserverSyncModule::OnGameplay(
  const observer_ptr<engine::FrameContext> context) -> co::Co<>
{
  SyncObservers(context);
  co_return;
}

auto SceneObserverSyncModule::OnSceneMutation(
  const observer_ptr<engine::FrameContext> context) -> co::Co<>
{
  SyncObservers(context);
  co_return;
}

auto SceneObserverSyncModule::SyncObservers(
  const observer_ptr<engine::FrameContext> context) -> void
{
  if (context == nullptr) {
    return;
  }
  const auto scene = context->GetScene();
  if (scene == nullptr) {
    return;
  }
  scene->SyncObservers();
}

} // namespace oxygen::scenesync
