//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <vector>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::content {
class IAssetLoader;
} // namespace oxygen::content

namespace oxygen::data {
class SceneAsset;
} // namespace oxygen::data

namespace oxygen::scene {
class Scene;
} // namespace oxygen::scene

namespace oxygen::examples {

//! Result payload for a completed scene load.
struct PendingSceneSwap {
  std::shared_ptr<scene::Scene> scene;
  scene::SceneNode active_camera;
  data::AssetKey scene_key {};
};

//! Async scene loading and instantiation service.
/*!
 Builds a runtime scene graph from a scene asset and exposes a swap payload
 once loading completes.

 @note This service is UI-agnostic and designed to be reused across demo
       modules.
*/
class SceneLoaderService
  : public std::enable_shared_from_this<SceneLoaderService> {
public:
  //! Create the service with the asset loader and initial viewport size.
  SceneLoaderService(
    oxygen::content::IAssetLoader& loader, int width, int height);
  //! Destroy the loader service.
  ~SceneLoaderService();

  //! Begin loading the scene associated with the asset key.
  void StartLoad(const data::AssetKey& key);

  //! Returns true once the loader has a swap payload ready to consume.
  [[nodiscard]] auto IsReady() const -> bool { return ready_ && !consumed_; }
  //! Returns true if the load failed.
  [[nodiscard]] auto IsFailed() const -> bool { return failed_; }
  //! Returns true after the payload has been consumed.
  [[nodiscard]] auto IsConsumed() const -> bool { return consumed_; }

  //! Take the pending swap payload.
  auto GetResult() -> PendingSceneSwap { return std::move(swap_); }

  //! Mark the result as consumed and begin cleanup.
  void MarkConsumed();
  //! Tick cleanup and return true once it can be destroyed.
  auto Tick() -> bool;

private:
  //! Handle completion of the async scene load.
  void OnSceneLoaded(std::shared_ptr<data::SceneAsset> asset);

  //! Build environment systems from the scene asset.
  auto BuildEnvironment(const data::SceneAsset& asset)
    -> std::unique_ptr<scene::SceneEnvironment>;
  //! Instantiate scene nodes with transforms.
  void InstantiateNodes(const data::SceneAsset& asset);
  //! Apply parent/child relationships to instantiated nodes.
  void ApplyHierarchy(const data::SceneAsset& asset);
  //! Attach renderable components to nodes.
  void AttachRenderables(const data::SceneAsset& asset);
  //! Attach light components to nodes.
  void AttachLights(const data::SceneAsset& asset);
  //! Choose an active camera based on the asset content.
  void SelectActiveCamera(const data::SceneAsset& asset);
  //! Ensure active camera is valid and viewport is applied.
  void EnsureCameraAndViewport();
  //! Log a summary of scene contents.
  void LogSceneSummary(const data::SceneAsset& asset) const;
  //! Log the runtime scene hierarchy.
  void LogSceneHierarchy();

  oxygen::content::IAssetLoader& loader_;
  int width_;
  int height_;
  PendingSceneSwap swap_ {};
  std::vector<scene::SceneNode> runtime_nodes_ {};
  bool ready_ { false };
  bool failed_ { false };
  bool consumed_ { false };
  int linger_frames_ { 0 };
};

} // namespace oxygen::examples
