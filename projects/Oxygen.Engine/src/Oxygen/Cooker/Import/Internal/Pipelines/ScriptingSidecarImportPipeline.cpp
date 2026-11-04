//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <expected>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/Internal/AssetReferenceBuilder.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedIndexRegistry.h>
#include <Oxygen/Cooker/Import/Internal/NodePathTable.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/ScriptingSidecarImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/ScriptingSidecarSource.h>
#include <Oxygen/Cooker/Import/Internal/SidecarSceneResolver.h>
#include <Oxygen/Cooker/Import/Internal/Utils/MountedCookedSources.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneComposition.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/StableId.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Serio/ByteReader.h>

namespace oxygen::content::import {
namespace {

  namespace script = data::pak::scripting;
  namespace world = data::pak::world;
  using SidecarResolvedSceneState = detail::ResolvedSceneState;

  //! The prefix of the sidecar's codes, such as `index_load_failed`.
  const auto kScriptSidecarResolverDiagnostics
    = detail::SidecarSceneResolverDiagnostics {
        .inflight_target_scene_ambiguous_code
        = import_codes::kScriptSidecarInflightTargetSceneAmbiguous,
        .target_scene_invalid_code
        = import_codes::kScriptSidecarTargetSceneInvalid,
        .target_scene_not_scene_code
        = import_codes::kScriptSidecarTargetSceneNotScene,
        .target_scene_read_failed_code
        = import_codes::kScriptSidecarTargetSceneReadFailed,
        .target_scene_virtual_path_invalid_code
        = import_codes::kScriptSidecarTargetSceneVirtualPathInvalid,
        .target_scene_missing_code
        = import_codes::kScriptSidecarTargetSceneMissing,
      };

  auto AddDiagnostic(ImportSession& session, const ImportRequest& request,
    const ImportSeverity severity, std::string code, std::string message)
    -> void
  {
    session.AddDiagnostic({
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = request.source_path.string(),
    });
  }

  //! A location in the sidecar document.
  using JsonPointer = nlohmann::json::json_pointer;

  auto AddDiagnosticAtPath(ImportSession& session, const ImportRequest& request,
    const ImportSeverity severity, std::string code, std::string message,
    const JsonPointer& location) -> void
  {
    session.AddDiagnostic({
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = request.source_path.string(),
      .json_pointer = location.to_string(),
    });
  }

  using SidecarParam = ScriptingSidecarParam;
  using SidecarBindingRow = ScriptingSidecarBindingRow;
  using SidecarInstanceRow = ScriptingSidecarInstanceRow;
  using SidecarDocument = ScriptingSidecarDocument;

  //! One change of a script parameter of an instanced node.
  struct InstanceParamChange final {
    uint32_t node_index = 0;
    std::vector<data::NodeId> node_path;
    data::ScriptSlotId slot_id;
    SidecarParam param;
    //! The sidecar location its diagnostics name; empty for a change the
    //! scene already holds.
    std::string json_pointer;
  };

  //! Cooked scenes by key.
  using SceneMap = std::unordered_map<data::AssetKey,
    std::shared_ptr<const data::SceneAsset>>;

  //! Parameter declarations of each script a sidecar references or a slot of
  //! an instanced scene runs.
  using ScriptDeclarationMap
    = std::map<data::AssetKey, std::vector<SidecarParam>>;

  struct SlotWithParams final {
    data::pak::scripting::ScriptSlotRecord slot = {};
    std::vector<SidecarParam> params;
  };

  struct SceneScriptData final {
    std::vector<script::ScriptingComponentRecord> components;
    std::vector<data::pak::scripting::ScriptSlotRecord> slots;
    std::vector<SidecarParam> params;
    //! Sorted by node, path, slot and key.
    std::vector<InstanceParamChange> instance_changes;
  };

  //! Decodes a cooked record with the string table of its descriptor, and the
  //! path of a node value of `scene`, when the record belongs to one.
  /*!
   @return The parameter, or nullopt when its key or value is not in the table.
  */
  auto DecodeParam(const data::pak::scripting::ScriptParamRecord& record,
    const data::DescriptorStrings& strings,
    const data::SceneAsset* scene = nullptr) -> std::optional<SidecarParam>
  {
    const auto key = data::TryGetScriptParamKey(record, strings);
    if (!key) {
      return std::nullopt;
    }
    auto param = SidecarParam { .record = record, .key = std::string { *key } };
    if (scene != nullptr
      && record.type == data::pak::scripting::ScriptParamType::kNode
      && record.value.as_node.node_index != script::kUnsetNodeParam) {
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

  class SceneDescriptorPatcher final {
  public:
    SceneDescriptorPatcher(const SidecarResolvedSceneState& scene_state,
      const data::SceneAsset& scene, const SceneScriptData& scripts)
      : source_descriptor_(scene_state.source_scene_descriptor)
      , scripts_(&scripts)
      , scene_(&scene)
      , environment_offset_(scene_state.environment_offset)
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

  struct MountedAsset final {
    observer_ptr<const internal::MountedCookedSource> context;
    observer_ptr<const cooked::CookedAsset> entry;
  };

  //! Finds an asset in the mounted cooked roots, the last mounted one first.
  auto FindMountedAsset(
    std::span<const internal::MountedCookedSource> cooked_contexts,
    const data::AssetKey& key) -> std::optional<MountedAsset>
  {
    for (const auto& context : std::views::reverse(cooked_contexts)) {
      if (!context.content.has_value()) {
        continue;
      }
      if (const auto* asset = context.content->FindAsset(key);
        asset != nullptr) {
        return MountedAsset {
          .context = observer_ptr { &context },
          .entry = observer_ptr { asset },
        };
      }
    }
    return std::nullopt;
  }

  //! Reads the parameter declarations of a cooked script descriptor.
  auto DecodeScriptDeclarations(std::span<const std::byte> bytes)
    -> std::optional<std::vector<SidecarParam>>
  {
    const auto desc_read = serio::CopyAt<script::ScriptAssetDesc>(bytes);
    if (!desc_read) {
      return std::nullopt;
    }
    const auto& desc = *desc_read;
    if (static_cast<data::AssetType>(desc.header.asset_type)
        != data::AssetType::kScript
      || desc.header.version != script::kScriptAssetVersion) {
      return std::nullopt;
    }

    auto strings = data::DescriptorStrings {};
    try {
      strings
        = data::DescriptorStrings::FromDescriptor(bytes, desc.header.strings);
    } catch (const std::invalid_argument&) {
      return std::nullopt;
    }

    const auto& table = desc.params;
    auto declarations = std::vector<SidecarParam> {};
    if (table.count == 0U) {
      return declarations;
    }
    if (table.entry_size != sizeof(script::ScriptParamRecord)
      || table.offset < sizeof(desc) || table.offset > bytes.size()
      || (bytes.size() - table.offset) / sizeof(script::ScriptParamRecord)
        < table.count) {
      return std::nullopt;
    }
    auto records = std::vector<script::ScriptParamRecord>(table.count);
    std::memcpy(records.data(), bytes.subspan(table.offset).data(),
      records.size() * sizeof(script::ScriptParamRecord));
    declarations.reserve(records.size());
    for (const auto& record : records) {
      auto declaration = DecodeParam(record, strings);
      if (!declaration) {
        return std::nullopt;
      }
      declarations.push_back(std::move(*declaration));
    }
    return declarations;
  }

  //! Loads the declarations of every script the sidecar binds.
  /*!
   Rows whose script does not resolve are skipped here; the scene builder
   reports them with their JSON paths.
  */
  auto LoadScriptDeclarations(ImportSession& session,
    const ImportRequest& request,
    std::span<const internal::MountedCookedSource> cooked_contexts,
    const SidecarDocument& parsed) -> std::optional<ScriptDeclarationMap>
  {
    auto declarations = ScriptDeclarationMap {};
    for (const auto& row : parsed.rows) {
      const auto reference
        = internal::ResolveJobReference(request, row.script_virtual_path);
      if (!reference.has_value()) {
        continue;
      }
      const auto found = internal::LookupReference(
        cooked_contexts, request, *reference, data::AssetType::kScript);
      if (!found.has_value() || declarations.contains(found->key)) {
        continue;
      }
      const auto key = found->key;
      const auto mounted = FindMountedAsset(cooked_contexts, key);
      if (!mounted) {
        continue;
      }

      const auto location = JsonPointer {} / "bindings" / row.source_index
        / "script_virtual_path";
      const auto read
        = detail::ReadCookedDescriptor(*mounted->context, mounted->entry->key);
      if (!read.has_value()) {
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          std::string(import_codes::kScriptSidecarScriptDeclarationsUnreadable),
          "Failed to read the script descriptor", location);
        continue;
      }
      auto decoded = DecodeScriptDeclarations(read.value());
      if (!decoded) {
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          std::string(import_codes::kScriptSidecarScriptDeclarationsUnreadable),
          "Script descriptor is invalid or not in the current format; recook "
          "the script",
          location);
        continue;
      }
      declarations.emplace(key, std::move(*decoded));
    }
    if (session.HasErrors()) {
      return std::nullopt;
    }
    return declarations;
  }

  //! The cooked scenes reachable through `scene`'s instances. A scene that is
  //! not mounted or cannot be read is absent, and changes below it are not
  //! checked: its own cook reports it.
  auto ReadInstancedScenes(
    std::span<const internal::MountedCookedSource> cooked_contexts,
    const data::SceneAsset& scene) -> SceneMap
  {
    auto scenes = SceneMap {};
    auto pending = std::vector<data::AssetKey> {};
    for (const auto& record :
      scene.GetComponents<world::SceneInstanceRecord>()) {
      pending.push_back(record.scene_key);
    }
    auto visited = std::unordered_set<data::AssetKey> { scene.GetAssetKey() };
    while (!pending.empty()) {
      const auto key = pending.back();
      pending.pop_back();
      if (!visited.insert(key).second) {
        continue;
      }
      const auto mounted = FindMountedAsset(cooked_contexts, key);
      if (!mounted) {
        continue;
      }
      auto read = detail::ReadCookedDescriptor(*mounted->context, key);
      if (!read.has_value()) {
        continue;
      }
      try {
        auto instanced
          = std::make_shared<const data::SceneAsset>(key, std::move(*read));
        for (const auto& record :
          instanced->GetComponents<world::SceneInstanceRecord>()) {
          pending.push_back(record.scene_key);
        }
        scenes.emplace(key, std::move(instanced));
      } catch (const std::exception&) {
        continue;
      }
    }
    return scenes;
  }

  //! Adds the declarations of the scripts the slots of `instanced` run.
  /*!
   A script that is not mounted, or cannot be read, is left out; a change that
   reaches its slot is reported then.
  */
  auto AddInstancedScriptDeclarations(
    std::span<const internal::MountedCookedSource> cooked_contexts,
    const SceneMap& instanced, ScriptDeclarationMap& declarations) -> void
  {
    for (const auto& scene : instanced | std::views::values) {
      for (const auto& component :
        scene->GetComponents<script::ScriptingComponentRecord>()) {
        for (const auto& slot : scene->ReadScriptSlots(
               component.slot_start_index, component.slot_count)) {
          const auto& key = slot.script_asset_key;
          if (declarations.contains(key)) {
            continue;
          }
          const auto mounted = FindMountedAsset(cooked_contexts, key);
          if (!mounted || mounted->entry->type != data::AssetType::kScript) {
            continue;
          }
          const auto read
            = detail::ReadCookedDescriptor(*mounted->context, key);
          if (!read.has_value()) {
            continue;
          }
          if (auto decoded = DecodeScriptDeclarations(read.value())) {
            declarations.emplace(key, std::move(*decoded));
          }
        }
      }
    }
  }

  auto BuildBindingsByNodeFromComponents(const data::SceneAsset& scene)
    -> std::map<uint32_t, std::vector<SlotWithParams>>
  {
    std::map<uint32_t, std::vector<SlotWithParams>> bindings;
    for (const auto& component :
      scene.GetComponents<script::ScriptingComponentRecord>()) {
      auto& slots = bindings[component.node_index];
      for (const auto& slot : scene.ReadScriptSlots(
             component.slot_start_index, component.slot_count)) {
        auto params = std::vector<SidecarParam> {};
        for (const auto& record : scene.ReadScriptParameters(slot)) {
          auto param = DecodeParam(record, scene.GetStrings(), &scene);
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

  //! The scene's changes of the script parameters of instanced nodes.
  auto CollectInstanceChanges(const data::SceneAsset& scene)
    -> std::vector<InstanceParamChange>
  {
    auto changes = std::vector<InstanceParamChange> {};
    for (const auto& record :
      scene.GetComponents<script::ScriptParamOverrideRecord>()) {
      auto param = DecodeParam(record.param, scene.GetStrings(), &scene);
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

  auto SerializeBindingsByNode(ImportSession& session,
    const ImportRequest& request,
    const std::map<uint32_t, std::vector<SlotWithParams>>& bindings)
    -> std::optional<SceneScriptData>
  {
    using data::pak::core::OffsetT;

    auto serialized = SceneScriptData {};
    for (const auto& [node_index, slots] : bindings) {
      if (slots.empty()) {
        continue;
      }
      if (serialized.slots.size() > std::numeric_limits<uint32_t>::max()) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          std::string(import_codes::kScriptSidecarSlotCountOverflow),
          "Scripting slot count exceeded uint32 limits");
        return std::nullopt;
      }
      if (slots.size() > std::numeric_limits<uint32_t>::max()) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          std::string(import_codes::kScriptSidecarSlotCountOverflow),
          "One scene node has too many script slots");
        return std::nullopt;
      }

      if (slots.size()
        > std::numeric_limits<uint32_t>::max() - serialized.slots.size()) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          std::string(import_codes::kScriptSidecarSlotCountOverflow),
          "Scene has too many script slots");
        return std::nullopt;
      }
      serialized.components.push_back({
        .node_index = node_index,
        .flags = script::ScriptingComponentFlags::kNone,
        .slot_start_index = static_cast<uint32_t>(serialized.slots.size()),
        .slot_count = static_cast<uint32_t>(slots.size()),
      });

      for (const auto& slot_payload : slots) {
        auto slot = slot_payload.slot;
        const auto param_offset = uint64_t { serialized.params.size() }
          * sizeof(script::ScriptParamRecord);
        if (param_offset > std::numeric_limits<OffsetT>::max()) {
          AddDiagnostic(session, request, ImportSeverity::kError,
            std::string(import_codes::kScriptSidecarParamOffsetOverflow),
            "Script param offset exceeded OffsetT limits");
          return std::nullopt;
        }
        if (slot_payload.params.size() > std::numeric_limits<uint32_t>::max()) {
          AddDiagnostic(session, request, ImportSeverity::kError,
            std::string(import_codes::kScriptSidecarParamCountOverflow),
            "Script param count exceeded uint32 limits");
          return std::nullopt;
        }
        slot.params_array_offset = slot_payload.params.empty()
          ? 0U
          : static_cast<OffsetT>(param_offset);
        slot.params_count = static_cast<uint32_t>(slot_payload.params.size());
        serialized.slots.push_back(slot);
        serialized.params.insert(serialized.params.end(),
          slot_payload.params.begin(), slot_payload.params.end());
      }
    }

    return serialized;
  }

  class SceneScriptBuilder final {
  public:
    SceneScriptBuilder(ImportSession& session, const ImportRequest& request,
      const SidecarDocument& parsed, const uint32_t node_count,
      std::span<const internal::MountedCookedSource> cooked_contexts,
      const data::SceneAsset& scene, const ScriptDeclarationMap& declarations,
      const SceneMap& instanced)
      : session_(&session)
      , request_(&request)
      , parsed_(&parsed)
      , node_count_(node_count)
      , cooked_contexts_(cooked_contexts)
      , scene_(&scene)
      , declarations_(&declarations)
      , instanced_(&instanced)
    {
    }

    auto Build() -> std::optional<SceneScriptData>
    {
      using data::pak::core::OffsetT;

      auto merged_bindings = BuildBindingsByNodeFromComponents(*scene_);

      auto incoming_bindings
        = std::map<uint32_t, std::vector<SlotWithParams>> {};
      for (const auto& row : parsed_->rows) {
        const auto location = JsonPointer {} / "bindings" / row.source_index;
        if (row.node_index >= node_count_) {
          AddDiagnosticAtPath(*session_, *request_, ImportSeverity::kError,
            std::string(import_codes::kScriptSidecarNodeRefUnresolved),
            "Node index is out of bounds for target scene",
            location / "node_index");
          continue;
        }

        const auto script_reference
          = internal::ResolveJobReference(*request_, row.script_virtual_path);
        if (!script_reference.has_value()) {
          session_->AddDiagnostic(internal::RefuseReference(*request_,
            row.script_virtual_path, script_reference.error(),
            (location / "script_virtual_path").to_string()));
          continue;
        }
        const auto found = internal::LookupReference(cooked_contexts_,
          *request_, *script_reference, data::AssetType::kScript);
        if (!found.has_value()) {
          ReportRefusedReference(*script_reference, found.error(),
            {
              .subject = "The script",
              .missing = import_codes::kScriptSidecarScriptRefUnresolved,
              .type_mismatch
              = import_codes::kScriptSidecarScriptRefNotScriptAsset,
            },
            location / "script_virtual_path");
          continue;
        }
        const auto resolved_script_key = found->key;

        const auto declared = declarations_->find(resolved_script_key);
        if (declared == declarations_->end()) {
          AddDiagnosticAtPath(*session_, *request_, ImportSeverity::kError,
            std::string(
              import_codes::kScriptSidecarScriptDeclarationsUnreadable),
            "Script parameter declarations are unavailable",
            location / "script_virtual_path");
          continue;
        }
        auto params = ValidateOverrides(row, location, declared->second);
        if (!params) {
          continue;
        }

        auto payload = SlotWithParams {};
        payload.slot.slot_id = row.slot_id;
        payload.slot.script_asset_key = resolved_script_key;
        payload.slot.params_array_offset = 0;
        payload.slot.params_count = 0;
        payload.slot.execution_order = row.execution_order;
        payload.slot.flags = script::ScriptSlotFlags::kNone;
        payload.params = std::move(*params);
        incoming_bindings[row.node_index].push_back(std::move(payload));
      }

      if (session_->HasErrors()) {
        return std::nullopt;
      }

      for (auto& [node_index, slots] : incoming_bindings) {
        std::ranges::sort(slots, {},
          [](const SlotWithParams& slot) -> auto { return slot.slot.slot_id; });
        merged_bindings.insert_or_assign(node_index, std::move(slots));
      }

      auto changes = BuildInstanceChanges();
      if (!changes) {
        return std::nullopt;
      }
      auto serialized
        = SerializeBindingsByNode(*session_, *request_, merged_bindings);
      if (serialized) {
        serialized->instance_changes = std::move(*changes);
      }
      return serialized;
    }

  private:
    [[nodiscard]] auto IsInstanceRoot(const uint32_t node_index) const -> bool
    {
      return scene_->FindComponent<world::SceneInstanceRecord>(node_index)
        != nullptr;
    }

    //! Checks a node value: an index of the scene, and with a path, an
    //! instance root.
    auto CheckNodeValue(const SidecarParam& param, const std::string& key,
      const JsonPointer& value_location) -> bool
    {
      const auto node_index = param.record.value.as_node.node_index;
      if (node_index >= node_count_) {
        AddDiagnosticAtPath(*session_, *request_, ImportSeverity::kError,
          std::string(import_codes::kScriptSidecarParamNodeOutOfRange),
          "Node index " + std::to_string(node_index) + " of parameter '" + key
            + "' is out of bounds for target scene",
          value_location);
        return false;
      }
      if (!param.node_path.empty() && !IsInstanceRoot(node_index)) {
        AddDiagnosticAtPath(*session_, *request_, ImportSeverity::kError,
          std::string(import_codes::kScriptSidecarInstanceRootInvalid),
          "Parameter '" + key
            + "' names a node path below a node that instances no scene",
          value_location);
        return false;
      }
      return true;
    }

    //! The scene's changes of instanced nodes' script parameters: those of
    //! the instance roots the sidecar names are replaced by its rows. A
    //! change that does not resolve is left out with a warning; values are
    //! checked against the slot's script when the scene is instantiated.
    auto BuildInstanceChanges()
      -> std::optional<std::vector<InstanceParamChange>>
    {
      auto changes = std::vector<InstanceParamChange> {};
      auto roots = std::set<uint32_t> {};
      for (const auto& row : parsed_->instance_rows) {
        roots.insert(row.node_index);
      }
      for (auto& change : CollectInstanceChanges(*scene_)) {
        if (!roots.contains(change.node_index)) {
          changes.push_back(std::move(change));
        }
      }
      auto valid = true;
      for (const auto& row : parsed_->instance_rows) {
        const auto location
          = JsonPointer {} / "instance_bindings" / row.source_index;
        if (row.node_index >= node_count_ || !IsInstanceRoot(row.node_index)) {
          AddDiagnosticAtPath(*session_, *request_, ImportSeverity::kError,
            std::string(import_codes::kScriptSidecarInstanceRootInvalid),
            "The row's node is not an instance root of the target scene",
            location / "node_index");
          valid = false;
          continue;
        }
        for (size_t j = 0; j < row.params.size(); ++j) {
          auto param = row.params.at(j);
          const auto value_location = location / "params" / j / "value";
          if (param.record.type == script::ScriptParamType::kAsset) {
            const auto asset = ResolveAssetParam(
              param, param.key, data::AssetType::kUnknown, value_location);
            if (!asset) {
              valid = false;
              continue;
            }
            param.record.value.as_asset = *asset;
          } else if (param.record.type == script::ScriptParamType::kNode
            && !CheckNodeValue(param, param.key, value_location)) {
            valid = false;
            continue;
          }
          changes.push_back({
            .node_index = row.node_index,
            .node_path = row.node_path,
            .slot_id = row.slot_id,
            .param = std::move(param),
            .json_pointer = (location / "params" / j).to_string(),
          });
        }
      }
      if (!valid) {
        return std::nullopt;
      }
      if (!CheckInstanceChanges(changes)) {
        return std::nullopt;
      }
      std::ranges::sort(changes, {}, [](const InstanceParamChange& change) {
        return std::tie(change.node_index, change.node_path, change.slot_id,
          change.param.key);
      });
      return changes;
    }

    //! Composes each change with the cooked instanced scenes: a change naming
    //! no node, or a slot the node does not have, is a warning and left out;
    //! one that changes the kind of the slot's value, or that the slot's
    //! script does not declare as it sets it, is an error. A change whose
    //! slot's script is not mounted is a warning and kept.
    /*!
     @return Whether no change is an error.
    */
    auto CheckInstanceChanges(std::vector<InstanceParamChange>& changes) -> bool
    {
      const auto resolve = [this](const data::AssetKey& key)
        -> std::shared_ptr<const data::SceneAsset> {
        const auto found = instanced_->find(key);
        return found != instanced_->end() ? found->second : nullptr;
      };
      const auto ancestors = std::array { scene_->GetAssetKey() };
      auto valid = true;
      auto unresolved = std::vector<const InstanceParamChange*> {};
      for (const auto& change : changes) {
        const auto location = change.json_pointer.empty()
          ? JsonPointer {} / "instance_bindings"
          : JsonPointer { change.json_pointer };
        // Each change is composed on its own, so that what composition finds
        // is this change's, and the slot it applies to is known.
        auto value = ParamValue(change.param);
        // Parsed and cooked parameters both carry a value type.
        DCHECK_F(value.has_value());
        const auto* instance
          = scene_->FindComponent<world::SceneInstanceRecord>(
            change.node_index);
        const auto scene_change = data::SceneChange {
          .path = change.node_path,
          .change = data::ParameterChange {
            .slot_id = change.slot_id,
            .key = change.param.key,
            .value = std::move(value).value_or(data::ScriptParam {}),
          },
        };
        const auto composed = data::ComposeInstance(
          {
            .source = instance->scene_key,
            .instance_root = change.node_index,
            .changes = std::span(&scene_change, 1U),
            .ancestors = ancestors,
          },
          resolve);
        const auto changes_kind = std::ranges::contains(composed.issues,
          data::CompositionIssueCode::kChangeInvalid,
          &data::CompositionIssue::code);
        if (changes_kind) {
          AddDiagnosticAtPath(*session_, *request_, ImportSeverity::kError,
            std::string(import_codes::kScriptSidecarInstanceOverrideInvalid),
            "The value has another kind than the slot's value of the "
            "parameter",
            location);
          valid = false;
          continue;
        }
        if (!composed.unresolved.empty()) {
          AddDiagnosticAtPath(*session_, *request_, ImportSeverity::kWarning,
            std::string(import_codes::kScriptSidecarInstanceOverrideUnresolved),
            "The change names no node of the instanced scene, or a slot that "
            "node does not have; it has no effect until it resolves",
            location);
          unresolved.push_back(&change);
          continue;
        }
        const auto* slot = AppliedSlot(composed, change);
        DCHECK_NOTNULL_F(slot);
        const auto declared = declarations_->find(slot->script);
        if (declared == declarations_->end()) {
          AddDiagnosticAtPath(*session_, *request_, ImportSeverity::kWarning,
            std::string(import_codes::kScriptSidecarInstanceScriptUnavailable),
            "The script of the slot the change reaches is not in the mounted "
            "cooked content; the change is checked when the scene is "
            "instantiated",
            location);
          continue;
        }
        if (!CheckInstanceDeclaration(change.param, declared->second,
              data::to_string(slot->script), location)) {
          valid = false;
        }
      }
      std::erase_if(changes, [&](const InstanceParamChange& change) -> bool {
        return std::ranges::contains(unresolved, &change);
      });
      return valid;
    }

    //! The value of a parsed parameter, as composition reads it: a string
    //! value's text is kept in a table of its own, and a node value's path
    //! does not take part.
    static auto ParamValue(const SidecarParam& param)
      -> std::optional<data::ScriptParam>
    {
      auto strings = data::DescriptorStringsBuilder {};
      auto record = param.record;
      if (record.type == script::ScriptParamType::kString) {
        record.value.as_string_offset = strings.Add(param.string_value);
      }
      return data::ToScriptParam(record, strings.Build());
    }

    //! The declaration of `param` among the declarations of `script_name`, or
    //! null after reporting an undeclared key or a value of another type at
    //! `param_location`.
    auto FindDeclaration(const SidecarParam& param,
      std::span<const SidecarParam> declared, const std::string& script_name,
      const JsonPointer& param_location) -> const SidecarParam*
    {
      const auto& key = param.key;
      const auto declaration
        = std::ranges::find(declared, key, &SidecarParam::key);
      if (declaration == declared.end()) {
        AddDiagnosticAtPath(*session_, *request_, ImportSeverity::kError,
          std::string(import_codes::kScriptSidecarParamUndeclared),
          "Script '" + script_name + "' declares no parameter '" + key + "'",
          param_location / "key");
        return nullptr;
      }
      if (declaration->record.type != param.record.type) {
        AddDiagnosticAtPath(*session_, *request_, ImportSeverity::kError,
          std::string(import_codes::kScriptSidecarParamTypeMismatch),
          "Parameter '" + key + "' is declared "
            + std::string(script::to_string(declaration->record.type))
            + " but the override is "
            + std::string(script::to_string(param.record.type)),
          param_location / "type");
        return nullptr;
      }
      return &*declaration;
    }

    //! Checks a change of an instanced slot's parameter against the
    //! declarations of the slot's script, as an override of the slot itself
    //! is checked; reports every violation at `param_location`.
    auto CheckInstanceDeclaration(const SidecarParam& param,
      std::span<const SidecarParam> declared, const std::string& script_name,
      const JsonPointer& param_location) -> bool
    {
      const auto* declaration
        = FindDeclaration(param, declared, script_name, param_location);
      if (declaration == nullptr) {
        return false;
      }
      const auto declared_type = declaration->record.value.as_asset.asset_type;
      const auto changed_type = param.record.value.as_asset.asset_type;
      const auto other_asset_type
        = param.record.type == script::ScriptParamType::kAsset
        && declared_type != data::AssetType::kUnknown
        && changed_type != declared_type;
      if (other_asset_type) {
        AddDiagnosticAtPath(*session_, *request_, ImportSeverity::kError,
          std::string(import_codes::kScriptSidecarParamAssetKindMismatch),
          "Parameter '" + param.key + "' expects a "
            + std::string(data::to_string(declared_type))
            + " asset but the change's asset is a "
            + std::string(data::to_string(changed_type)),
          param_location / "value");
        return false;
      }
      return true;
    }

    //! The slot a change composed on its own applied to, or null when it did
    //! not apply.
    static auto AppliedSlot(const data::ComposedScene& composed,
      const InstanceParamChange& change) -> const data::ComposedSlot*
    {
      for (const auto& occurrence : composed.occurrences) {
        for (const auto& slot : occurrence.slots) {
          const auto applied = slot.slot_id == change.slot_id
            && std::ranges::any_of(slot.parameters,
              [&change](const data::ComposedParameter& parameter) -> bool {
                return parameter.key == change.param.key
                  && parameter.supplier == data::kRequester;
              });
          if (applied) {
            return &slot;
          }
        }
      }
      return nullptr;
    }

    //! Checks each override of `row` against the script's declarations and
    //! resolves asset values; reports every violation at its JSON path.
    auto ValidateOverrides(const SidecarBindingRow& row,
      const JsonPointer& location, std::span<const SidecarParam> declared)
      -> std::optional<std::vector<SidecarParam>>
    {
      auto records = std::vector<SidecarParam> {};
      auto valid = true;
      for (size_t j = 0; j < row.params.size(); ++j) {
        const auto& param = row.params.at(j);
        const auto param_location = location / "params" / j;
        const auto& key = param.key;
        const auto* declaration = FindDeclaration(
          param, declared, row.script_virtual_path, param_location);
        if (declaration == nullptr) {
          valid = false;
          continue;
        }

        auto resolved = param;
        auto& record = resolved.record;
        if (record.type == script::ScriptParamType::kAsset) {
          const auto asset = ResolveAssetParam(param, key,
            declaration->record.value.as_asset.asset_type,
            param_location / "value");
          if (!asset) {
            valid = false;
            continue;
          }
          record.value.as_asset = *asset;
        } else if (record.type == script::ScriptParamType::kNode
          && !CheckNodeValue(resolved, key, param_location / "value")) {
          valid = false;
          continue;
        }
        records.push_back(std::move(resolved));
      }
      if (!valid) {
        return std::nullopt;
      }
      return records;
    }

    auto ResolveAssetParam(const SidecarParam& param, const std::string& key,
      const data::AssetType declared_type, const JsonPointer& value_location)
      -> std::optional<script::ScriptAssetParamValue>
    {
      const auto reference
        = internal::ResolveJobReference(*request_, param.asset_virtual_path);
      if (!reference.has_value()) {
        session_->AddDiagnostic(
          internal::RefuseReference(*request_, param.asset_virtual_path,
            reference.error(), value_location.to_string()));
        return std::nullopt;
      }
      const auto expected = declared_type == data::AssetType::kUnknown
        ? std::nullopt
        : std::optional { declared_type };
      const auto found = internal::LookupReference(
        cooked_contexts_, *request_, *reference, expected);
      if (!found.has_value()) {
        ReportRefusedReference(*reference, found.error(),
          {
            .subject = "The asset of parameter '" + key + "'",
            .missing = import_codes::kScriptSidecarParamAssetUnresolved,
            .type_mismatch = import_codes::kScriptSidecarParamAssetKindMismatch,
          },
          value_location);
        return std::nullopt;
      }
      return script::ScriptAssetParamValue {
        .key = found->key,
        .asset_type = found->type,
      };
    }

    //! What a refused reference names, and the codes its refusals take
    //! besides `import.reference_unmounted`.
    struct RefusedReference final {
      //! How the message names the reference, such as `The script`.
      std::string subject;
      std::string_view missing;
      std::string_view type_mismatch;
    };

    //! Reports a reference `LookupReference` refused, with what it looked
    //! for.
    auto ReportRefusedReference(const internal::ResolvedReference& reference,
      const internal::AssetReferenceRefused& refused,
      const RefusedReference& report, const JsonPointer& location) const -> void
    {
      switch (refused.reason) {
      case internal::AssetReferenceRefusal::kUnmounted:
        session_->AddDiagnostic(internal::RefuseUnmountedReference(
          *request_, reference, location.to_string()));
        return;
      case internal::AssetReferenceRefusal::kMissing:
        AddDiagnosticAtPath(*session_, *request_, ImportSeverity::kError,
          std::string(report.missing),
          report.subject
            + " was not found: " + internal::DescribeLookup(reference, refused),
          location);
        return;
      case internal::AssetReferenceRefusal::kTypeMismatch:
        AddDiagnosticAtPath(*session_, *request_, ImportSeverity::kError,
          std::string(report.type_mismatch),
          report.subject + " is a "
            + std::string(data::to_string(
              refused.found_type.value_or(data::AssetType::kUnknown)))
            + " asset of the wrong type: "
            + internal::DescribeLookup(reference, refused),
          location);
        return;
      }
    }

    observer_ptr<ImportSession> session_;
    observer_ptr<const ImportRequest> request_;
    observer_ptr<const SidecarDocument> parsed_;
    uint32_t node_count_ = 0;
    std::span<const internal::MountedCookedSource> cooked_contexts_;
    observer_ptr<const data::SceneAsset> scene_;
    observer_ptr<const ScriptDeclarationMap> declarations_;
    observer_ptr<const SceneMap> instanced_;
  };

  auto BuildPatchedSceneDescriptor(ImportSession& session,
    const ImportRequest& request, const SidecarResolvedSceneState& scene_state,
    const data::SceneAsset& scene, const SceneScriptData& scripts)
    -> std::optional<std::vector<std::byte>>
  {
    auto patched_scene_bytes = std::vector<std::byte> {};
    auto patch_error = std::string {};
    auto patcher = SceneDescriptorPatcher(scene_state, scene, scripts);
    if (!patcher.Patch(patched_scene_bytes, patch_error)) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        std::string(import_codes::kScriptSidecarScenePatchFailed),
        std::move(patch_error));
      return std::nullopt;
    }

    return patched_scene_bytes;
  }

  //! Asset keys the scene's previous script slots contributed, other than
  //! keys its own components also use.
  auto CollectScriptOnlyParamKeys(const data::SceneAsset& scene)
    -> std::set<data::AssetKey>
  {
    auto component_keys = std::set<data::AssetKey> {};
    for (const auto& renderable :
      scene.GetComponents<world::RenderableRecord>()) {
      component_keys.insert(renderable.geometry_key);
    }
    for (const auto& record : scene.GetComponents<world::OverrideRecord>()) {
      const auto* property
        = data::schema::FindOverrideProperty(record.override_id);
      // The scene validated every override against the schema when it loaded.
      DCHECK_NOTNULL_F(property);
      if (property->type == data::schema::ValueType::kAsset) {
        component_keys.insert(record.value.as_asset);
      }
    }
    for (const auto& instance :
      scene.GetComponents<world::SceneInstanceRecord>()) {
      component_keys.insert(instance.scene_key);
    }
    for (const auto& context :
      scene.GetComponents<world::StartingInputContextRecord>()) {
      component_keys.insert(context.context_key);
    }

    auto keys = std::set<data::AssetKey> {};
    for (const auto& change : CollectInstanceChanges(scene)) {
      if (change.param.record.type == script::ScriptParamType::kAsset
        && !component_keys.contains(change.param.record.value.as_asset.key)) {
        keys.insert(change.param.record.value.as_asset.key);
      }
    }
    for (const auto& slots :
      BuildBindingsByNodeFromComponents(scene) | std::views::values) {
      for (const auto& slot : slots) {
        for (const auto& param : slot.params) {
          if (param.record.type == script::ScriptParamType::kAsset
            && !component_keys.contains(param.record.value.as_asset.key)) {
            keys.insert(param.record.value.as_asset.key);
          }
        }
      }
    }
    return keys;
  }

  auto EmitPatchedScene(ImportSession& session, const ImportRequest& request,
    const SidecarResolvedSceneState& scene_state, const data::SceneAsset& scene,
    std::span<const std::byte> patched_scene_bytes,
    const SceneScriptData& scripts) -> co::Co<bool>
  {
    using data::AssetType;

    try {
      const auto previous_param_keys = CollectScriptOnlyParamKeys(scene);
      // Script keys are derived again from the slots below.
      AssetReferenceBuilder references(scene_state.references,
        [&previous_param_keys](const data::KeyReference& reference) -> bool {
          return reference.kind != data::KeyReferenceKind::kAsset
            || (reference.expected_type != data::AssetType::kScript
              && !previous_param_keys.contains(reference.key));
        });
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
      co_await session.AssetEmitter().EmitSync(scene_state.scene_key,
        AssetType::kScene, scene_state.scene_virtual_path,
        scene_state.scene_descriptor_relpath, patched_scene_bytes,
        std::move(references).Build());
    } catch (const std::exception& ex) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        std::string(import_codes::kScriptSidecarSceneEmitFailed), ex.what());
      co_return false;
    }

    co_return true;
  }

  struct ScriptingSidecarIoHandles final {
    cooked::CookerStorage* storage = nullptr;
    LooseCookedIndexRegistry* index_registry = nullptr;
  };

  auto ValidateScriptingSidecarRequest(
    ImportSession& session, const ImportRequest& request) -> bool
  {
    const auto& scripting = request.options.scripting;
    if (scripting.import_kind != ScriptingImportKind::kScriptingSidecar) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        std::string(import_codes::kScriptRequestInvalidImportKind),
        "Scripting sidecar import requires "
        "options.scripting.import_kind=kScriptingSidecar");
      return false;
    }

    if (scripting.target_scene_virtual_path.empty()) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        std::string(import_codes::kScriptRequestTargetSceneVirtualPathMissing),
        "Scripting sidecar import requires target_scene_virtual_path");
      return false;
    }

    return true;
  }

  auto ResolveScriptingSidecarIoHandles(ImportSession& session,
    const ImportRequest& request,
    observer_ptr<LooseCookedIndexRegistry> index_registry)
    -> std::optional<ScriptingSidecarIoHandles>
  {
    auto handles = ScriptingSidecarIoHandles {
      .storage = session.Storage().get(),
      .index_registry = index_registry.get(),
    };
    if (handles.storage == nullptr || handles.index_registry == nullptr) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        std::string(import_codes::kScriptSidecarIoUnavailable),
        "Scripting sidecar emission requires a storage and an index "
        "registry");
      return std::nullopt;
    }
    return handles;
  }

  //! Patches the target scene with the sidecar's scripts and emits it. The
  //! mounted cooked roots are released before the emission: they keep files
  //! of the cooked root open, the target scene's descriptor among them.
  auto ApplyScriptingSidecar(ImportSession& session,
    const ImportRequest& request,
    std::vector<internal::MountedCookedSource>& cooked_contexts,
    const SidecarDocument& parsed,
    const SidecarResolvedSceneState& resolved_scene_state) -> co::Co<bool>
  {
    auto declarations
      = LoadScriptDeclarations(session, request, cooked_contexts, parsed);
    if (!declarations) {
      co_return false;
    }

    const data::SceneAsset scene(resolved_scene_state.scene_key,
      std::span<const std::byte>(resolved_scene_state.source_scene_descriptor));
    const auto instanced = ReadInstancedScenes(cooked_contexts, scene);
    AddInstancedScriptDeclarations(cooked_contexts, instanced, *declarations);
    const auto scripts = SceneScriptBuilder(session, request, parsed,
      resolved_scene_state.node_count, cooked_contexts, scene, *declarations,
      instanced)
                           .Build();
    if (!scripts) {
      co_return false;
    }
    const auto patched = BuildPatchedSceneDescriptor(
      session, request, resolved_scene_state, scene, *scripts);
    if (!patched) {
      co_return false;
    }
    cooked_contexts.clear();
    if (!co_await EmitPatchedScene(
          session, request, resolved_scene_state, scene, *patched, *scripts)) {
      co_return false;
    }

    co_return true;
  }

} // namespace
ScriptingSidecarImportPipeline::ScriptingSidecarImportPipeline()
  : ScriptingSidecarImportPipeline(Config {})
{
}

ScriptingSidecarImportPipeline::ScriptingSidecarImportPipeline(Config config)
  : config_(config)
  , input_channel_(config.queue_capacity)
  , output_channel_(config.queue_capacity)
{
}

ScriptingSidecarImportPipeline::~ScriptingSidecarImportPipeline()
{
  if (started_) {
    DLOG_IF_F(WARNING, HasPending(),
      "ScriptingSidecarImportPipeline destroyed with {} pending items",
      PendingCount());
  }
  input_channel_.Close();
  output_channel_.Close();
}

auto ScriptingSidecarImportPipeline::Start(co::Nursery& nursery) -> void
{
  DCHECK_F(
    !started_, "ScriptingSidecarImportPipeline::Start() called more than once");
  started_ = true;

  const auto worker_count = std::max(1U, config_.worker_count);
  for (uint32_t i = 0; i < worker_count; ++i) {
    nursery.Start(&ScriptingSidecarImportPipeline::Worker, this);
  }
}

auto ScriptingSidecarImportPipeline::Submit(WorkItem item) -> co::Co<>
{
  pending_.fetch_add(1, std::memory_order_acq_rel);
  submitted_.fetch_add(1, std::memory_order_acq_rel);
  co_await input_channel_.Send(std::move(item));
}

auto ScriptingSidecarImportPipeline::TrySubmit(WorkItem item) -> bool
{
  if (input_channel_.Closed() || input_channel_.Full()) {
    return false;
  }

  const auto ok = input_channel_.TrySend(std::move(item));
  if (ok) {
    pending_.fetch_add(1, std::memory_order_acq_rel);
    submitted_.fetch_add(1, std::memory_order_acq_rel);
  }
  return ok;
}

auto ScriptingSidecarImportPipeline::Collect() -> co::Co<WorkResult>
{
  auto maybe_result = co_await output_channel_.Receive();
  if (!maybe_result.has_value()) {
    co_return WorkResult {
      .source_id = {},
      .diagnostics = {},
      .telemetry = {},
      .success = false,
    };
  }

  pending_.fetch_sub(1, std::memory_order_acq_rel);
  if (maybe_result->success) {
    completed_.fetch_add(1, std::memory_order_acq_rel);
  } else {
    failed_.fetch_add(1, std::memory_order_acq_rel);
  }
  co_return std::move(*maybe_result);
}

auto ScriptingSidecarImportPipeline::Close() -> void { input_channel_.Close(); }

auto ScriptingSidecarImportPipeline::HasPending() const noexcept -> bool
{
  return pending_.load(std::memory_order_acquire) > 0;
}

auto ScriptingSidecarImportPipeline::PendingCount() const noexcept -> size_t
{
  return pending_.load(std::memory_order_acquire);
}

auto ScriptingSidecarImportPipeline::GetProgress() const noexcept
  -> PipelineProgress
{
  const auto submitted = submitted_.load(std::memory_order_acquire);
  const auto completed = completed_.load(std::memory_order_acquire);
  const auto failed = failed_.load(std::memory_order_acquire);
  return PipelineProgress {
    .submitted = submitted,
    .completed = completed,
    .failed = failed,
    .in_flight = submitted - completed - failed,
    .throughput = 0.0F,
  };
}

auto ScriptingSidecarImportPipeline::Worker() -> co::Co<>
{
  const auto MakeDuration
    = [](const std::chrono::steady_clock::time_point start,
        const std::chrono::steady_clock::time_point end)
    -> std::chrono::microseconds {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  };

  while (true) {
    auto maybe_item = co_await input_channel_.Receive();
    if (!maybe_item.has_value()) {
      break;
    }

    auto item = std::move(*maybe_item);
    if (item.stop_token.stop_possible() && item.stop_token.stop_requested()) {
      co_await ReportCancelled(std::move(item));
      continue;
    }

    if (item.on_started) {
      item.on_started();
    }

    const auto process_start = std::chrono::steady_clock::now();
    auto result = WorkResult {
      .source_id = item.source_id,
      .diagnostics = {},
      .telemetry = {},
      .success = false,
    };

    try {
      result.success = co_await Process(item);
    } catch (const std::exception& ex) {
      if (item.session != nullptr) {
        const auto& request = item.session->Request();
        AddDiagnostic(*item.session, request, ImportSeverity::kError,
          std::string(import_codes::kScriptSidecarPipelineException),
          std::string { "Unhandled scripting sidecar pipeline exception: " }
            + ex.what());
      }
      result.success = false;
    }

    result.telemetry.cook_duration
      = MakeDuration(process_start, std::chrono::steady_clock::now());

    if (item.on_finished) {
      item.on_finished();
    }

    co_await output_channel_.Send(std::move(result));
  }

  co_return;
}

auto ScriptingSidecarImportPipeline::Process(WorkItem& item) -> co::Co<bool>
{
  auto* const session = item.session.get();
  if (session == nullptr) {
    co_return false;
  }

  const auto& req = session->Request();
  if (!ValidateScriptingSidecarRequest(*session, req)) {
    co_return false;
  }

  const auto source_bytes = item.source_bytes.Bytes();
  auto source_text = std::string(source_bytes.size(), '\0');
  if (!source_bytes.empty()) {
    std::memcpy(source_text.data(), source_bytes.data(), source_bytes.size());
  }
  auto diagnostics = std::vector<ImportDiagnostic> {};
  const auto parsed = PrepareScriptingSidecarDocument(
    source_text, req.source_path, diagnostics);
  for (auto& diagnostic : diagnostics) {
    session->AddDiagnostic(std::move(diagnostic));
  }
  if (!parsed.has_value()) {
    co_return false;
  }

  const auto io_handles
    = ResolveScriptingSidecarIoHandles(*session, req, item.index_registry);
  if (!io_handles.has_value()) {
    co_return false;
  }

  auto cooked_contexts = internal::LoadMountedCookedSources(
    *session, req, import_codes::kScriptSidecarIndexLoadFailed);
  if (session->HasErrors()) {
    co_return false;
  }

  auto descriptor_edit = co_await item.index_registry->LockDescriptor(
    session->CookedRoot(), req.options.scripting.target_scene_virtual_path);

  const auto resolved_scene_state = detail::ResolveTargetSceneState(*session,
    req, cooked_contexts, req.options.scripting.target_scene_virtual_path,
    kScriptSidecarResolverDiagnostics);
  if (!resolved_scene_state.has_value()) {
    co_return false;
  }

  co_return co_await ApplyScriptingSidecar(
    *session, req, cooked_contexts, *parsed, *resolved_scene_state);
}

auto ScriptingSidecarImportPipeline::ReportCancelled(WorkItem item) -> co::Co<>
{
  if (item.on_finished) {
    item.on_finished();
  }
  co_await output_channel_.Send(WorkResult {
    .source_id = std::move(item.source_id),
    .diagnostics = {},
    .telemetry = {},
    .success = false,
  });
}

} // namespace oxygen::content::import
