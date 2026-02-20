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

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::examples::internal {

//! Control block for the active scene managed by DemoShell.
/*!
 Owns the active scene and maintains a generation counter that increments on
 every scene swap or clear. This is used by ActiveScene to validate access
 across threads.
*/
class SceneControlBlock final {
public:
  SceneControlBlock() = default;
  ~SceneControlBlock() = default;

  OXYGEN_MAKE_NON_COPYABLE(SceneControlBlock)
  OXYGEN_MAKE_NON_MOVABLE(SceneControlBlock)

  //! Stage a scene for publication on frame start.
  auto StageScene(std::unique_ptr<scene::Scene> scene) -> void
  {
    std::scoped_lock lock(scene_mutex_);
    staged_scene_ = std::shared_ptr(std::move(scene));
    staged_main_camera_ = {};
    staged_scene_ptr_.store(staged_scene_.get(), std::memory_order_release);
    has_staged_scene_.store(true, std::memory_order_release);
  }

  //! Return true if a scene is currently staged.
  [[nodiscard]] auto HasStagedScene() const noexcept -> bool
  {
    return has_staged_scene_.load(std::memory_order_acquire);
  }

  //! Attach the staged scene's selected main camera.
  auto SetStagedMainCamera(scene::SceneNode camera) -> void
  {
    std::scoped_lock lock(scene_mutex_);
    staged_main_camera_ = std::move(camera);
  }

  //! Consume the most recently published main camera.
  auto TakePublishedMainCamera() -> scene::SceneNode
  {
    std::scoped_lock lock(scene_mutex_);
    return std::move(published_main_camera_);
  }

  //! Return the staged scene for mutation-time build/hydration.
  [[nodiscard]] auto GetStagedScene() const -> observer_ptr<scene::Scene>
  {
    CHECK_F(HasStagedScene(), "staged scene is required before GetStagedScene");
    const auto staged_ptr
      = observer_ptr { staged_scene_ptr_.load(std::memory_order_acquire) };
    CHECK_NOTNULL_F(staged_ptr.get(), "staged scene object is null");
    return staged_ptr;
  }

  //! Publish staged scene as active scene and advance generation.
  auto PublishStagedScene() -> bool
  {
    std::scoped_lock lock(scene_mutex_);
    if (!has_staged_scene_.load(std::memory_order_acquire)) {
      return false;
    }
    scene_ = std::move(staged_scene_);
    scene_ptr_.store(scene_.get(), std::memory_order_release);
    published_main_camera_ = std::move(staged_main_camera_);
    if (scene_) {
      scene_->CollectMutationsEnd();
    }
    staged_scene_ptr_.store(nullptr, std::memory_order_release);
    has_staged_scene_.store(false, std::memory_order_release);
    generation_.fetch_add(1U, std::memory_order_acq_rel);
    return true;
  }

  //! Swap the active scene and advance the generation counter.
  auto SetScene(std::unique_ptr<scene::Scene> scene) -> void
  {
    std::scoped_lock lock(scene_mutex_);
    scene_ = std::shared_ptr(std::move(scene));
    scene_ptr_.store(scene_.get(), std::memory_order_release);
    staged_scene_.reset();
    staged_scene_ptr_.store(nullptr, std::memory_order_release);
    has_staged_scene_.store(false, std::memory_order_release);
    staged_main_camera_ = {};
    published_main_camera_ = {};
    generation_.fetch_add(1U, std::memory_order_acq_rel);
  }

  //! Clear the active scene and advance the generation counter.
  auto ClearScene() -> void { SetScene(std::unique_ptr<scene::Scene> {}); }

  //! Return a non-owning pointer to the active scene (may be null).
  [[nodiscard]] auto TryGetScene() const noexcept -> observer_ptr<scene::Scene>
  {
    return observer_ptr { scene_ptr_.load(std::memory_order_acquire) };
  }

private:
  friend class ActiveScene;

  std::atomic<uint64_t> generation_ { 0U };
  std::atomic<scene::Scene*> scene_ptr_ { nullptr };
  std::atomic<scene::Scene*> staged_scene_ptr_ { nullptr };
  std::atomic<bool> has_staged_scene_ { false };
  mutable std::mutex scene_mutex_;
  std::shared_ptr<scene::Scene> scene_;
  std::shared_ptr<scene::Scene> staged_scene_;
  scene::SceneNode staged_main_camera_ {};
  scene::SceneNode published_main_camera_ {};
};

} // namespace oxygen::examples::internal
