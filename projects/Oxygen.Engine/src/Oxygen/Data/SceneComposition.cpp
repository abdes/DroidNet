//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneComposition.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/Schema/Generated.SceneChanges.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/ScriptAsset.h>

namespace oxygen::data {

namespace {
  namespace world = pak::world;
  namespace scripting = pak::scripting;
  using schema::ComponentId;

  //! Applies `change` to `record` when the node has one.
  template <typename Record>
  auto ApplyToRecord(std::optional<Record>& record,
    const world::OverrideRecord& change) -> ChangeOutcome
  {
    if (!record.has_value()) {
      return ChangeOutcome::kUnresolved;
    }
    return schema::ApplyChange(*record, change.override_id, change.value)
      ? ChangeOutcome::kApplied
      : ChangeOutcome::kInvalid;
  }

  //! Replaces the entry with the same key, or inserts it.
  template <typename Entry, typename Key>
  auto Upsert(std::vector<Entry>& entries, Entry entry, Key key) -> void
  {
    const auto found = std::ranges::find(entries, std::invoke(key, entry), key);
    if (found != entries.end()) {
      *found = std::move(entry);
      return;
    }
    entries.push_back(std::move(entry));
  }

  auto ApplyProperty(ComposedNode& node, const world::OverrideRecord& change,
    const ChangeSupplier supplier) -> ChangeOutcome
  {
    const auto* property = schema::FindOverrideProperty(change.override_id);
    if (property == nullptr) {
      return ChangeOutcome::kInvalid;
    }
    auto outcome = ChangeOutcome::kInvalid;
    if (property->asset_sourced) {
      if (!node.renderable.has_value()) {
        return ChangeOutcome::kUnresolved;
      }
      auto record = change;
      record.target = { .node_index = node.index };
      Upsert(node.overrides,
        ComposedOverride { .record = record, .supplier = supplier },
        [](const ComposedOverride& entry) -> auto {
          return std::tie(entry.record.override_id, entry.record.part_id);
        });
      outcome = ChangeOutcome::kApplied;
    } else {
      switch (property->component) {
      case ComponentId::kTransform:
      case ComponentId::kNode:
        outcome
          = schema::ApplyChange(node.node, change.override_id, change.value)
          ? ChangeOutcome::kApplied
          : ChangeOutcome::kInvalid;
        break;
      case ComponentId::kPerspectiveCamera:
        outcome = ApplyToRecord(node.perspective_camera, change);
        break;
      case ComponentId::kOrthographicCamera:
        outcome = ApplyToRecord(node.orthographic_camera, change);
        break;
      case ComponentId::kDirectionalLight:
        outcome = ApplyToRecord(node.directional_light, change);
        break;
      case ComponentId::kPointLight:
        outcome = ApplyToRecord(node.point_light, change);
        break;
      case ComponentId::kSpotLight:
        outcome = ApplyToRecord(node.spot_light, change);
        break;
      case ComponentId::kLocalFogVolume:
        outcome = ApplyToRecord(node.local_fog_volume, change);
        break;
      case ComponentId::kInvalid:
        break;
      }
    }
    if (outcome == ChangeOutcome::kApplied) {
      Upsert(node.changes,
        AppliedChange {
          .override_id = change.override_id,
          .part_id = change.part_id,
          .supplier = supplier,
        },
        [](const AppliedChange& entry) -> auto {
          return std::tie(entry.override_id, entry.part_id);
        });
    }
    return outcome;
  }

  auto ApplyParameter(std::vector<ComposedSlot>& slots,
    const world::SceneNodeIndexT node_index, const ParameterChange& change,
    const ChangeSupplier supplier) -> ChangeOutcome
  {
    const auto slot
      = std::ranges::find_if(slots, [&](const ComposedSlot& entry) -> bool {
          return entry.node_index == node_index
            && entry.slot_id == change.slot_id;
        });
    if (slot == slots.end()) {
      return ChangeOutcome::kUnresolved;
    }
    Upsert(slot->parameters,
      ComposedParameter {
        .key = change.key,
        .value = change.value,
        .supplier = supplier,
      },
      &ComposedParameter::key);
    return ChangeOutcome::kApplied;
  }

  //! The nodes of `occurrence`'s scene with their own records and overrides.
  auto BaseNodes(const ComposedOccurrence& occurrence, const std::size_t self)
    -> std::vector<ComposedNode>
  {
    const auto& scene = *occurrence.scene;
    auto nodes = std::vector<ComposedNode> {};
    nodes.reserve(scene.GetNodes().size());
    for (const auto [index, record] : std::views::enumerate(scene.GetNodes())) {
      auto node = ComposedNode {
        .index = static_cast<world::SceneNodeIndexT>(index),
        .node = record,
      };
      if (occurrence.level != 0U) {
        node.path = occurrence.prefix;
        node.path.push_back(record.node_id);
      }
      nodes.push_back(std::move(node));
    }
    const auto attach
      = [&nodes]<typename Record>(std::optional<Record> ComposedNode::* member,
          const std::span<const Record> records) -> void {
      for (const auto& record : records) {
        if (record.node_index < nodes.size()) {
          nodes.at(record.node_index).*member = record;
        }
      }
    };
    attach(&ComposedNode::renderable,
      scene.GetComponents<world::RenderableRecord>());
    attach(&ComposedNode::perspective_camera,
      scene.GetComponents<world::PerspectiveCameraRecord>());
    attach(&ComposedNode::orthographic_camera,
      scene.GetComponents<world::OrthographicCameraRecord>());
    attach(&ComposedNode::directional_light,
      scene.GetComponents<world::DirectionalLightRecord>());
    attach(&ComposedNode::point_light,
      scene.GetComponents<world::PointLightRecord>());
    attach(
      &ComposedNode::spot_light, scene.GetComponents<world::SpotLightRecord>());
    attach(&ComposedNode::local_fog_volume,
      scene.GetComponents<world::LocalFogVolumeRecord>());
    for (const auto& record : scene.GetComponents<world::OverrideRecord>()) {
      if (record.target.node_path == world::kNoNodePath) {
        nodes.at(record.target.node_index)
          .overrides.push_back({ .record = record, .supplier = self });
      }
    }
    return nodes;
  }

  //! The slots of `scene` with their own parameters, supplied by `self`;
  //! none when the scene's script records cannot be read.
  auto ReadSlots(const SceneAsset& scene, const std::size_t self)
    -> std::optional<std::vector<ComposedSlot>>
  {
    auto slots = std::vector<ComposedSlot> {};
    try {
      for (const auto& component :
        scene.GetComponents<scripting::ScriptingComponentRecord>()) {
        for (const auto& slot : scene.ReadScriptSlots(
               component.slot_start_index, component.slot_count)) {
          auto composed = ComposedSlot {
            .node_index = component.node_index,
            .slot_id = slot.slot_id,
            .script = slot.script_asset_key,
            .execution_order = slot.execution_order,
          };
          for (auto& [key, value] : DecodeScriptParameters(
                 scene.ReadScriptParameters(slot), scene.GetStrings())) {
            composed.parameters.push_back({
              .key = std::move(key),
              .value = std::move(value),
              .supplier = self,
            });
          }
          slots.push_back(std::move(composed));
        }
      }
    } catch (const std::exception&) {
      return std::nullopt;
    }
    return slots;
  }

  //! The reaching changes that apply to one node, deepest supplier first.
  auto ChangesOf(const ComposedOccurrence& occurrence, const NodeId& node_id)
    -> std::vector<const ReachingChange*>
  {
    auto changes = std::vector<const ReachingChange*> {};
    for (const auto& reaching : occurrence.changes) {
      const auto& path = reaching.change.path;
      if (path.size() == reaching.consumed + 1U && path.back() == node_id) {
        changes.push_back(&reaching);
      }
    }
    std::ranges::stable_sort(
      changes, std::ranges::greater {}, &ReachingChange::level);
    return changes;
  }

  //! Builds the composed tree depth first.
  class Composer final {
  public:
    explicit Composer(const SceneResolver& resolve)
      : resolve_(&resolve)
    {
    }

    auto Compose(ComposedOccurrence root, std::vector<AssetKey> ancestors)
      -> ComposedScene
    {
      ancestors_ = std::move(ancestors);
      ancestors_.push_back(root.scene->GetAssetKey());
      result_.occurrences.push_back(std::move(root));
      Build(0U);
      return std::move(result_);
    }

  private:
    auto Unresolved(const ReachingChange& reaching) -> void
    {
      result_.unresolved.push_back({
        .change = reaching.change,
        .supplier = reaching.supplier,
        .instance_root = reaching.supplier_root,
      });
    }

    auto Issue(const CompositionIssueCode code, const std::size_t occurrence,
      const std::optional<world::SceneNodeIndexT> node_index,
      const AssetKey& scene = {}) -> void
    {
      result_.issues.push_back({
        .code = code,
        .occurrence = occurrence,
        .node_index = node_index,
        .scene = scene,
      });
    }

    //! The slots of the occurrence's scene, with their own parameters.
    auto BaseSlots(const std::size_t self) -> std::vector<ComposedSlot>
    {
      auto slots = ReadSlots(*result_.occurrences.at(self).scene, self);
      if (!slots.has_value()) {
        Issue(CompositionIssueCode::kRecordInvalid, self, std::nullopt);
        return {};
      }
      return std::move(*slots);
    }

    //! The changes the occurrence's scene makes below its instance roots,
    //! by root.
    auto OwnChanges(const std::size_t self)
      -> std::map<world::SceneNodeIndexT, std::vector<ReachingChange>>
    {
      const auto& occurrence = result_.occurrences.at(self);
      const auto& scene = *occurrence.scene;
      auto changes
        = std::map<world::SceneNodeIndexT, std::vector<ReachingChange>> {};
      const auto add
        = [&](const world::NodeRef& target, SceneChange change) -> void {
        changes[target.node_index].push_back({
          .change = std::move(change),
          .supplier = self,
          .supplier_root = target.node_index,
          .consumed = 0U,
          .level = occurrence.level,
        });
      };
      for (const auto& record : scene.GetComponents<world::OverrideRecord>()) {
        if (record.target.node_path != world::kNoNodePath) {
          add(record.target,
            { .path = scene.GetNodePath(record.target), .change = record });
        }
      }
      for (const auto& record :
        scene.GetComponents<scripting::ScriptParamOverrideRecord>()) {
        const auto key = TryGetScriptParamKey(record.param, scene.GetStrings());
        auto value = ToScriptParam(record.param, scene.GetStrings());
        if (!key.has_value() || !value.has_value()) {
          Issue(CompositionIssueCode::kRecordInvalid, self,
            record.target.node_index);
          continue;
        }
        add(record.target,
          {
            .path = scene.GetNodePath(record.target),
            .change = ParameterChange {
              .slot_id = record.slot_id,
              .key = std::string(*key),
              .value = std::move(*value),
            },
          });
      }
      return changes;
    }

    auto Build(const std::size_t self) -> void
    {
      auto nodes = BaseNodes(result_.occurrences.at(self), self);
      auto slots = BaseSlots(self);
      auto forwarded = OwnChanges(self);
      const auto& scene = *result_.occurrences.at(self).scene;

      auto by_id = std::unordered_map<NodeId, world::SceneNodeIndexT> {};
      for (const auto& node : nodes) {
        by_id.emplace(node.node.node_id, node.index);
      }
      const auto is_root
        = [&scene](const world::SceneNodeIndexT index) -> bool {
        return scene.FindComponent<world::SceneInstanceRecord>(index)
          != nullptr;
      };

      // Applying the deepest level first lets the outermost change win.
      auto reaching = result_.occurrences.at(self).changes;
      std::ranges::stable_sort(
        reaching, std::ranges::greater {}, &ReachingChange::level);
      for (auto& change : reaching) {
        const auto& path = change.change.path;
        const auto found = change.consumed < path.size()
          ? by_id.find(path.at(change.consumed))
          : by_id.end();
        if (found == by_id.end()) {
          Unresolved(change);
          continue;
        }
        const auto index = found->second;
        if (path.size() > change.consumed + 1U) {
          if (!is_root(index)) {
            Unresolved(change);
            continue;
          }
          ++change.consumed;
          forwarded[index].push_back(std::move(change));
          continue;
        }
        const auto outcome = std::visit(
          [&]<typename Change>(const Change& value) -> ChangeOutcome {
            if constexpr (std::is_same_v<Change, ParameterChange>) {
              return ApplyParameter(slots, index, value, change.supplier);
            } else {
              return ApplyProperty(nodes.at(index), value, change.supplier);
            }
          },
          change.change.change);
        if (outcome == ChangeOutcome::kUnresolved) {
          Unresolved(change);
        } else if (outcome == ChangeOutcome::kInvalid) {
          Issue(CompositionIssueCode::kChangeInvalid, self, index);
        }
      }
      {
        auto& occurrence = result_.occurrences.at(self);
        occurrence.nodes = std::move(nodes);
        occurrence.slots = std::move(slots);
      }

      for (const auto& instance :
        scene.GetComponents<world::SceneInstanceRecord>()) {
        BuildInstance(
          self, instance, std::move(forwarded[instance.node_index]));
      }
    }

    auto BuildInstance(const std::size_t parent,
      const world::SceneInstanceRecord& instance,
      std::vector<ReachingChange> changes) -> void
    {
      if (std::ranges::contains(ancestors_, instance.scene_key)) {
        Issue(CompositionIssueCode::kCycle, parent, instance.node_index,
          instance.scene_key);
        return;
      }
      auto scene = (*resolve_)(instance.scene_key);
      if (!scene) {
        Issue(CompositionIssueCode::kSceneMissing, parent, instance.node_index,
          instance.scene_key);
        return;
      }
      const auto& owner = result_.occurrences.at(parent);
      auto child = ComposedOccurrence {
        .scene = std::move(scene),
        .parent = parent,
        .instance_root = instance.node_index,
        .outer_root
        = owner.level == 0U ? instance.node_index : owner.outer_root,
        .prefix = owner.level == 0U ? std::vector<NodeId> {}
                                    : owner.nodes.at(instance.node_index).path,
        .level = owner.level + 1U,
        .changes = std::move(changes),
      };
      const auto self = result_.occurrences.size();
      result_.occurrences.push_back(std::move(child));
      ancestors_.push_back(instance.scene_key);
      Build(self);
      ancestors_.pop_back();
    }

    observer_ptr<const SceneResolver> resolve_;
    std::vector<AssetKey> ancestors_;
    ComposedScene result_;
  };

} // namespace

auto to_string(const CompositionIssueCode code) noexcept -> std::string_view
{
  switch (code) {
  case CompositionIssueCode::kSceneMissing:
    return "scene.instance.missing";
  case CompositionIssueCode::kCycle:
    return "scene.instance.cycle";
  case CompositionIssueCode::kChangeInvalid:
    return "scene.instance.change_invalid";
  case CompositionIssueCode::kRecordInvalid:
    return "scene.instance.record_invalid";
  }
  return "__NotSupported__";
}

auto ComposeScene(std::shared_ptr<const SceneAsset> scene,
  const SceneResolver& resolve) -> ComposedScene
{
  if (!scene) {
    return {};
  }
  return Composer(resolve).Compose(
    ComposedOccurrence {
      .scene = std::move(scene),
    },
    {});
}

auto ComposeInstance(const InstanceComposition& instance,
  const SceneResolver& resolve) -> ComposedScene
{
  auto result = ComposedScene {};
  if (std::ranges::contains(instance.ancestors, instance.source)) {
    result.issues.push_back({
      .code = CompositionIssueCode::kCycle,
      .node_index = instance.instance_root,
      .scene = instance.source,
    });
    return result;
  }
  auto scene = resolve(instance.source);
  if (!scene) {
    result.issues.push_back({
      .code = CompositionIssueCode::kSceneMissing,
      .node_index = instance.instance_root,
      .scene = instance.source,
    });
    return result;
  }
  auto root = ComposedOccurrence {
    .scene = std::move(scene),
    .instance_root = instance.instance_root,
    .outer_root = instance.instance_root,
    .level = 1U,
  };
  for (const auto& change : instance.changes) {
    root.changes.push_back({
      .change = change,
      .supplier = kRequester,
      .supplier_root = instance.instance_root,
      .consumed = 0U,
      .level = 0U,
    });
  }
  return Composer(resolve).Compose(
    std::move(root), { instance.ancestors.begin(), instance.ancestors.end() });
}

auto ComposeNode(const ComposedScene& scene, const std::size_t occurrence,
  const world::SceneNodeIndexT index) -> ComposedNode
{
  const auto& composed = scene.occurrences.at(occurrence);
  auto node = std::move(BaseNodes(composed, occurrence).at(index));
  for (const auto* reaching : ChangesOf(composed, node.node.node_id)) {
    if (const auto* property
      = std::get_if<world::OverrideRecord>(&reaching->change.change)) {
      (void)ApplyProperty(node, *property, reaching->supplier);
    }
  }
  return node;
}

auto ComposeSlots(const ComposedScene& scene, const std::size_t occurrence)
  -> std::vector<ComposedSlot>
{
  const auto& composed = scene.occurrences.at(occurrence);
  const auto& asset = *composed.scene;
  auto slots
    = ReadSlots(asset, occurrence).value_or(std::vector<ComposedSlot> {});
  auto by_id = std::unordered_map<NodeId, world::SceneNodeIndexT> {};
  for (const auto [index, record] : std::views::enumerate(asset.GetNodes())) {
    by_id.emplace(record.node_id, static_cast<world::SceneNodeIndexT>(index));
  }
  auto reaching = std::vector<const ReachingChange*> {};
  for (const auto& change : composed.changes) {
    reaching.push_back(&change);
  }
  std::ranges::stable_sort(
    reaching, std::ranges::greater {}, &ReachingChange::level);
  for (const auto* change : reaching) {
    const auto& path = change->change.path;
    const auto* parameter
      = std::get_if<ParameterChange>(&change->change.change);
    if (parameter == nullptr || path.size() != change->consumed + 1U) {
      continue;
    }
    if (const auto found = by_id.find(path.back()); found != by_id.end()) {
      (void)ApplyParameter(slots, found->second, *parameter, change->supplier);
    }
  }
  return slots;
}

auto AddressOf(const SceneChange& change) -> ChangeAddress
{
  return ChangeAddress {
    .path = change.path,
    .target = std::visit(
      []<typename Change>(const Change& value)
        -> std::variant<PropertyAddress, ParameterAddress> {
        if constexpr (std::is_same_v<Change, ParameterChange>) {
          return ParameterAddress {
            .slot_id = value.slot_id,
            .key = value.key,
          };
        } else {
          return PropertyAddress {
            .override_id = value.override_id,
            .part_id = value.part_id,
          };
        }
      },
      change.change),
  };
}

auto CheckChange(const std::shared_ptr<const SceneAsset>& scene,
  const world::SceneNodeIndexT index, const SceneChange& change)
  -> ChangeOutcome
{
  if (!scene || index >= scene->GetNodes().size()) {
    return ChangeOutcome::kUnresolved;
  }
  return std::visit(
    [&]<typename Change>(const Change& value) -> ChangeOutcome {
      if constexpr (std::is_same_v<Change, ParameterChange>) {
        auto slots = ReadSlots(*scene, 0U);
        if (!slots.has_value()) {
          return ChangeOutcome::kInvalid;
        }
        return ApplyParameter(*slots, index, value, 0U);
      } else {
        auto nodes = BaseNodes(ComposedOccurrence { .scene = scene }, 0U);
        return ApplyProperty(nodes.at(index), value, 0U);
      }
    },
    change.change);
}

} // namespace oxygen::data
