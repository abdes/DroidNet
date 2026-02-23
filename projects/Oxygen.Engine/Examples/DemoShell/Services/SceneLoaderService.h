//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <unordered_set>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Content/ResourceTable.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Engine/ModuleEvent.h>
#include <Oxygen/Engine/ModuleManager.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::content {
class IAssetLoader;
class PakFile;
} // namespace oxygen::content

// Remove redundant forward declaration for FileStream as its header is included
// namespace oxygen::serio {
// template <typename StreamT> class FileStream;
// } // namespace oxygen::serio

namespace oxygen::data {
class SceneAsset;
class PhysicsSceneAsset;
class InputActionAsset;
class InputMappingContextAsset;
class ScriptResource; // Add forward declaration for ScriptResource
} // namespace oxygen::data

namespace oxygen::engine {
class InputSystem;
} // namespace oxygen::engine
namespace oxygen {
class AsyncEngine;
}

namespace oxygen::scripting {
class IScriptCompilationService;
class IScriptSourceResolver;
} // namespace oxygen::scripting

namespace oxygen::scene {
class Scene;
class SceneEnvironment;
} // namespace oxygen::scene
namespace oxygen::physics {
class PhysicsModule;
}

namespace oxygen::examples {

//! Result payload for a completed scene load.
struct PendingSceneSwap {
  std::shared_ptr<data::SceneAsset> asset;
  std::shared_ptr<data::PhysicsSceneAsset> physics_asset;
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
  SceneLoaderService(content::IAssetLoader& loader, Extent<uint32_t> viewport,
    std::filesystem::path source_pak_path, observer_ptr<AsyncEngine> engine,
    observer_ptr<engine::InputSystem> input_system,
    observer_ptr<scripting::IScriptCompilationService> compilation_service,
    PathFinder path_finder);
  //! Destroy the loader service.
  ~SceneLoaderService();

  OXYGEN_MAKE_NON_COPYABLE(SceneLoaderService)
  OXYGEN_DEFAULT_MOVABLE(SceneLoaderService)

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

  //! Build the runtime scene from a loaded asset.
  auto BuildSceneAsync(scene::Scene& scene, const data::SceneAsset& asset)
    -> co::Co<scene::SceneNode>;

  //! Mark the result as consumed and begin cleanup.
  void MarkConsumed();
  //! Tick cleanup and return true once it can be destroyed.
  auto Tick() -> bool;

private:
  //! Handle completion of the async scene load.
  void OnSceneLoaded(std::shared_ptr<data::SceneAsset> asset);
  //! Handle completion of the async physics sidecar load.
  void OnPhysicsSceneLoaded(std::shared_ptr<data::SceneAsset> scene_asset,
    data::AssetKey sidecar_key,
    std::shared_ptr<data::PhysicsSceneAsset> physics_asset);
  //! Resolve the mandatory physics sidecar key for the scene.
  auto ResolvePhysicsSidecarKey(const data::AssetKey& scene_key) const
    -> std::optional<data::AssetKey>;
  //! Validate strict scene/physics identity invariants.
  void ValidatePhysicsSidecarIdentity(const data::SceneAsset& scene_asset,
    const data::PhysicsSceneAsset& physics_asset,
    const data::AssetKey& sidecar_key) const;
  //! Track module lifecycle and cache PhysicsModule when available.
  void OnModuleAttached(const engine::ModuleEvent& event);
  //! Resolve a live PhysicsModule instance, refreshing cached pointer.
  auto ResolvePhysicsModule() -> observer_ptr<physics::PhysicsModule>;
  //! Hydrate supported physics bindings from sidecar into runtime scene.
  void HydratePhysicsBindings(const data::PhysicsSceneAsset& physics_asset);
  //! Attach rigid-body bindings; hard-fail on invalid/unsupported data.
  void HydrateRigidBodyBindings(physics::PhysicsModule& physics_module,
    std::span<const data::pak::RigidBodyBindingRecord> bindings);
  //! Attach character bindings; hard-fail on invalid/unsupported data.
  void HydrateCharacterBindings(physics::PhysicsModule& physics_module,
    std::span<const data::pak::CharacterBindingRecord> bindings);
  //! Attach collider-only bindings as static trigger bodies.
  void HydrateColliderBindings(physics::PhysicsModule& physics_module,
    std::span<const data::pak::ColliderBindingRecord> bindings);
  //! Enforce explicit failure for sidecar domains not yet hydrated.
  void ValidateUnsupportedPhysicsDomains(
    const data::PhysicsSceneAsset& physics_asset) const;

  //! Legacy hook for geometry dependency readiness (currently no-op).
  void QueueGeometryDependencies(const data::SceneAsset& asset);
  //! Clear local pin bookkeeping (non-destructive).
  void ReleasePinnedGeometryAssets();

  //! Build environment systems from the scene asset.
  auto BuildEnvironment(const data::SceneAsset& asset)
    -> std::unique_ptr<scene::SceneEnvironment>;
  //! Instantiate scene nodes with transforms.
  void InstantiateNodes(scene::Scene& scene, const data::SceneAsset& asset);
  //! Apply parent/child relationships to instantiated nodes.
  void ApplyHierarchy(scene::Scene& scene, const data::SceneAsset& asset);
  //! Attach renderable components to nodes.
  void AttachRenderables(const data::SceneAsset& asset);
  //! Attach light components to nodes.
  void AttachLights(const data::SceneAsset& asset);
  //! Attach scripting components to nodes.
  void AttachScripting(const data::SceneAsset& asset);
  //! Attach input mapping contexts to the InputSystem.
  void AttachInputMappings(const data::SceneAsset& asset);
  //! Apply script parameter overrides for one slot.
  void ApplySlotParameters(scene::SceneNode::Scripting& scripting,
    const scene::SceneNode::Scripting::Slot& slot,
    std::span<const data::pak::ScriptParamRecord> params);
  //! Queue script compilation for a slot when source is available.
  void QueueSlotCompilation(scene::SceneNode node,
    const scene::SceneNode::Scripting::Slot& slot,
    std::shared_ptr<const data::ScriptAsset> script_asset);
  //! Choose an active camera based on the asset content.
  void SelectActiveCamera(const data::SceneAsset& asset);
  //! Ensure active camera is valid and viewport is applied.
  void EnsureCameraAndViewport(scene::Scene& scene);
  //! Log a summary of scene contents.
  void LogSceneSummary(const data::SceneAsset& asset) const;
  //! Log the runtime scene hierarchy.
  void LogSceneHierarchy(const scene::Scene& scene);

  content::IAssetLoader& loader_; // FIXME
  Extent<uint32_t> extent_ {};

  // Stored execution context for reloading
  std::filesystem::path source_pak_path_;
  PathFinder path_finder_;

  PendingSceneSwap swap_ {};
  std::vector<scene::SceneNode> runtime_nodes_;
  scene::SceneNode active_camera_;
  bool ready_ { false };
  bool failed_ { false };
  bool consumed_ { false };
  int linger_frames_ { 0 };

  std::unordered_set<data::AssetKey> pending_geometry_keys_;
  std::vector<data::AssetKey> pinned_geometry_keys_;

  std::unique_ptr<content::PakFile> source_pak_;
  observer_ptr<AsyncEngine> engine_;
  engine::ModuleManager::Subscription physics_module_subscription_ {};
  observer_ptr<physics::PhysicsModule> physics_module_;
  observer_ptr<engine::InputSystem> input_system_;
  observer_ptr<scripting::IScriptCompilationService> compilation_service_;
  std::unique_ptr<scripting::IScriptSourceResolver> source_resolver_;

  auto ReadScriptResource(uint32_t index) const
    -> std::shared_ptr<const data::ScriptResource>;
};

} // namespace oxygen::examples
