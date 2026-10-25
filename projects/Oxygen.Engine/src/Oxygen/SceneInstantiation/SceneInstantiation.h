//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/SceneInstantiation/api_export.h>
#include <Oxygen/Scripting/Attachment/SceneScriptAttachment.h>

namespace oxygen::content {
class IAssetLoader;
} // namespace oxygen::content

namespace oxygen::data {
class SceneAsset;
} // namespace oxygen::data

namespace oxygen::engine {
class InputSystem;
} // namespace oxygen::engine

namespace oxygen::scene {
class Scene;
} // namespace oxygen::scene

namespace oxygen::instantiation {

//! The codes of the instantiation issues; the composition's issues keep its
//! codes (`data::CompositionIssueCode`).
namespace issue_codes {
  //! A change whose address does not resolve; it has no effect.
  inline constexpr std::string_view kChangeUnresolved
    = "scene.instance.change_unresolved";
  //! A node cannot be placed under its parent or instance root.
  inline constexpr std::string_view kNodeParentInvalid
    = "scene.node.parent_invalid";
  //! A renderable's geometry is not loaded.
  inline constexpr std::string_view kGeometryMissing
    = "scene.renderable.geometry_missing";
  //! An override no longer matches its geometry, names a surface it does not
  //! declare, or its value is not loaded.
  inline constexpr std::string_view kOverrideNotApplied
    = "scene.override.not_applied";
  //! A camera, light or local fog volume cannot be attached.
  inline constexpr std::string_view kComponentNotAttached
    = "scene.component.not_attached";
  //! The environment cannot be applied.
  inline constexpr std::string_view kEnvironmentInvalid
    = "scene.environment.invalid";
  //! The directional lights do not resolve.
  inline constexpr std::string_view kLightsInvalid = "scene.lights.invalid";
  //! The authored starting camera does not resolve to a node with a camera;
  //! the first camera applies.
  inline constexpr std::string_view kStartingCameraUnresolved
    = "scene.starting_camera.unresolved";
  //! No starting camera is authored; the first camera applies.
  inline constexpr std::string_view kStartingCameraMissing
    = "scene.starting_camera.missing";
  //! The scene has no camera.
  inline constexpr std::string_view kNoCamera = "scene.camera.none";
  //! A starting input context is missing or cannot be hydrated.
  inline constexpr std::string_view kInputContextInvalid
    = "scene.input_context.invalid";
} // namespace issue_codes

//! A record that could not be applied; the rest of the scene loads.
struct SceneInstantiationIssue {
  //! The contract code, from `issue_codes` or the composition's codes.
  std::string code;
  //! Scene whose record it is: the requested scene or one it instances.
  data::AssetKey scene;
  //! Node index within that scene, when the record belongs to a node.
  std::optional<uint32_t> node_index;
  //! With a node index naming an instance root, the node below it.
  std::vector<data::NodeId> node_path;
  std::string message;
};

struct SceneInstantiationRequest {
  std::shared_ptr<const data::SceneAsset> asset;
  observer_ptr<scene::Scene> target;
  //! Attaches the scripts of every instantiated scene; null: no scripts.
  observer_ptr<const scripting::SceneScriptAttachment> scripts;
  //! Receives the scene's starting input contexts; null: none are started.
  observer_ptr<engine::InputSystem> input;
};

struct InstantiationResult {
  //! The node created for each node of the requested scene; index = cooked
  //! node index. Nodes of instanced scenes are their descendants, found
  //! through `SceneNode::GetInstance()` of their instance roots.
  std::vector<scene::SceneNode> nodes;
  std::vector<SceneInstantiationIssue> issues;
  //! Outcomes of every attached script slot, instanced scenes included.
  std::vector<scripting::SceneScriptSlotOutcome> scripts;
};

//! Creates the runtime scene of a cooked scene asset in `request.target`.
/*!
 Loads the scenes it instances, composes it (`data::ComposeScene`) and builds
 the composed tree: each scene's nodes in index order with names, flags,
 transforms and hierarchy, every change applied; renderables with their
 overrides, cameras, lights and local fog volumes. Each instance root gets a
 `SceneInstanceComponent`. The environment, start state and input contexts
 are the requested scene's; then the scripts of every scene attach with their
 effective parameters. Only authored values are applied: no camera is created
 and no camera value is changed.

 Start state: the active camera is the authored starting camera, which may be
 a camera an instance brings in, or, when none is authored or it does not
 resolve, the first camera in composed order, with an issue. Each starting
 input context is hydrated, added with its priority and activated; a context
 that fails to load is an issue and the others still start.

 Records that cannot be applied, changes that do not resolve, missing
 instanced scenes and cycles become issues; the rest of the scene loads. The
 scene's dependencies must be loaded; instanced scenes and starting contexts
 are references and are loaded here.
*/
OXGN_SCNI_NDAPI auto Instantiate(SceneInstantiationRequest request,
  content::IAssetLoader& loader) -> co::Co<InstantiationResult>;

} // namespace oxygen::instantiation
