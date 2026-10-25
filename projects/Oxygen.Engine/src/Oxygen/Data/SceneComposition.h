//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

class SceneAsset;

//! The loaded scene an asset key names, or null when none is available.
using SceneResolver
  = std::function<std::shared_ptr<const SceneAsset>(const AssetKey&)>;

//! A change of one script parameter of a slot of an instanced node.
struct ParameterChange {
  ScriptSlotId slot_id;
  std::string key;
  //! A node value names a node of the scene that makes the change.
  ScriptParam value;
};

//! A change one scene makes of a node an instance brings in.
/*!
 `path` names the node below the instance root, one node ID per nesting level,
 shallowest first. A property change is an `OverrideRecord` whose `target` is
 not used: `override_id`, `part_id`, `layout_revision` and `value` are.
*/
struct SceneChange {
  std::vector<NodeId> path;
  std::variant<pak::world::OverrideRecord, ParameterChange> change;
};

//! The property and part a change sets.
struct PropertyAddress {
  schema::OverrideId override_id = schema::OverrideId::kInvalid;
  SurfaceId part_id;

  auto operator==(const PropertyAddress&) const -> bool = default;
};

//! The script parameter a change sets.
struct ParameterAddress {
  ScriptSlotId slot_id;
  std::string key;

  auto operator==(const ParameterAddress&) const -> bool = default;
};

//! What a change sets: its node path and the property or parameter. A scene
//! makes at most one change per address below one instance root.
struct ChangeAddress {
  std::vector<NodeId> path;
  std::variant<PropertyAddress, ParameterAddress> target;

  auto operator==(const ChangeAddress&) const -> bool = default;
};

//! The address `change` sets.
OXGN_DATA_NDAPI auto AddressOf(const SceneChange& change) -> ChangeAddress;

//! Where a value of the composed scene comes from: an occurrence, or the
//! requester of an instance composition (`kRequester`).
using ChangeSupplier = std::size_t;
inline constexpr ChangeSupplier kRequester
  = std::numeric_limits<ChangeSupplier>::max();

//! An asset-sourced override in effect on a composed node.
struct ComposedOverride {
  //! `target` is the node's own index in its scene, with no path.
  pak::world::OverrideRecord record;
  //! The scene whose tables hold the value (a LOD policy, an asset).
  ChangeSupplier supplier = 0;
};

//! A changed property of a composed node and the scene that supplied it.
struct AppliedChange {
  schema::OverrideId override_id = schema::OverrideId::kInvalid;
  SurfaceId part_id;
  ChangeSupplier supplier = 0;
};

//! A node of the composed tree, with every change applied.
struct ComposedNode {
  //! Index in the scene that defines the node.
  pak::world::SceneNodeIndexT index = 0;
  //! Node IDs below the outermost instance root, shallowest first; empty for
  //! a node of the composed scene itself.
  std::vector<NodeId> path;
  pak::world::NodeRecord node;
  std::optional<pak::world::RenderableRecord> renderable;
  std::optional<pak::world::PerspectiveCameraRecord> perspective_camera;
  std::optional<pak::world::OrthographicCameraRecord> orthographic_camera;
  std::optional<pak::world::DirectionalLightRecord> directional_light;
  std::optional<pak::world::PointLightRecord> point_light;
  std::optional<pak::world::SpotLightRecord> spot_light;
  std::optional<pak::world::LocalFogVolumeRecord> local_fog_volume;
  //! The geometry's values the node replaces, its own and changed.
  std::vector<ComposedOverride> overrides;
  //! The properties changed from outside the defining scene.
  std::vector<AppliedChange> changes;
};

//! A script parameter of a composed slot and the scene that supplied it.
struct ComposedParameter {
  std::string key;
  ScriptParam value;
  ChangeSupplier supplier = 0;
};

//! A script slot of a composed node with its effective parameters.
struct ComposedSlot {
  pak::world::SceneNodeIndexT node_index = 0;
  ScriptSlotId slot_id;
  AssetKey script;
  int32_t execution_order = 0;
  std::vector<ComposedParameter> parameters;
};

//! A change that reaches an occurrence: a change of `supplier`'s scene, of
//! which the first `consumed` path elements led here.
struct ReachingChange {
  SceneChange change;
  ChangeSupplier supplier = 0;
  //! The supplier's instance root the change is made below.
  pak::world::SceneNodeIndexT supplier_root = 0;
  std::size_t consumed = 0;
  //! Nesting level of the supplier; a deeper level's change applies first.
  std::uint32_t level = 0;
};

//! One scene of the composed tree: the composed scene, or one instance.
struct ComposedOccurrence {
  std::shared_ptr<const SceneAsset> scene;
  //! The occurrence that instances this one; none for the first.
  std::optional<std::size_t> parent;
  //! The instance root in the parent's scene, or the requester's root.
  pak::world::SceneNodeIndexT instance_root = 0;
  //! The outermost instance root in the composed scene.
  pak::world::SceneNodeIndexT outer_root = 0;
  //! Node IDs that lead from the outermost instance root to this occurrence.
  std::vector<NodeId> prefix;
  //! Nesting level: 0 for the composed scene.
  std::uint32_t level = 0;
  //! The scene's nodes, in index order.
  std::vector<ComposedNode> nodes;
  //! Its scripts' slots with their effective parameters, in record order.
  std::vector<ComposedSlot> slots;
  //! The changes from above that reach it.
  std::vector<ReachingChange> changes;
};

//! A change whose address does not resolve; it has no effect.
struct UnresolvedChange {
  //! The change as its scene made it, its path below `instance_root`.
  SceneChange change;
  ChangeSupplier supplier = 0;
  pak::world::SceneNodeIndexT instance_root = 0;
};

enum class CompositionIssueCode : std::uint8_t {
  //! An instanced scene is missing or cannot be read.
  kSceneMissing,
  //! An instanced scene is reached again on its own path; that branch ends.
  kCycle,
  //! A change's value fails the validation of the record it patches.
  kChangeInvalid,
  //! The scene's script parameters cannot be decoded.
  kRecordInvalid,
};

//! The issue's code, such as `scene.instance.cycle`.
OXGN_DATA_NDAPI auto to_string(CompositionIssueCode code) noexcept
  -> std::string_view;

struct CompositionIssue {
  CompositionIssueCode code = CompositionIssueCode::kSceneMissing;
  //! The occurrence whose record it concerns.
  std::size_t occurrence = 0;
  //! The node of that occurrence's scene the record belongs to.
  std::optional<pak::world::SceneNodeIndexT> node_index;
  //! The instanced scene, for a missing scene or a cycle.
  AssetKey scene;
};

//! A composed scene: occurrences depth first, the changes that do not
//! resolve and the issues.
struct ComposedScene {
  std::vector<ComposedOccurrence> occurrences;
  std::vector<UnresolvedChange> unresolved;
  std::vector<CompositionIssue> issues;
};

//! Composes `scene`: follows its instances and applies every change.
/*!
 Occurrences list the scene, then one per instance, depth first. A change
 whose remaining path is one element applies to that node; a longer one goes
 to the instance its first element roots. A change that names no node, passes
 through a node that is not an instance root, or sets a property the node has
 no record for, is unresolved. The outermost scene's change wins. Builds no
 scene graph.
*/
OXGN_DATA_NDAPI auto ComposeScene(std::shared_ptr<const SceneAsset> scene,
  const SceneResolver& resolve) -> ComposedScene;

//! What composing one instance needs.
struct InstanceComposition {
  //! The instanced scene; resolved with the resolver.
  AssetKey source;
  //! The instance root in the requester's scene.
  pak::world::SceneNodeIndexT instance_root = 0;
  //! The requester's changes below the root.
  std::span<const SceneChange> changes;
  //! The scenes on the requester's path, itself included: reaching one again
  //! is a cycle.
  std::span<const AssetKey> ancestors;
};

//! Composes one instance with its requester's changes: the first occurrence
//! is the instanced scene; the requester supplies `kRequester`.
OXGN_DATA_NDAPI auto ComposeInstance(const InstanceComposition& instance,
  const SceneResolver& resolve) -> ComposedScene;

//! Recomposes node `index` of an occurrence of `scene` from its scene and the
//! changes that reach it, as the composition that made it did.
OXGN_DATA_NDAPI auto ComposeNode(const ComposedScene& scene,
  std::size_t occurrence, pak::world::SceneNodeIndexT index) -> ComposedNode;

//! Recomposes the script slots of an occurrence of `scene` from its scene and
//! the parameter changes that reach it, as the composition that made them did.
OXGN_DATA_NDAPI auto ComposeSlots(const ComposedScene& scene,
  std::size_t occurrence) -> std::vector<ComposedSlot>;

//! How a change fares on the node it names.
enum class ChangeOutcome : std::uint8_t {
  kApplied,
  //! The node has no record for the property, or no such script slot.
  kUnresolved,
  //! The value fails the validation of the record it patches.
  kInvalid,
};

//! How `change` fares on node `index` of `scene`, from the scene's own
//! records; the change's path is not read.
OXGN_DATA_NDAPI auto CheckChange(const std::shared_ptr<const SceneAsset>& scene,
  pak::world::SceneNodeIndexT index, const SceneChange& change)
  -> ChangeOutcome;

} // namespace oxygen::data
