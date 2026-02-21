//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Physics/System/IWorldApi.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::physics {

PhysicsModule::PhysicsModule(engine::ModulePriority priority)
  : priority_(priority)
{
}

auto PhysicsModule::OnAttached(observer_ptr<AsyncEngine> engine) noexcept
  -> bool
{
  engine_ = engine;
  // Register as a scene observer to listen for transform changes
  // In a real implementation, we would get the active scene from the engine
  // and register this module as an observer.
  return true;
}

auto PhysicsModule::OnShutdown() noexcept -> void { engine_ = nullptr; }

auto PhysicsModule::OnGameplay(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  // Push kinematic updates to physics
  // This is where we would iterate over the collected transform changes
  // and update the corresponding physics bodies.
  co_return;
}

auto PhysicsModule::OnSceneMutation(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  // Pull dynamic updates from physics
  // This is where we would query the physics world for active bodies
  // and update the corresponding scene nodes.
  co_return;
}

auto PhysicsModule::OnTransformChanged(
  const scene::NodeHandle& node_handle) noexcept -> void
{
  // Collect transform changes from the scene
  // This is called by the scene when a node's transform changes.
  // We would store the node_handle in a queue to be processed in OnGameplay.
}

} // namespace oxygen::physics
