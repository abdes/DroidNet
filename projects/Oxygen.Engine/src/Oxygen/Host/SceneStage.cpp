//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Host/SceneStage.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::host {

ActiveScene::ActiveScene(const observer_ptr<const SceneStage> stage)
  : stage_(stage)
  , generation_(stage ? stage->GetGeneration() : 0U)
{
}

auto ActiveScene::IsValid() const noexcept -> bool
{
  return stage_ && stage_->GetGeneration() == generation_
    && stage_->TryGetScene() != nullptr;
}

auto ActiveScene::operator->() const -> scene::Scene*
{
  CHECK_F(stage_ != nullptr, "ActiveScene used without a stage");
  const auto current = stage_->GetGeneration();
  CHECK_F(current == generation_,
    "ActiveScene stale: expected generation={}, current generation={}",
    generation_, current);
  const auto scene = stage_->TryGetScene();
  CHECK_F(scene != nullptr, "ActiveScene used with no active scene");
  return scene.get();
}

SceneStage::SceneStage() = default;

SceneStage::~SceneStage() = default;

auto SceneStage::Stage(std::shared_ptr<scene::Scene> scene) -> void
{
  CHECK_NOTNULL_F(scene.get(), "Stage requires a non-null scene");
  scene->CollectMutationsStart();
  std::scoped_lock lock(mutex_);
  DropStagedLocked();
  staged_scene_ = std::move(scene);
  staged_scene_ptr_.store(staged_scene_.get(), std::memory_order_release);
}

auto SceneStage::HasStagedScene() const noexcept -> bool
{
  return staged_scene_ptr_.load(std::memory_order_acquire) != nullptr;
}

auto SceneStage::GetStagedScene() const -> observer_ptr<scene::Scene>
{
  const auto staged
    = observer_ptr { staged_scene_ptr_.load(std::memory_order_acquire) };
  CHECK_NOTNULL_F(staged.get(), "GetStagedScene requires a staged scene");
  return staged;
}

auto SceneStage::SetStagedMainCamera(scene::SceneNode camera) -> void
{
  std::scoped_lock lock(mutex_);
  staged_main_camera_ = std::move(camera);
}

auto SceneStage::DiscardStagedScene() -> void
{
  std::scoped_lock lock(mutex_);
  DropStagedLocked();
}

auto SceneStage::PublishStaged() -> bool
{
  std::scoped_lock lock(mutex_);
  if (!staged_scene_) {
    return false;
  }
  scene_ = std::move(staged_scene_);
  scene_->CollectMutationsEnd();
  scene_ptr_.store(scene_.get(), std::memory_order_release);
  staged_scene_ptr_.store(nullptr, std::memory_order_release);
  published_main_camera_ = std::exchange(staged_main_camera_, {});
  generation_.fetch_add(1U, std::memory_order_acq_rel);
  return true;
}

auto SceneStage::TakePublishedMainCamera() -> scene::SceneNode
{
  std::scoped_lock lock(mutex_);
  return std::exchange(published_main_camera_, {});
}

auto SceneStage::SetScene(std::shared_ptr<scene::Scene> scene) -> void
{
  std::scoped_lock lock(mutex_);
  DropStagedLocked();
  scene_ = std::move(scene);
  scene_ptr_.store(scene_.get(), std::memory_order_release);
  published_main_camera_ = {};
  generation_.fetch_add(1U, std::memory_order_acq_rel);
}

auto SceneStage::Clear() -> void { SetScene(nullptr); }

auto SceneStage::TryGetScene() const noexcept -> observer_ptr<scene::Scene>
{
  return observer_ptr { scene_ptr_.load(std::memory_order_acquire) };
}

auto SceneStage::GetActiveScene() const -> ActiveScene
{
  return ActiveScene { observer_ptr { this } };
}

auto SceneStage::GetGeneration() const noexcept -> std::uint64_t
{
  return generation_.load(std::memory_order_acquire);
}

auto SceneStage::DropStagedLocked() -> void
{
  // A scene leaving the stage unpublished stops forced collection, in case
  // its owner keeps it.
  if (staged_scene_) {
    staged_scene_->CollectMutationsEnd();
  }
  staged_scene_.reset();
  staged_scene_ptr_.store(nullptr, std::memory_order_release);
  staged_main_camera_ = {};
}

} // namespace oxygen::host
