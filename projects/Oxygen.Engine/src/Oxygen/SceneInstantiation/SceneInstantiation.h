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
#include <variant>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/SceneChangeValues.h>
#include <Oxygen/Data/SceneComposition.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/SurfaceId.h>
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
  //! The node to instance under is not a live node of the target scene, or
  //! is an instance root already.
  inline constexpr std::string_view kInstanceRootInvalid
    = "scene.instance.root_invalid";
  //! A live change names a node that is not an instance root.
  inline constexpr std::string_view kNotInstanceRoot
    = "scene.instance.not_root";
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
  //! Scene whose record it is: the requested scene or one it instances; nil
  //! for a host's change.
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

//! A property change a host makes, its value in the schema's units.
struct PropertyValue {
  data::schema::OverrideId override_id = data::schema::OverrideId::kInvalid;
  //! The surface, for a surface property.
  data::SurfaceId part_id;
  data::schema::SchemaValue value;
};

//! A script parameter change a host makes whose value is a live node.
struct NodeParameterValue {
  data::ScriptSlotId slot_id;
  std::string key;
  //! A node of the instance's scene.
  scene::SceneNode node;
};

//! A LOD policy change a host makes. A scene's LOD policy change indexes the
//! scene's policy table, which a host has none of; the instance root keeps
//! the host's policies instead.
struct LodPolicyValue {
  data::pak::geometry::LodPolicyRecord policy;
};

//! A change a host makes of a node below an instance root.
struct InstanceChange {
  //! One node ID per nesting level below the root, shallowest first.
  std::vector<data::NodeId> path;
  //! A host names a node by `NodeParameterValue`: the node value of a
  //! `data::ParameterChange` indexes a scene's nodes, which a host has none
  //! of, so one that names a node is invalid.
  std::variant<PropertyValue, LodPolicyValue, data::ParameterChange,
    NodeParameterValue>
    change;
};

struct InstanceRequest {
  observer_ptr<scene::Scene> target;
  //! A live node of `target` that is not an instance root; it becomes the
  //! instance root.
  scene::SceneNode root;
  //! The scene to instance; it and the scenes it instances are loaded here.
  data::AssetKey source;
  std::vector<InstanceChange> changes;
  //! Attaches the scripts of the instanced scenes; null: no scripts.
  observer_ptr<const scripting::SceneScriptAttachment> scripts;
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

//! Instances `request.source` under the existing node `request.root`.
/*!
 Builds the composed tree of the source with the host's changes
 (`data::ComposeInstance`) by the builders `Instantiate` uses: the source's
 root nodes become children of the root, which gets a
 `SceneInstanceComponent` with the changes and no requester scene. Values are
 encoded from the schema's units (`data::schema::EncodeChange`); a value that
 does not encode (`scene.instance.change_invalid`) and a change that does not
 resolve are issues and have no effect. A scene on the root's own instance path
 is a cycle. The result's nodes are the source's nodes, by index. Scene
 dependencies must be loaded, as for `Instantiate`; the materials a change names
 must be too.
*/
OXGN_SCNI_NDAPI auto InstantiateUnder(InstanceRequest request,
  content::IAssetLoader& loader) -> co::Co<InstantiationResult>;

//! What applying a live change met.
struct ChangeResult {
  std::vector<SceneInstantiationIssue> issues;
  //! The outcome of a script parameter applied again.
  std::vector<scripting::SceneScriptSlotOutcome> scripts;
};

//! Sets a change of the instance `root` makes, replacing the one with its
//! address, and applies the addressed node's value again.
/*!
 The node's value is recomposed (`data::ComposeNode`, `data::ComposeSlots`)
 from its defining scene and the changes the live instance roots above it
 hold, so a change an outer scene makes of the same property still wins. Only
 the records of the property's component are applied again; a script
 parameter is applied through `scripts`, when the scene's scripts were
 attached. An address that does not resolve, or a value that does not
 encode, returns an issue and changes nothing. Issues of applying the value
 (a material that is not loaded) leave the change set.
*/
OXGN_SCNI_NDAPI auto SetChange(scene::SceneNode& root,
  const InstanceChange& change, content::IAssetLoader& loader,
  observer_ptr<const scripting::SceneScriptAttachment> scripts = nullptr)
  -> ChangeResult;

//! Removes the change of the instance `root` with `address` and applies the
//! addressed node's value again: the next level's change, or the defining
//! scene's value. An address that does not resolve, or that `root` holds no
//! change for, returns an issue and changes nothing.
OXGN_SCNI_NDAPI auto ClearChange(scene::SceneNode& root,
  const data::ChangeAddress& address, content::IAssetLoader& loader,
  observer_ptr<const scripting::SceneScriptAttachment> scripts = nullptr)
  -> ChangeResult;

//! Removes the instance `root` of `target`: destroys the hierarchies of the
//! source's root nodes and the root's `SceneInstanceComponent`. The root and
//! its owned children stay. False when `root` is not an instance root of
//! `target`.
OXGN_SCNI_API auto Detach(scene::Scene& target, scene::SceneNode& root) -> bool;

} // namespace oxygen::instantiation
