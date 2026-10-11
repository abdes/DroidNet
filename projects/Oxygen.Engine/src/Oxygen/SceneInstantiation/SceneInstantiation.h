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
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/AssetKey.h>
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

//! A record that could not be applied; the rest of the scene loads.
struct SceneInstantiationIssue {
  //! Scene whose record it is: the requested scene or one it instances.
  data::AssetKey scene;
  //! Node index within that scene, when the record belongs to a node.
  std::optional<uint32_t> node_index;
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

struct SceneInstance {
  //! The node created for each node of the requested scene; index = cooked
  //! node index. Nodes of instanced scenes are their descendants.
  std::vector<scene::SceneNode> nodes;
  std::vector<SceneInstantiationIssue> issues;
  //! Outcomes of every attached script slot, instanced scenes included.
  std::vector<scripting::SceneScriptSlotOutcome> scripts;
};

//! Creates the runtime scene of a cooked scene asset in `request.target`.
/*!
 Creates the nodes in index order with names, flags, transforms and
 hierarchy; attaches renderables with their material overrides, cameras,
 lights and local fog volumes; sets the environment; instantiates the scenes
 it instances under their instance nodes; applies its start state; and then
 attaches scripts. Only authored values are applied: no camera is created and
 no camera value is changed.

 Start state: the active camera is the authored starting camera or, when
 none is authored, the first camera in node order, with an issue saying so.
 Each starting input context is hydrated, added with its priority and
 activated; a context that fails to load is an issue and the others still
 start. Instanced scenes contribute nodes and scripts, never start state or
 environment.

 Records that cannot be applied become issues; the rest of the scene loads.
 The scene's dependencies must be loaded; instanced scenes and starting
 contexts are references and are loaded here.
*/
OXGN_SCNI_NDAPI auto Instantiate(SceneInstantiationRequest request,
  content::IAssetLoader& loader) -> co::Co<SceneInstance>;

} // namespace oxygen::instantiation
