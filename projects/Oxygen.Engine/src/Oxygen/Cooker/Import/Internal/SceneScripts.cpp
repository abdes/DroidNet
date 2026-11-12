//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <expected>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/AssetReferenceBuilder.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/NodePathTable.h>
#include <Oxygen/Cooker/Import/Internal/SceneScripts.h>
#include <Oxygen/Cooker/Import/Internal/ScriptingSidecarSource.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/StableId.h>

namespace oxygen::content::import::internal {
namespace {

  namespace script = data::pak::scripting;
  namespace world = data::pak::world;
  using SidecarParam = ScriptingSidecarParam;

  class SceneDescriptorPatcher final {
  public:
    SceneDescriptorPatcher(const std::span<const std::byte> descriptor,
      const data::SceneAsset& scene, const SceneScriptData& scripts)
      : source_descriptor_(descriptor)
      , scripts_(&scripts)
      , scene_(&scene)
      , environment_offset_(
          descriptor.size() - scene.GetEnvironmentBlockHeader().byte_size)
    {
    }

    auto Patch(std::vector<std::byte>& patched_descriptor, std::string& error)
      -> bool
    {
      if (!ValidateAndParseSource(error)) {
        return false;
      }
      if (!CollectExistingComponentPayloads(error)) {
        return false;
      }
      AppendScriptingComponentPayload();
      CaptureTrailingBytes();
      return SerializePatchedDescriptor(patched_descriptor, error);
    }

  private:
    struct ComponentPayload final {
      uint32_t component_type = 0;
      uint32_t entry_size = 0;
      std::vector<std::byte> bytes;
    };

    [[nodiscard]] auto RangeOk(const uint64_t offset, const uint64_t size) const
      -> bool
    {
      return offset <= source_descriptor_.size()
        && size <= (source_descriptor_.size() - offset);
    }

    auto ValidateAndParseSource(std::string& error) -> bool
    {
      using world::SceneAssetDesc;

      if (source_descriptor_.size() < sizeof(SceneAssetDesc)) {
        error = "Scene descriptor is smaller than SceneAssetDesc";
        return false;
      }

      std::memcpy(
        &source_desc_, source_descriptor_.data(), sizeof(source_desc_));

      node_table_size_ = static_cast<uint64_t>(source_desc_.nodes.count)
        * static_cast<uint64_t>(source_desc_.nodes.entry_size);
      if (!RangeOk(source_desc_.nodes.offset, node_table_size_)) {
        error = "Scene node table range is invalid";
        return false;
      }

      try {
        source_strings_ = data::DescriptorStrings::FromDescriptor(
          source_descriptor_, source_desc_.header.strings);
      } catch (const std::invalid_argument&) {
        error = "Scene string table range is invalid";
        return false;
      }

      payload_end_ = static_cast<uint64_t>(sizeof(SceneAssetDesc));
      payload_end_ = (std::max)(payload_end_,
        source_desc_.nodes.offset + node_table_size_);
      payload_end_ = (std::max)(payload_end_,
        uint64_t { source_desc_.header.strings.offset }
          + source_desc_.header.strings.size);
      return true;
    }

    auto CollectExistingComponentPayloads(std::string& error) -> bool
    {
      using data::ComponentType;
      using world::SceneComponentTableDesc;

      if (source_desc_.component_table_count == 0U) {
        return true;
      }

      const auto directory_size
        = static_cast<uint64_t>(source_desc_.component_table_count)
        * sizeof(SceneComponentTableDesc);
      if (!RangeOk(
            source_desc_.component_table_directory_offset, directory_size)) {
        error = "Scene component directory range is invalid";
        return false;
      }

      payload_end_ = (std::max)(payload_end_,
        source_desc_.component_table_directory_offset + directory_size);

      for (uint32_t i = 0; i < source_desc_.component_table_count; ++i) {
        const auto dir_offset
          = static_cast<size_t>(source_desc_.component_table_directory_offset)
          + (static_cast<size_t>(i) * sizeof(SceneComponentTableDesc));
        auto entry = SceneComponentTableDesc {};
        std::memcpy(
          &entry, source_descriptor_.subspan(dir_offset).data(), sizeof(entry));

        if (entry.table.count == 0U) {
          continue;
        }

        const auto table_size = static_cast<uint64_t>(entry.table.count)
          * static_cast<uint64_t>(entry.table.entry_size);
        if (!RangeOk(entry.table.offset, table_size)) {
          error = "Scene component table range is invalid";
          return false;
        }
        payload_end_
          = (std::max)(payload_end_, entry.table.offset + table_size);

        // The patch writes the scripting tables and the node paths again,
        // with only the paths something still names.
        const auto component_type
          = static_cast<ComponentType>(entry.component_type);
        if (component_type == ComponentType::kScripting
          || component_type == ComponentType::kScriptParamOverride
          || component_type == ComponentType::kNodePath
          || component_type == ComponentType::kNodePathElement) {
          continue;
        }
        if (component_type == ComponentType::kOverride) {
          AppendOverridePayload();
          continue;
        }

        auto payload = ComponentPayload {
          .component_type = entry.component_type,
          .entry_size = entry.table.entry_size,
          .bytes = {},
        };
        payload.bytes.resize(static_cast<size_t>(table_size));
        std::memcpy(payload.bytes.data(),
          source_descriptor_.subspan(static_cast<size_t>(entry.table.offset))
            .data(),
          payload.bytes.size());
        component_payloads_.push_back(std::move(payload));
      }

      return true;
    }

    auto AppendScriptingComponentPayload() -> void
    {
      using data::ComponentType;
      if (scripts_->components.empty()) {
        return;
      }

      auto payload = ComponentPayload {
        .component_type = static_cast<uint32_t>(ComponentType::kScripting),
        .entry_size = sizeof(script::ScriptingComponentRecord),
        .bytes = {},
      };
      payload.bytes.resize(
        scripts_->components.size() * sizeof(script::ScriptingComponentRecord));
      std::memcpy(payload.bytes.data(), scripts_->components.data(),
        payload.bytes.size());
      component_payloads_.push_back(std::move(payload));
    }

    //! The scene's overrides, naming their node paths in the patch's table
    //! and ordered as the scene requires.
    auto AppendOverridePayload() -> void
    {
      const auto source = scene_->GetComponents<world::OverrideRecord>();
      auto overrides
        = std::vector<world::OverrideRecord>(source.begin(), source.end());
      for (auto& record : overrides) {
        if (record.target.node_path != world::kNoNodePath) {
          record.target.node_path
            = node_paths_.Intern(scene_->GetNodePath(record.target));
        }
      }
      std::ranges::sort(
        overrides, {}, [](const world::OverrideRecord& record) -> auto {
          return std::tie(record.target, record.override_id, record.part_id);
        });
      AppendPayload(data::ComponentType::kOverride,
        std::span<const world::OverrideRecord>(overrides));
    }

    template <typename Record>
    auto AppendPayload(
      const data::ComponentType type, std::span<const Record> records) -> void
    {
      if (records.empty()) {
        return;
      }
      const auto bytes = std::as_bytes(records);
      component_payloads_.push_back({
        .component_type = static_cast<uint32_t>(type),
        .entry_size = sizeof(Record),
        .bytes = { bytes.begin(), bytes.end() },
      });
    }

    //! The record of `param` with its strings in `strings` and its node path
    //! in the scene's paths.
    auto WriteParam(const SidecarParam& param,
      data::DescriptorStringsBuilder& strings) -> script::ScriptParamRecord
    {
      auto record = param.record;
      record.key_offset = strings.Add(param.key);
      if (record.type == script::ScriptParamType::kString) {
        record.value.as_string_offset = strings.Add(param.string_value);
      }
      if (record.type == script::ScriptParamType::kNode
        && !param.node_path.empty()) {
        record.value.as_node.node_path = node_paths_.Intern(param.node_path);
      }
      return record;
    }

    auto CaptureTrailingBytes() -> void
    {
      const auto trailing = source_descriptor_.subspan(environment_offset_);
      trailing_bytes_.assign(trailing.begin(), trailing.end());
    }

    auto SerializePatchedDescriptor(
      std::vector<std::byte>& patched_descriptor, std::string& error) -> bool
    {
      using world::SceneComponentTableDesc;

      auto out = std::vector<std::byte> {};
      out.resize(sizeof(world::SceneAssetDesc));

      auto desc = source_desc_;
      desc.nodes.offset = sizeof(world::SceneAssetDesc);
      const auto scene_strings_offset
        = uint64_t { desc.nodes.offset } + node_table_size_;
      if (scene_strings_offset
        > std::numeric_limits<data::pak::core::StringTableOffsetT>::max()) {
        error = "Scene string table offset overflow";
        return false;
      }

      // The slot parameters' strings join the scene's string table; existing
      // strings keep their offsets.
      auto strings = data::DescriptorStringsBuilder { source_strings_ };
      auto params = std::vector<script::ScriptParamRecord> {};
      params.reserve(scripts_->params.size());
      //! A written change, with its key text for ordering.
      struct KeyedChange final {
        script::ScriptParamOverrideRecord record;
        std::string_view key;
      };
      auto keyed = std::vector<KeyedChange> {};
      keyed.reserve(scripts_->instance_changes.size());
      try {
        for (const auto& param : scripts_->params) {
          params.push_back(WriteParam(param, strings));
        }
        for (const auto& change : scripts_->instance_changes) {
          keyed.push_back({
            .record = {
              .target = {
                .node_index = change.node_index,
                .node_path = node_paths_.Intern(change.node_path),
              },
              .slot_id = change.slot_id,
              .param = WriteParam(change.param, strings),
            },
            .key = change.param.key,
          });
        }
      } catch (const std::length_error& limit) {
        error = std::string { "Scene tables exceed the format limit: " }
          + limit.what();
        return false;
      }
      // The scene orders changes by node reference, slot and key; a path's
      // index is known only once it is interned.
      std::ranges::sort(keyed, {}, [](const KeyedChange& change) -> auto {
        return std::tie(
          change.record.target, change.record.slot_id, change.key);
      });
      auto changes = std::vector<script::ScriptParamOverrideRecord> {};
      changes.reserve(keyed.size());
      for (const auto& change : keyed) {
        changes.push_back(change.record);
      }
      AppendPayload(data::ComponentType::kScriptParamOverride,
        std::span<const script::ScriptParamOverrideRecord>(changes));
      if (desc.starting_camera.node_path != world::kNoNodePath) {
        desc.starting_camera.node_path
          = node_paths_.Intern(scene_->GetNodePath(desc.starting_camera));
      }
      AppendPayload(data::ComponentType::kNodePath, node_paths_.Records());
      AppendPayload(
        data::ComponentType::kNodePathElement, node_paths_.Elements());
      std::ranges::sort(component_payloads_,
        [](const ComponentPayload& lhs, const ComponentPayload& rhs) -> bool {
          return lhs.component_type < rhs.component_type;
        });
      desc.header.strings = {
        .offset = static_cast<data::pak::core::StringTableOffsetT>(
          scene_strings_offset),
        .size = strings.Size(),
      };
      desc.component_table_directory_offset = 0;
      desc.component_table_count = 0;

      const auto append_bytes = [&](std::span<const std::byte> bytes) -> void {
        out.insert(out.end(), bytes.begin(), bytes.end());
      };

      const auto source = source_descriptor_;
      append_bytes(
        source.subspan(static_cast<size_t>(source_desc_.nodes.offset),
          static_cast<size_t>(node_table_size_)));
      append_bytes(strings.Bytes());

      if (!component_payloads_.empty()) {
        desc.component_table_directory_offset = out.size();
        desc.component_table_count
          = static_cast<uint32_t>(component_payloads_.size());

        const auto directory_size
          = component_payloads_.size() * sizeof(SceneComponentTableDesc);
        const auto directory_offset = out.size();
        out.resize(out.size() + directory_size);

        auto directory = std::vector<SceneComponentTableDesc> {};
        directory.resize(component_payloads_.size());
        for (size_t i = 0; i < component_payloads_.size(); ++i) {
          auto& entry = directory.at(i);
          entry.component_type = component_payloads_.at(i).component_type;
          entry.table.entry_size = component_payloads_.at(i).entry_size;
          entry.table.count
            = static_cast<uint32_t>(component_payloads_.at(i).entry_size == 0U
                ? 0U
                : component_payloads_.at(i).bytes.size()
                  / component_payloads_.at(i).entry_size);
          entry.table.offset = out.size();
          append_bytes(component_payloads_.at(i).bytes);
        }

        std::memcpy(std::span(out).subspan(directory_offset).data(),
          directory.data(), directory_size);
      }

      desc.script_slots = {};
      if (!scripts_->slots.empty()) {
        if (scripts_->slots.size() > std::numeric_limits<uint32_t>::max()) {
          error = "Scene script slot count exceeds the format limit";
          return false;
        }
        desc.script_slots = {
          .offset = out.size(),
          .count = static_cast<uint32_t>(scripts_->slots.size()),
          .entry_size = sizeof(script::ScriptSlotRecord),
        };
        auto slots = scripts_->slots;
        const auto parameter_base
          = out.size() + (slots.size() * sizeof(script::ScriptSlotRecord));
        for (auto& slot : slots) {
          if (slot.params_count != 0U) {
            slot.params_array_offset += parameter_base;
          } else {
            slot.params_array_offset = 0;
          }
        }
        append_bytes(std::as_bytes(std::span(slots)));
        append_bytes(std::as_bytes(std::span(params)));
      }
      append_bytes(trailing_bytes_);

      std::memcpy(out.data(), &desc, sizeof(desc));
      patched_descriptor = std::move(out);
      return true;
    }

    std::span<const std::byte> source_descriptor_;
    observer_ptr<const SceneScriptData> scripts_;
    observer_ptr<const data::SceneAsset> scene_;
    size_t environment_offset_;
    world::SceneAssetDesc source_desc_ {};
    data::DescriptorStrings source_strings_;
    uint64_t node_table_size_ = 0;
    uint64_t payload_end_ = 0;
    std::vector<ComponentPayload> component_payloads_;
    std::vector<std::byte> trailing_bytes_;
    internal::NodePathTable node_paths_;
  };

  //! The descriptor of the scene `key` the cooked root `root` lists, if it
  //! lists one.
  /*!
   @throw data::CookedFormatError When the root exists and cannot be read.
  */
  auto ListedSceneDescriptor(const cooked::CookerStorageReader& storage,
    const std::filesystem::path& root, const data::AssetKey& key)
    -> std::optional<std::vector<std::byte>>
  {
    auto content = std::optional<cooked::CookedContent> {};
    try {
      content.emplace(cooked::CookedContent::Open(storage, root));
    } catch (const data::CookedFormatError& error) {
      // A root nothing was cooked into yet lists nothing.
      if (error.Defect() == data::CookedDefect::kAbsent) {
        return std::nullopt;
      }
      throw;
    }
    const auto* asset = content->FindAsset(key);
    if (asset == nullptr || asset->type != data::AssetType::kScene) {
      return std::nullopt;
    }
    return content->Descriptor(key);
  }

  //! The index of each node of `scene`, by ID.
  using NodeIndices = std::unordered_map<data::NodeId, uint32_t>;

  auto IndexNodes(const data::SceneAsset& scene) -> NodeIndices
  {
    const auto nodes = scene.GetNodes();
    auto indices = NodeIndices {};
    indices.reserve(nodes.size());
    for (uint32_t index = 0; index < nodes.size(); ++index) {
      indices.emplace(nodes[index].node_id, index);
    }
    return indices;
  }

  //! Matches the nodes of the scene a re-cook replaces with the nodes of the
  //! re-cooked scene, by ID.
  class NodeRebinding final {
  public:
    //! Rebinds the nodes of `listed` to `fresh_indices`, the nodes of the
    //! re-cooked scene (`IndexNodes`).
    NodeRebinding(const data::SceneAsset& listed, NodeIndices fresh_indices,
      const std::string_view source_id,
      std::vector<ImportDiagnostic>& diagnostics)
      : listed_(&listed)
      , source_id_(source_id)
      , diagnostics_(&diagnostics)
      , fresh_indices_(std::move(fresh_indices))
    {
    }

    //! The index in the re-cooked scene of the listed node `listed_index`,
    //! if it still holds that node.
    [[nodiscard]] auto Rebind(const uint32_t listed_index) const
      -> std::optional<uint32_t>
    {
      const auto nodes = listed_->GetNodes();
      if (listed_index >= nodes.size()) {
        return std::nullopt;
      }
      const auto found = fresh_indices_.find(nodes[listed_index].node_id);
      if (found == fresh_indices_.end()) {
        return std::nullopt;
      }
      return found->second;
    }

    //! Rebinds the node value of `param`, a parameter on the listed node
    //! `owner`; a value naming a node the scene no longer holds is unset.
    auto RebindValue(SidecarParam& param, const uint32_t owner) const -> void
    {
      auto& value = param.record.value.as_node;
      if (param.record.type != script::ScriptParamType::kNode
        || value.node_index == script::kUnsetNodeParam) {
        return;
      }
      if (const auto rebound = Rebind(value.node_index)) {
        value.node_index = *rebound;
        return;
      }
      Warn(import_codes::kSceneScriptsNodeValueMissing,
        "The script parameter '" + param.key + "' of node '" + NodeName(owner)
          + "' named node '" + NodeName(value.node_index)
          + "', which the scene no longer holds; it is unset");
      value.node_index = script::kUnsetNodeParam;
      value.node_path = world::kNoNodePath;
      param.node_path.clear();
    }

    //! The name of the listed node `listed_index`, its ID when it has none.
    [[nodiscard]] auto NodeName(const uint32_t listed_index) const
      -> std::string
    {
      const auto nodes = listed_->GetNodes();
      if (listed_index >= nodes.size()) {
        return std::to_string(listed_index);
      }
      const auto& node = nodes[listed_index];
      const auto name = listed_->GetNodeName(node);
      return name.empty() ? data::to_string(node.node_id) : std::string(name);
    }

    auto Warn(const std::string_view code, std::string message) const -> void
    {
      diagnostics_->push_back({
        .severity = ImportSeverity::kWarning,
        .code = std::string(code),
        .message = std::move(message),
        .source_path = std::string(source_id_),
      });
    }

  private:
    observer_ptr<const data::SceneAsset> listed_;
    std::string_view source_id_;
    observer_ptr<std::vector<ImportDiagnostic>> diagnostics_;
    NodeIndices fresh_indices_;
  };

  //! Orders changes by node, path, slot and key, as `SceneScriptData` holds
  //! them.
  auto ChangeOrder(const InstanceParamChange& change) -> auto
  {
    return std::tie(
      change.node_index, change.node_path, change.slot_id, change.param.key);
  }

} // namespace

auto DecodeScriptParam(const data::pak::scripting::ScriptParamRecord& record,
  const data::DescriptorStrings& strings, const data::SceneAsset* scene)
  -> std::optional<ScriptingSidecarParam>
{
  const auto key = data::TryGetScriptParamKey(record, strings);
  if (!key) {
    return std::nullopt;
  }
  auto param
    = ScriptingSidecarParam { .record = record, .key = std::string { *key } };
  if (scene != nullptr
    && record.type == data::pak::scripting::ScriptParamType::kNode
    && record.value.as_node.node_index
      != data::pak::scripting::kUnsetNodeParam) {
    param.node_path = scene->GetNodePath(record.value.as_node);
  }
  if (record.type == data::pak::scripting::ScriptParamType::kString) {
    const auto value = strings.TryGet(record.value.as_string_offset);
    if (!value) {
      return std::nullopt;
    }
    param.string_value = std::string { *value };
  }
  return param;
}

auto ReadSceneScriptBindings(const data::SceneAsset& scene)
  -> SceneScriptBindings
{
  auto bindings = SceneScriptBindings {};
  for (const auto& component :
    scene.GetComponents<data::pak::scripting::ScriptingComponentRecord>()) {
    auto& slots = bindings[component.node_index];
    for (const auto& slot :
      scene.ReadScriptSlots(component.slot_start_index, component.slot_count)) {
      auto params = std::vector<SidecarParam> {};
      for (const auto& record : scene.ReadScriptParameters(slot)) {
        auto param = DecodeScriptParam(record, scene.GetStrings(), &scene);
        if (!param) {
          throw std::runtime_error(
            "scene script parameter has an invalid key or value");
        }
        params.push_back(std::move(*param));
      }
      slots.push_back({
        .slot = slot,
        .params = std::move(params),
      });
    }
  }
  return bindings;
}

auto ReadSceneInstanceChanges(const data::SceneAsset& scene)
  -> std::vector<InstanceParamChange>
{
  auto changes = std::vector<InstanceParamChange> {};
  for (const auto& record :
    scene.GetComponents<data::pak::scripting::ScriptParamOverrideRecord>()) {
    auto param = DecodeScriptParam(record.param, scene.GetStrings(), &scene);
    if (!param) {
      throw std::runtime_error(
        "scene script parameter change has an invalid key or value");
    }
    changes.push_back({
      .node_index = record.target.node_index,
      .node_path = scene.GetNodePath(record.target),
      .slot_id = record.slot_id,
      .param = std::move(*param),
    });
  }
  return changes;
}

auto SerializeSceneScriptBindings(const SceneScriptBindings& bindings)
  -> std::expected<SceneScriptData, SceneScriptsOverflow>
{
  using data::pak::core::OffsetT;

  auto serialized = SceneScriptData {};
  for (const auto& [node_index, slots] : bindings) {
    if (slots.empty()) {
      continue;
    }
    if (serialized.slots.size() > std::numeric_limits<uint32_t>::max()) {
      return std::unexpected(SceneScriptsOverflow {
        .code = import_codes::kScriptSidecarSlotCountOverflow,
        .message = "Scripting slot count exceeded uint32 limits",
      });
    }
    if (slots.size() > std::numeric_limits<uint32_t>::max()) {
      return std::unexpected(SceneScriptsOverflow {
        .code = import_codes::kScriptSidecarSlotCountOverflow,
        .message = "One scene node has too many script slots",
      });
    }

    if (slots.size()
      > std::numeric_limits<uint32_t>::max() - serialized.slots.size()) {
      return std::unexpected(SceneScriptsOverflow {
        .code = import_codes::kScriptSidecarSlotCountOverflow,
        .message = "Scene has too many script slots",
      });
    }
    serialized.components.push_back({
      .node_index = node_index,
      .flags = data::pak::scripting::ScriptingComponentFlags::kNone,
      .slot_start_index = static_cast<uint32_t>(serialized.slots.size()),
      .slot_count = static_cast<uint32_t>(slots.size()),
    });

    for (const auto& slot_payload : slots) {
      auto slot = slot_payload.slot;
      const auto param_offset = uint64_t { serialized.params.size() }
        * sizeof(data::pak::scripting::ScriptParamRecord);
      if (param_offset > std::numeric_limits<OffsetT>::max()) {
        return std::unexpected(SceneScriptsOverflow {
          .code = import_codes::kScriptSidecarParamOffsetOverflow,
          .message = "Script param offset exceeded OffsetT limits",
        });
      }
      if (slot_payload.params.size() > std::numeric_limits<uint32_t>::max()) {
        return std::unexpected(SceneScriptsOverflow {
          .code = import_codes::kScriptSidecarParamCountOverflow,
          .message = "Script param count exceeded uint32 limits",
        });
      }
      slot.params_array_offset
        = slot_payload.params.empty() ? 0U : static_cast<OffsetT>(param_offset);
      slot.params_count = static_cast<uint32_t>(slot_payload.params.size());
      serialized.slots.push_back(slot);
      serialized.params.insert(serialized.params.end(),
        slot_payload.params.begin(), slot_payload.params.end());
    }
  }

  return serialized;
}

auto PatchSceneScripts(const std::span<const std::byte> descriptor,
  const data::SceneAsset& scene, const SceneScriptData& scripts)
  -> std::expected<std::vector<std::byte>, std::string>
{
  auto patched = std::vector<std::byte> {};
  auto error = std::string {};
  auto patcher = SceneDescriptorPatcher(descriptor, scene, scripts);
  if (!patcher.Patch(patched, error)) {
    return std::unexpected(std::move(error));
  }
  return patched;
}

auto AddSceneScriptReferences(
  AssetReferenceBuilder& references, const SceneScriptData& scripts) -> void
{
  for (const auto& slot : scripts.slots) {
    references.AddAsset(slot.script_asset_key, data::AssetType::kScript);
  }
  const auto add_asset = [&references](const SidecarParam& param) -> void {
    const auto& value = param.record.value;
    if (param.record.type == script::ScriptParamType::kAsset
      && !value.as_asset.key.IsNil()) {
      references.AddAsset(value.as_asset.key, value.as_asset.asset_type);
    }
  };
  std::ranges::for_each(scripts.params, add_asset);
  for (const auto& change : scripts.instance_changes) {
    add_asset(change.param);
  }
}

auto CarrySceneScripts(const data::SceneAsset& listed,
  const data::SceneAsset& fresh,
  const std::span<const std::byte> fresh_descriptor,
  const data::AssetReferences& fresh_references,
  const std::string_view source_id, std::vector<ImportDiagnostic>& diagnostics)
  -> std::expected<std::optional<CarriedSceneScripts>, std::string>
{
  auto listed_bindings = ReadSceneScriptBindings(listed);
  auto listed_changes = ReadSceneInstanceChanges(listed);
  if (listed_bindings.empty() && listed_changes.empty()) {
    return std::nullopt;
  }

  const auto nodes
    = NodeRebinding(listed, IndexNodes(fresh), source_id, diagnostics);
  // A node the re-cooked scene scripts itself keeps its own scripts.
  auto bindings = ReadSceneScriptBindings(fresh);
  for (auto& [listed_index, slots] : listed_bindings) {
    const auto node = nodes.Rebind(listed_index);
    if (!node.has_value()) {
      nodes.Warn(import_codes::kSceneScriptsNodeMissing,
        "The scripts of node '" + nodes.NodeName(listed_index)
          + "' are dropped: the scene no longer holds the node");
      continue;
    }
    for (auto& slot : slots) {
      for (auto& param : slot.params) {
        nodes.RebindValue(param, listed_index);
      }
    }
    bindings.try_emplace(*node, std::move(slots));
  }

  auto scripts = SerializeSceneScriptBindings(bindings);
  if (!scripts.has_value()) {
    return std::unexpected(std::move(scripts.error().message));
  }
  scripts->instance_changes = ReadSceneInstanceChanges(fresh);
  const auto own_change_count = scripts->instance_changes.size();
  for (auto& change : listed_changes) {
    const auto listed_index = change.node_index;
    const auto node = nodes.Rebind(listed_index);
    if (!node.has_value()) {
      nodes.Warn(import_codes::kSceneScriptsChangeNodeMissing,
        "The change of script parameter '" + change.param.key + "' of node '"
          + nodes.NodeName(listed_index)
          + "' is dropped: the scene no longer holds the node");
      continue;
    }
    change.node_index = *node;
    nodes.RebindValue(change.param, listed_index);
    const auto own
      = std::span(scripts->instance_changes).first(own_change_count);
    const auto changed_here = std::ranges::any_of(
      own, [&change](const InstanceParamChange& held) -> bool {
        return ChangeOrder(held) == ChangeOrder(change);
      });
    if (!changed_here) {
      scripts->instance_changes.push_back(std::move(change));
    }
  }
  std::ranges::sort(scripts->instance_changes,
    [](const InstanceParamChange& lhs, const InstanceParamChange& rhs) -> bool {
      return ChangeOrder(lhs) < ChangeOrder(rhs);
    });

  auto patched = PatchSceneScripts(fresh_descriptor, fresh, *scripts);
  if (!patched.has_value()) {
    return std::unexpected(std::move(patched.error()));
  }
  auto references = AssetReferenceBuilder(fresh_references);
  AddSceneScriptReferences(references, *scripts);
  return CarriedSceneScripts {
    .descriptor = std::move(*patched),
    .references = std::move(references).Build(),
  };
}

auto KeepListedSceneScripts(ImportSession& session,
  const data::AssetKey& scene_key, const std::string_view source_id,
  std::vector<std::byte>& descriptor, data::AssetReferences& references) -> bool
{
  const auto fail = [&session, source_id](const std::string_view why) -> bool {
    session.AddDiagnostic({
      .severity = ImportSeverity::kError,
      .code = std::string(import_codes::kSceneScriptsCarryFailed),
      .message
      = "The scripts of the scene this import replaces cannot be kept: "
        + std::string(why),
      .source_path = std::string(source_id),
    });
    return false;
  };
  try {
    const auto listed_descriptor = ListedSceneDescriptor(
      *session.Storage(), session.CookedRoot(), scene_key);
    if (!listed_descriptor.has_value()) {
      return true;
    }
    auto diagnostics = std::vector<ImportDiagnostic> {};
    auto carried
      = CarrySceneScripts(data::SceneAsset(scene_key,
                            std::span<const std::byte>(*listed_descriptor)),
        data::SceneAsset(scene_key, std::span<const std::byte>(descriptor)),
        descriptor, references, source_id, diagnostics);
    for (auto& diagnostic : diagnostics) {
      session.AddDiagnostic(std::move(diagnostic));
    }
    if (!carried.has_value()) {
      return fail(carried.error());
    }
    if (carried->has_value()) {
      descriptor = std::move((*carried)->descriptor);
      references = std::move((*carried)->references);
    }
    return true;
  } catch (const std::exception& error) {
    return fail(error.what());
  }
}

} // namespace oxygen::content::import::internal
