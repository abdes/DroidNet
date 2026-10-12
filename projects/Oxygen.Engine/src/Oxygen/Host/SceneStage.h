//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Host/api_export.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::scene {
class Scene;
} // namespace oxygen::scene

namespace oxygen::host {

class SceneStage;

//! Handle to the scene that was active when the handle was taken.
/*!
 Every scene change advances the stage generation. A handle whose generation
 no longer matches is stale: `IsValid()` returns false and `operator->`
 aborts, so code that cached a handle cannot reach a replaced scene.
*/
class ActiveScene final {
public:
  ActiveScene() = default;
  OXGN_HOST_API explicit ActiveScene(observer_ptr<const SceneStage> stage);
  ~ActiveScene() = default;

  OXYGEN_DEFAULT_COPYABLE(ActiveScene)
  OXYGEN_DEFAULT_MOVABLE(ActiveScene)

  //! True while the stage still holds the scene this handle was taken for.
  OXGN_HOST_NDAPI auto IsValid() const noexcept -> bool;

  //! Access the scene; aborts when the handle is stale or empty.
  OXGN_HOST_API auto operator->() const -> scene::Scene*;

private:
  observer_ptr<const SceneStage> stage_ { nullptr };
  std::uint64_t generation_ { 0U };
};

//! Hands a scene built off the frame over to the frame at frame start.
/*!
 A scene is staged and built during scene mutation, then published at the
 next frame start before `FrameContext::SetScene`. Staging starts the scene's
 mutation collection and publishing ends it, so mutations made while the scene
 was built reach the observers that register once it is published.

 The active scene is read from other threads (UI code), so the active and
 staged pointers are atomics and every change is made under a mutex.
*/
class SceneStage final {
public:
  OXGN_HOST_API SceneStage();
  OXGN_HOST_API ~SceneStage();

  OXYGEN_MAKE_NON_COPYABLE(SceneStage)
  OXYGEN_MAKE_NON_MOVABLE(SceneStage)

  //! Stage a scene for publication; replaces any staged scene.
  OXGN_HOST_API auto Stage(std::shared_ptr<scene::Scene> scene) -> void;

  //! True when a scene is staged.
  OXGN_HOST_NDAPI auto HasStagedScene() const noexcept -> bool;

  //! The staged scene; a scene must be staged.
  OXGN_HOST_NDAPI auto GetStagedScene() const -> observer_ptr<scene::Scene>;

  //! Select the camera that becomes the main camera when the stage publishes.
  OXGN_HOST_API auto SetStagedMainCamera(scene::SceneNode camera) -> void;

  //! Drop the staged scene without publishing it.
  OXGN_HOST_API auto DiscardStagedScene() -> void;

  //! Make the staged scene active; false when nothing is staged.
  OXGN_HOST_API auto PublishStaged() -> bool;

  //! Consume the main camera selected for the last published scene.
  OXGN_HOST_NDAPI auto TakePublishedMainCamera() -> scene::SceneNode;

  //! Make a scene active directly and drop any staged scene.
  OXGN_HOST_API auto SetScene(std::shared_ptr<scene::Scene> scene) -> void;

  //! Drop the active and the staged scenes.
  OXGN_HOST_API auto Clear() -> void;

  //! The active scene, or null.
  OXGN_HOST_NDAPI auto TryGetScene() const noexcept
    -> observer_ptr<scene::Scene>;

  //! A handle that goes stale when the active scene changes.
  OXGN_HOST_NDAPI auto GetActiveScene() const -> ActiveScene;

  //! Advances on every publish, `SetScene` and `Clear`.
  OXGN_HOST_NDAPI auto GetGeneration() const noexcept -> std::uint64_t;

private:
  auto DropStagedLocked() -> void;

  std::atomic<std::uint64_t> generation_ { 0U };
  std::atomic<scene::Scene*> scene_ptr_ { nullptr };
  std::atomic<scene::Scene*> staged_scene_ptr_ { nullptr };
  mutable std::mutex mutex_;
  std::shared_ptr<scene::Scene> scene_;
  std::shared_ptr<scene::Scene> staged_scene_;
  scene::SceneNode staged_main_camera_;
  scene::SceneNode published_main_camera_;
};

} // namespace oxygen::host
