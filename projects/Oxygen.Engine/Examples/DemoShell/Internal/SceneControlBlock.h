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
#include <Oxygen/Scene/Scene.h>

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

  //! Swap the active scene and advance the generation counter.
  auto SetScene(std::unique_ptr<scene::Scene> scene) -> void
  {
    std::scoped_lock lock(scene_mutex_);
    scene_ = std::shared_ptr(std::move(scene));
    scene_ptr_.store(scene_.get(), std::memory_order_release);
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
  mutable std::mutex scene_mutex_;
  std::shared_ptr<scene::Scene> scene_;
};

} // namespace oxygen::examples::internal
