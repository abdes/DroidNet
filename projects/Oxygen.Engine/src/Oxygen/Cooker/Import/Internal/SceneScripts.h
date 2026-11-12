//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/ScriptingSidecarSource.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneIds.h>

namespace oxygen::content::import {
class AssetReferenceBuilder;
class ImportSession;
} // namespace oxygen::content::import

namespace oxygen::content::import::internal {

//! One change of a script parameter of an instanced node.
struct InstanceParamChange final {
  uint32_t node_index = 0;
  std::vector<data::NodeId> node_path;
  data::ScriptSlotId slot_id;
  ScriptingSidecarParam param;
  //! The sidecar location its diagnostics name; empty for a change the
  //! scene already holds.
  std::string json_pointer;
};

//! A script slot of a node, with its parameters.
struct SlotWithParams final {
  data::pak::scripting::ScriptSlotRecord slot = {};
  std::vector<ScriptingSidecarParam> params;
};

//! The script slots of a scene's nodes, by node index.
using SceneScriptBindings = std::map<uint32_t, std::vector<SlotWithParams>>;

//! The scripts of a scene, laid out as its descriptor holds them.
struct SceneScriptData final {
  std::vector<data::pak::scripting::ScriptingComponentRecord> components;
  std::vector<data::pak::scripting::ScriptSlotRecord> slots;
  std::vector<ScriptingSidecarParam> params;
  //! Sorted by node, path, slot and key.
  std::vector<InstanceParamChange> instance_changes;
};

//! Script tables that outgrow their format, with the code that names it.
struct SceneScriptsOverflow final {
  std::string_view code;
  std::string message;
};

//! Decodes a cooked record with the string table of its descriptor, and the
//! path of a node value of `scene`, when the record belongs to one.
/*!
 @return The parameter, or nullopt when its key or value is not in the table.
*/
OXGN_COOK_NDAPI auto DecodeScriptParam(
  const data::pak::scripting::ScriptParamRecord& record,
  const data::DescriptorStrings& strings,
  const data::SceneAsset* scene = nullptr)
  -> std::optional<ScriptingSidecarParam>;

//! The script slots `scene` holds, by node.
/*!
 @throw std::runtime_error When a parameter's key or value is not in the
   scene's string table.
*/
OXGN_COOK_NDAPI auto ReadSceneScriptBindings(const data::SceneAsset& scene)
  -> SceneScriptBindings;

//! The changes `scene` holds of the script parameters of instanced nodes.
/*!
 @throw std::runtime_error When a parameter's key or value is not in the
   scene's string table.
*/
OXGN_COOK_NDAPI auto ReadSceneInstanceChanges(const data::SceneAsset& scene)
  -> std::vector<InstanceParamChange>;

//! Lays out `bindings` as a descriptor holds them, without instance changes.
OXGN_COOK_NDAPI auto SerializeSceneScriptBindings(
  const SceneScriptBindings& bindings)
  -> std::expected<SceneScriptData, SceneScriptsOverflow>;

//! The descriptor `descriptor` of `scene` with `scripts` in place of the
//! scripts it holds.
/*!
 Every other table is kept; the node path tables are written again with the
 paths something still names.

 @return The patched descriptor, or why it cannot be written.
*/
OXGN_COOK_NDAPI auto PatchSceneScripts(std::span<const std::byte> descriptor,
  const data::SceneAsset& scene, const SceneScriptData& scripts)
  -> std::expected<std::vector<std::byte>, std::string>;

//! Adds to `references` the scripts and the asset values of `scripts`.
OXGN_COOK_API auto AddSceneScriptReferences(
  AssetReferenceBuilder& references, const SceneScriptData& scripts) -> void;

//! A scene re-cooked from its source with the scripts it keeps.
struct CarriedSceneScripts final {
  std::vector<std::byte> descriptor;
  data::AssetReferences references;
};

//! Gives `fresh`, a scene re-cooked from its source, the scripts of `listed`,
//! the cooked scene it replaces, on the same nodes.
/*!
 A scene's scripts are applied to its cooked descriptor by scripting
 sidecars, so a re-cook keeps them: nodes are matched by ID, whatever their
 index now. Scripts of a node `fresh` no longer holds are dropped
 (`scene.scripts.node_missing`), as are changes of a node it no longer holds
 (`scene.scripts.change_node_missing`); a node value naming such a node is
 unset (`scene.scripts.node_value_missing`). Each is a warning in
 `diagnostics`, attributed to `source_id`. A node `fresh` scripts itself
 keeps its own scripts.

 @param fresh_descriptor The descriptor of `fresh`.
 @param fresh_references The references of `fresh`.
 @return The scene with the scripts it keeps; nullopt when `listed` holds
   none; or why it cannot be written.
 @throw std::runtime_error When a parameter of `listed` has a key or value
   that is not in its string table.
*/
OXGN_COOK_NDAPI auto CarrySceneScripts(const data::SceneAsset& listed,
  const data::SceneAsset& fresh, std::span<const std::byte> fresh_descriptor,
  const data::AssetReferences& fresh_references, std::string_view source_id,
  std::vector<ImportDiagnostic>& diagnostics)
  -> std::expected<std::optional<CarriedSceneScripts>, std::string>;

//! Gives the scene `scene_key` the session re-cooked into `descriptor`, with
//! `references`, the scripts of the scene its cooked root lists at that key
//! (`CarrySceneScripts`).
/*!
 A root that lists no such scene leaves the scene as it is. Diagnostics go
 to `session`, attributed to `source_id`.

 @return False, with an error in `session`
   (`scene.scripts.carry_failed`), when the listed scene cannot be read or
   the scripts cannot be written.
*/
OXGN_COOK_NDAPI auto KeepListedSceneScripts(ImportSession& session,
  const data::AssetKey& scene_key, std::string_view source_id,
  std::vector<std::byte>& descriptor, data::AssetReferences& references)
  -> bool;

} // namespace oxygen::content::import::internal
