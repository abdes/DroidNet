//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/VirtualPathResolver.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/IAsyncFileWriter.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedIndexRegistry.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/ScriptingSidecarImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Utils/ContentHashUtils.h>
#include <Oxygen/Cooker/Import/Internal/Utils/ImportSettingsUtils.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>

namespace oxygen::content::import {
namespace {

  namespace script = data::pak::scripting;
  namespace world = data::pak::world;
  namespace lc = oxygen::content::lc;

  constexpr auto kScriptsTableFileName = std::string_view { "scripts.table" };
  constexpr auto kScriptsDataFileName = std::string_view { "scripts.data" };

  auto JoinRelativePath(
    const std::string_view base, const std::string_view child) -> std::string
  {
    if (base.empty()) {
      return std::string { child };
    }
    if (child.empty()) {
      return std::string { base };
    }
    return std::string { base } + "/" + std::string { child };
  }

  auto BuildScriptsTableRelPath(const ImportRequest& request) -> std::string
  {
    return JoinRelativePath(
      request.loose_cooked_layout.resources_dir, kScriptsTableFileName);
  }

  auto BuildScriptsDataRelPath(const ImportRequest& request) -> std::string
  {
    return JoinRelativePath(
      request.loose_cooked_layout.resources_dir, kScriptsDataFileName);
  }

  template <size_t N>
  auto CopyNullTerminated(const std::string_view src, std::span<char, N> dst)
    -> bool
  {
    if (src.size() >= dst.size()) {
      return false;
    }
    std::ranges::fill(dst, '\0');
    std::ranges::copy(src, dst.begin());
    return true;
  }

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

  auto AddDiagnosticAtPath(ImportSession& session, const ImportRequest& request,
    const ImportSeverity severity, std::string code, std::string message,
    std::string object_path) -> void
  {
    session.AddDiagnostic({
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = request.source_path.string(),
      .object_path = std::move(object_path),
    });
  }

  struct SidecarBindingRow final {
    uint32_t node_index = 0;
    std::string slot_id;
    std::string script_virtual_path;
    int32_t execution_order = 0;
    std::vector<data::pak::scripting::ScriptParamRecord> params;
  };

  struct SidecarDocument final {
    std::vector<SidecarBindingRow> rows;
  };

  struct SlotWithParams final {
    std::string slot_id;
    data::pak::scripting::ScriptSlotRecord slot = {};
    std::vector<data::pak::scripting::ScriptParamRecord> params;
  };

  struct ExistingScriptTables final {
    std::vector<data::pak::scripting::ScriptSlotRecord> slots;
    std::vector<data::pak::scripting::ScriptParamRecord> params;
  };

  [[nodiscard]] auto MakeBindingIdentity(
    const uint32_t node_index, const std::string_view slot_id) -> std::string
  {
    return std::to_string(node_index) + "|" + std::string { slot_id };
  }

  auto ParseScriptParamRecord(const nlohmann::json& param,
    data::pak::scripting::ScriptParamRecord& out, std::string& error) -> bool
  {
    using data::pak::scripting::ScriptParamRecord;
    using data::pak::scripting::ScriptParamType;

    if (!param.is_object()) {
      error = "Param record must be an object";
      return false;
    }
    if (!param.contains("key") || !param["key"].is_string()) {
      error = "Param record requires string field 'key'";
      return false;
    }
    if (!param.contains("type") || !param["type"].is_string()) {
      error = "Param record requires string field 'type'";
      return false;
    }
    if (!param.contains("value")) {
      error = "Param record requires field 'value'";
      return false;
    }

    const auto key = param["key"].get<std::string>();
    if (!CopyNullTerminated(key, std::span { out.key })) {
      error = "Param key exceeds ScriptParamRecord::key capacity";
      return false;
    }

    const auto type = param["type"].get<std::string>();
    const auto& value = param["value"];

    if (type == "bool") {
      if (!value.is_boolean()) {
        error = "bool param requires boolean value";
        return false;
      }
      out.type = ScriptParamType::kBool;
      out.value.as_bool = value.get<bool>();
      return true;
    }
    if (type == "int32") {
      if (!value.is_number_integer()) {
        error = "int32 param requires integer value";
        return false;
      }
      const auto v = value.get<int64_t>();
      if (v < (std::numeric_limits<int32_t>::min)()
        || v > (std::numeric_limits<int32_t>::max)()) {
        error = "int32 param value is out of range";
        return false;
      }
      out.type = ScriptParamType::kInt32;
      out.value.as_int32 = static_cast<int32_t>(v);
      return true;
    }
    if (type == "float") {
      if (!value.is_number()) {
        error = "float param requires numeric value";
        return false;
      }
      out.type = ScriptParamType::kFloat;
      out.value.as_float = value.get<float>();
      return true;
    }
    if (type == "string") {
      if (!value.is_string()) {
        error = "string param requires string value";
        return false;
      }
      const auto text = value.get<std::string>();
      if (!CopyNullTerminated(text, std::span { out.value.as_string })) {
        error = "string param exceeds ScriptParamRecord::value.as_string "
                "capacity";
        return false;
      }
      out.type = ScriptParamType::kString;
      return true;
    }

    const auto ParseVectorParam
      = [&](const size_t expected_size, const ScriptParamType param_type,
          const std::string_view type_name) -> bool {
      if (!value.is_array()) {
        error = std::string(type_name) + " param requires array value";
        return false;
      }
      if (value.size() != expected_size) {
        error = std::string(type_name) + " param requires array size "
          + std::to_string(expected_size);
        return false;
      }
      for (size_t i = 0; i < expected_size; ++i) {
        if (!value[i].is_number()) {
          error = std::string(type_name)
            + " param array must contain numeric elements";
          return false;
        }
        out.value.as_vec[i] = value[i].get<float>();
      }
      out.type = param_type;
      return true;
    };

    if (type == "vec2") {
      return ParseVectorParam(2, ScriptParamType::kVec2, "vec2");
    }
    if (type == "vec3") {
      return ParseVectorParam(3, ScriptParamType::kVec3, "vec3");
    }
    if (type == "vec4") {
      return ParseVectorParam(4, ScriptParamType::kVec4, "vec4");
    }

    error = "Unsupported param type '" + type + "'";
    return false;
  }

  auto ParseSidecarDocument(std::span<const std::byte> bytes,
    ImportSession& session, const ImportRequest& request)
    -> std::optional<SidecarDocument>
  {
    using json = nlohmann::json;

    std::string source_text;
    source_text.resize(bytes.size());
    if (!bytes.empty()) {
      std::memcpy(source_text.data(), bytes.data(), bytes.size());
    }

    json doc;
    try {
      doc = json::parse(source_text);
    } catch (const std::exception& ex) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "script.sidecar.parse_failed",
        "Failed to parse sidecar document as JSON: " + std::string(ex.what()));
      return std::nullopt;
    }

    if (!doc.is_object()) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "script.sidecar.payload_invalid",
        "Sidecar document root must be a JSON object");
      return std::nullopt;
    }
    if (!doc.contains("bindings") || !doc["bindings"].is_array()) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "script.sidecar.payload_invalid",
        "Sidecar document requires array field 'bindings'");
      return std::nullopt;
    }

    SidecarDocument out {};
    for (size_t i = 0; i < doc["bindings"].size(); ++i) {
      const auto object_path = "bindings[" + std::to_string(i) + "]";
      const auto& binding = doc["bindings"][i];
      if (!binding.is_object()) {
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          "script.sidecar.payload_invalid", "Binding row must be an object",
          object_path);
        continue;
      }
      if (!binding.contains("node_index")
        || !binding["node_index"].is_number_unsigned()) {
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          "script.sidecar.payload_invalid",
          "Binding row requires unsigned field 'node_index'", object_path);
        continue;
      }
      if (!binding.contains("slot_id") || !binding["slot_id"].is_string()) {
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          "script.sidecar.payload_invalid",
          "Binding row requires string field 'slot_id'", object_path);
        continue;
      }
      if (!binding.contains("script_virtual_path")
        || !binding["script_virtual_path"].is_string()) {
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          "script.sidecar.payload_invalid",
          "Binding row requires string field 'script_virtual_path'",
          object_path);
        continue;
      }

      SidecarBindingRow row {};
      row.node_index = binding["node_index"].get<uint32_t>();
      row.slot_id = binding["slot_id"].get<std::string>();
      row.script_virtual_path
        = binding["script_virtual_path"].get<std::string>();
      if (binding.contains("execution_order")) {
        if (!binding["execution_order"].is_number_integer()) {
          AddDiagnosticAtPath(session, request, ImportSeverity::kError,
            "script.sidecar.payload_invalid",
            "Binding row field 'execution_order' must be integer", object_path);
          continue;
        }
        const auto order = binding["execution_order"].get<int64_t>();
        if (order < (std::numeric_limits<int32_t>::min)()
          || order > (std::numeric_limits<int32_t>::max)()) {
          AddDiagnosticAtPath(session, request, ImportSeverity::kError,
            "script.sidecar.payload_invalid",
            "Binding row field 'execution_order' is out of int32 range",
            object_path);
          continue;
        }
        row.execution_order = static_cast<int32_t>(order);
      }

      if (binding.contains("params")) {
        if (!binding["params"].is_array()) {
          AddDiagnosticAtPath(session, request, ImportSeverity::kError,
            "script.sidecar.payload_invalid",
            "Binding row field 'params' must be an array", object_path);
          continue;
        }
        for (size_t j = 0; j < binding["params"].size(); ++j) {
          auto parsed_param = data::pak::scripting::ScriptParamRecord {};
          auto error = std::string {};
          const auto param_path
            = object_path + ".params[" + std::to_string(j) + "]";
          if (!ParseScriptParamRecord(
                binding["params"][j], parsed_param, error)) {
            AddDiagnosticAtPath(session, request, ImportSeverity::kError,
              "script.sidecar.param_invalid", std::move(error), param_path);
            continue;
          }
          row.params.push_back(parsed_param);
        }
      }

      out.rows.push_back(std::move(row));
    }

    if (session.HasErrors()) {
      return std::nullopt;
    }

    std::ranges::sort(
      out.rows, [](const SidecarBindingRow& lhs, const SidecarBindingRow& rhs) {
        if (lhs.node_index != rhs.node_index) {
          return lhs.node_index < rhs.node_index;
        }
        return lhs.slot_id < rhs.slot_id;
      });

    auto seen = std::unordered_map<std::string, size_t> {};
    for (size_t i = 0; i < out.rows.size(); ++i) {
      const auto identity
        = MakeBindingIdentity(out.rows[i].node_index, out.rows[i].slot_id);
      if (seen.contains(identity)) {
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          "script.sidecar.duplicate_slot_conflict",
          "Duplicate sidecar binding identity (" + identity + ")",
          "bindings[" + std::to_string(i) + "]");
      } else {
        seen.emplace(identity, i);
      }
    }

    if (session.HasErrors()) {
      return std::nullopt;
    }

    return out;
  }

  auto ReadTypedVectorFromBytes(std::span<const std::byte> bytes,
    std::vector<data::pak::scripting::ScriptSlotRecord>& records) -> bool
  {
    using data::pak::scripting::ScriptSlotRecord;
    if ((bytes.size() % sizeof(ScriptSlotRecord)) != 0U) {
      return false;
    }
    records.resize(bytes.size() / sizeof(ScriptSlotRecord));
    if (!bytes.empty()) {
      std::memcpy(records.data(), bytes.data(), bytes.size());
    }
    return true;
  }

  auto ReadTypedVectorFromBytes(std::span<const std::byte> bytes,
    std::vector<data::pak::scripting::ScriptParamRecord>& records) -> bool
  {
    using data::pak::scripting::ScriptParamRecord;
    if ((bytes.size() % sizeof(ScriptParamRecord)) != 0U) {
      return false;
    }
    records.resize(bytes.size() / sizeof(ScriptParamRecord));
    if (!bytes.empty()) {
      std::memcpy(records.data(), bytes.data(), bytes.size());
    }
    return true;
  }

  auto LoadInspectionFromRoot(const std::filesystem::path& cooked_root,
    ImportSession& session, const ImportRequest& request,
    lc::Inspection& inspection) -> bool
  {
    try {
      inspection.LoadFromRoot(cooked_root);
      return true;
    } catch (const std::exception& ex) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "script.sidecar.index_load_failed",
        "Failed loading cooked index: " + std::string(ex.what()));
      return false;
    }
  }

  auto FindAssetEntryByKey(const lc::Inspection& inspection,
    const data::AssetKey& key) -> const lc::AssetEntry*
  {
    for (const auto& entry : inspection.Assets()) {
      if (entry.key == key) {
        return &entry;
      }
    }
    return nullptr;
  }

  struct ComponentPayload final {
    uint32_t component_type = 0;
    uint32_t entry_size = 0;
    std::vector<std::byte> bytes;
  };

  auto PatchSceneDescriptorScriptingComponents(
    const std::vector<std::byte>& source_descriptor,
    std::span<const script::ScriptingComponentRecord> scripting_components,
    std::vector<std::byte>& patched_descriptor, std::string& error) -> bool
  {
    using data::ComponentType;
    using world::SceneAssetDesc;
    using world::SceneComponentTableDesc;

    if (source_descriptor.size() < sizeof(SceneAssetDesc)) {
      error = "Scene descriptor is smaller than SceneAssetDesc";
      return false;
    }

    SceneAssetDesc source_desc {};
    std::memcpy(&source_desc, source_descriptor.data(), sizeof(source_desc));

    const auto range_ok = [&](const uint64_t offset, const uint64_t size) {
      return offset <= source_descriptor.size()
        && size <= (source_descriptor.size() - offset);
    };

    const auto node_table_size = static_cast<uint64_t>(source_desc.nodes.count)
      * static_cast<uint64_t>(source_desc.nodes.entry_size);
    if (!range_ok(source_desc.nodes.offset, node_table_size)) {
      error = "Scene node table range is invalid";
      return false;
    }

    const auto scene_string_size
      = static_cast<uint64_t>(source_desc.scene_strings.size);
    if (!range_ok(source_desc.scene_strings.offset, scene_string_size)) {
      error = "Scene string table range is invalid";
      return false;
    }

    auto payload_end = static_cast<uint64_t>(sizeof(SceneAssetDesc));
    payload_end
      = (std::max)(payload_end, source_desc.nodes.offset + node_table_size);
    payload_end = (std::max)(payload_end,
      source_desc.scene_strings.offset + scene_string_size);

    auto component_payloads = std::vector<ComponentPayload> {};
    if (source_desc.component_table_count > 0U) {
      const auto directory_size
        = static_cast<uint64_t>(source_desc.component_table_count)
        * sizeof(SceneComponentTableDesc);
      if (!range_ok(
            source_desc.component_table_directory_offset, directory_size)) {
        error = "Scene component directory range is invalid";
        return false;
      }

      payload_end = (std::max)(payload_end,
        source_desc.component_table_directory_offset + directory_size);

      for (uint32_t i = 0; i < source_desc.component_table_count; ++i) {
        const auto dir_offset
          = static_cast<size_t>(source_desc.component_table_directory_offset)
          + static_cast<size_t>(i) * sizeof(SceneComponentTableDesc);
        auto entry = SceneComponentTableDesc {};
        std::memcpy(
          &entry, source_descriptor.data() + dir_offset, sizeof(entry));

        if (entry.table.count == 0U) {
          continue;
        }

        const auto table_size = static_cast<uint64_t>(entry.table.count)
          * static_cast<uint64_t>(entry.table.entry_size);
        if (!range_ok(entry.table.offset, table_size)) {
          error = "Scene component table range is invalid";
          return false;
        }
        payload_end = (std::max)(payload_end, entry.table.offset + table_size);

        const auto type = static_cast<ComponentType>(entry.component_type);
        if (type == ComponentType::kScripting) {
          continue;
        }

        auto payload = ComponentPayload {
          .component_type = entry.component_type,
          .entry_size = entry.table.entry_size,
          .bytes = {},
        };
        payload.bytes.resize(static_cast<size_t>(table_size));
        std::memcpy(payload.bytes.data(),
          source_descriptor.data() + static_cast<size_t>(entry.table.offset),
          payload.bytes.size());
        component_payloads.push_back(std::move(payload));
      }
    }

    if (!scripting_components.empty()) {
      auto payload = ComponentPayload {
        .component_type = static_cast<uint32_t>(ComponentType::kScripting),
        .entry_size = sizeof(script::ScriptingComponentRecord),
        .bytes = {},
      };
      payload.bytes.resize(
        scripting_components.size() * sizeof(script::ScriptingComponentRecord));
      std::memcpy(payload.bytes.data(), scripting_components.data(),
        payload.bytes.size());
      component_payloads.push_back(std::move(payload));
    }

    std::ranges::sort(component_payloads,
      [](const ComponentPayload& lhs, const ComponentPayload& rhs) {
        return lhs.component_type < rhs.component_type;
      });

    auto trailing_bytes = std::vector<std::byte> {};
    if (payload_end < source_descriptor.size()) {
      const auto remaining
        = source_descriptor.size() - static_cast<size_t>(payload_end);
      trailing_bytes.resize(remaining);
      std::memcpy(trailing_bytes.data(),
        source_descriptor.data() + static_cast<size_t>(payload_end), remaining);
    }

    auto out = std::vector<std::byte> {};
    out.resize(sizeof(SceneAssetDesc));

    auto desc = source_desc;
    desc.header.content_hash = 0;
    desc.nodes.offset = sizeof(SceneAssetDesc);
    const auto scene_strings_offset
      = uint64_t { desc.nodes.offset } + node_table_size;
    using SceneStringOffsetT = decltype(desc.scene_strings.offset);
    if (scene_strings_offset
      > (std::numeric_limits<SceneStringOffsetT>::max)()) {
      error = "Scene string table offset overflow";
      return false;
    }
    desc.scene_strings.offset
      = static_cast<SceneStringOffsetT>(scene_strings_offset);
    desc.component_table_directory_offset = 0;
    desc.component_table_count = 0;

    const auto append_bytes = [&](std::span<const std::byte> bytes) {
      out.insert(out.end(), bytes.begin(), bytes.end());
    };

    append_bytes(std::span<const std::byte>(
      source_descriptor.data() + static_cast<size_t>(source_desc.nodes.offset),
      static_cast<size_t>(node_table_size)));
    append_bytes(std::span<const std::byte>(source_descriptor.data()
        + static_cast<size_t>(source_desc.scene_strings.offset),
      static_cast<size_t>(scene_string_size)));

    if (!component_payloads.empty()) {
      desc.component_table_directory_offset = out.size();
      desc.component_table_count
        = static_cast<uint32_t>(component_payloads.size());

      const auto directory_size
        = component_payloads.size() * sizeof(SceneComponentTableDesc);
      const auto directory_offset = out.size();
      out.resize(out.size() + directory_size);

      auto directory = std::vector<SceneComponentTableDesc> {};
      directory.resize(component_payloads.size());
      for (size_t i = 0; i < component_payloads.size(); ++i) {
        auto& entry = directory[i];
        entry.component_type = component_payloads[i].component_type;
        entry.table.entry_size = component_payloads[i].entry_size;
        entry.table.count
          = static_cast<uint32_t>(component_payloads[i].entry_size == 0U
              ? 0U
              : component_payloads[i].bytes.size()
                / component_payloads[i].entry_size);
        entry.table.offset = out.size();
        append_bytes(component_payloads[i].bytes);
      }

      std::memcpy(
        out.data() + directory_offset, directory.data(), directory_size);
    }

    append_bytes(trailing_bytes);

    std::memcpy(out.data(), &desc, sizeof(desc));
    patched_descriptor = std::move(out);
    return true;
  }

  struct ResolvedSceneState final {
    data::AssetKey scene_key {};
    std::string scene_virtual_path;
    std::string scene_descriptor_relpath;
    std::vector<std::byte> source_scene_descriptor;
    uint32_t node_count = 0;
    std::vector<script::ScriptingComponentRecord> existing_components;
  };

  struct CookedInspectionContext final {
    std::filesystem::path cooked_root;
    lc::Inspection inspection;
  };

  struct ScriptsTableState final {
    std::optional<std::string> scripts_table_relpath;
    std::optional<std::string> scripts_data_relpath;
    bool has_existing_script_tables = false;
    ExistingScriptTables tables;
  };

  struct MergedScriptsState final {
    std::vector<script::ScriptingComponentRecord> components;
    std::vector<script::ScriptSlotRecord> slots;
    std::vector<script::ScriptParamRecord> params;
  };

  struct SceneBindingContextOutcome final {
    std::optional<ResolvedSceneState> state;
    bool failed = false;
  };

  struct ComponentSlotPatchRef final {
    size_t component_index = 0;
    uint32_t slot_start_index = 0;
    uint32_t slot_count = 0;
  };

  auto BuildResolvedSceneStateFromDescriptor(ImportSession& session,
    const ImportRequest& request, const data::AssetKey scene_key,
    std::string scene_virtual_path, std::string scene_descriptor_relpath,
    std::vector<std::byte> descriptor_bytes)
    -> std::optional<ResolvedSceneState>
  {
    auto scene_asset = std::optional<data::SceneAsset> {};
    try {
      scene_asset.emplace(scene_key, descriptor_bytes);
    } catch (const std::exception& ex) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "script.sidecar.target_scene_invalid",
        "Target scene descriptor failed validation: " + std::string(ex.what()));
      return std::nullopt;
    }

    auto state = ResolvedSceneState {
      .scene_key = scene_key,
      .scene_virtual_path = std::move(scene_virtual_path),
      .scene_descriptor_relpath = std::move(scene_descriptor_relpath),
      .source_scene_descriptor = std::move(descriptor_bytes),
      .node_count = static_cast<uint32_t>(scene_asset->GetNodes().size()),
      .existing_components = {},
    };
    const auto existing_components_span
      = scene_asset->GetComponents<script::ScriptingComponentRecord>();
    state.existing_components.assign(
      existing_components_span.begin(), existing_components_span.end());
    return state;
  }

  auto ResolveSceneBindingContextFromInflightByVirtualPath(
    ImportSession& session, const ImportRequest& request)
    -> SceneBindingContextOutcome
  {
    const auto& target_scene_path
      = request.options.scripting.target_scene_virtual_path;
    auto matches = std::vector<const ImportRequest::InflightSceneContext*> {};
    matches.reserve(request.inflight_scene_contexts.size());
    for (const auto& candidate : request.inflight_scene_contexts) {
      if (candidate.virtual_path == target_scene_path) {
        matches.push_back(&candidate);
      }
    }

    if (matches.size() > 1U) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "script.sidecar.inflight_target_scene_ambiguous",
        "Multiple inflight scene contexts match target_scene_virtual_path");
      return SceneBindingContextOutcome {
        .state = std::nullopt,
        .failed = true,
      };
    }
    if (matches.empty()) {
      return SceneBindingContextOutcome {
        .state = std::nullopt,
        .failed = false,
      };
    }

    const auto* match = matches.front();
    if (match->descriptor_relpath.empty()) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "script.sidecar.target_scene_invalid",
        "Inflight scene context has empty descriptor_relpath");
      return SceneBindingContextOutcome {
        .state = std::nullopt,
        .failed = true,
      };
    }

    const auto state = BuildResolvedSceneStateFromDescriptor(session, request,
      match->scene_key, match->virtual_path, match->descriptor_relpath,
      match->descriptor_bytes);
    return SceneBindingContextOutcome {
      .state = state,
      .failed = !state.has_value(),
    };
  }

  auto ResolveSceneBindingContextFromInflightBySceneKey(ImportSession& session,
    const ImportRequest& request, const data::AssetKey& scene_key)
    -> SceneBindingContextOutcome
  {
    auto matches = std::vector<const ImportRequest::InflightSceneContext*> {};
    matches.reserve(request.inflight_scene_contexts.size());
    for (const auto& candidate : request.inflight_scene_contexts) {
      if (candidate.scene_key == scene_key) {
        matches.push_back(&candidate);
      }
    }

    if (matches.size() > 1U) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "script.sidecar.inflight_target_scene_ambiguous",
        "Multiple inflight scene contexts match the resolved target scene key");
      return SceneBindingContextOutcome {
        .state = std::nullopt,
        .failed = true,
      };
    }
    if (matches.empty()) {
      return SceneBindingContextOutcome {
        .state = std::nullopt,
        .failed = false,
      };
    }

    const auto* match = matches.front();
    if (match->descriptor_relpath.empty()) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "script.sidecar.target_scene_invalid",
        "Inflight scene context has empty descriptor_relpath");
      return SceneBindingContextOutcome {
        .state = std::nullopt,
        .failed = true,
      };
    }

    const auto state = BuildResolvedSceneStateFromDescriptor(session, request,
      match->scene_key, match->virtual_path, match->descriptor_relpath,
      match->descriptor_bytes);
    return SceneBindingContextOutcome {
      .state = state,
      .failed = !state.has_value(),
    };
  }

  auto ResolveSceneBindingContextFromCookedInspection(ImportSession& session,
    const ImportRequest& request, const data::AssetKey& scene_key,
    const std::vector<CookedInspectionContext>& cooked_contexts,
    IAsyncFileReader& reader) -> co::Co<SceneBindingContextOutcome>
  {
    using data::AssetType;

    for (auto it = cooked_contexts.rbegin(); it != cooked_contexts.rend();
      ++it) {
      const auto* scene_asset_entry
        = FindAssetEntryByKey(it->inspection, scene_key);
      if (scene_asset_entry == nullptr) {
        continue;
      }
      if (static_cast<AssetType>(scene_asset_entry->asset_type)
        != AssetType::kScene) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "script.sidecar.target_scene_not_scene",
          "Target scene virtual path does not reference a scene asset");
        co_return SceneBindingContextOutcome {
          .state = std::nullopt,
          .failed = true,
        };
      }

      const auto descriptor_path
        = it->cooked_root / scene_asset_entry->descriptor_relpath;
      const auto scene_read = co_await reader.ReadFile(descriptor_path);
      if (!scene_read.has_value()) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "script.sidecar.target_scene_read_failed",
          "Failed reading target scene descriptor: "
            + scene_read.error().ToString());
        co_return SceneBindingContextOutcome {
          .state = std::nullopt,
          .failed = true,
        };
      }

      const auto state = BuildResolvedSceneStateFromDescriptor(session, request,
        scene_asset_entry->key, scene_asset_entry->virtual_path,
        scene_asset_entry->descriptor_relpath, scene_read.value());
      co_return SceneBindingContextOutcome {
        .state = state,
        .failed = !state.has_value(),
      };
    }

    co_return SceneBindingContextOutcome {
      .state = std::nullopt,
      .failed = false,
    };
  }

  auto ResolveTargetSceneState(ImportSession& session,
    const ImportRequest& request, content::VirtualPathResolver& resolver,
    const std::vector<CookedInspectionContext>& cooked_contexts,
    IAsyncFileReader& reader) -> co::Co<std::optional<ResolvedSceneState>>
  {
    const auto& target_scene_path
      = request.options.scripting.target_scene_virtual_path;

    std::optional<data::AssetKey> resolver_key {};
    try {
      resolver_key = resolver.ResolveAssetKey(target_scene_path);
    } catch (const std::invalid_argument& ex) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "script.sidecar.target_scene_virtual_path_invalid",
        "Target scene virtual path is invalid: " + std::string(ex.what()));
      co_return std::nullopt;
    }

    const auto inflight_path_outcome
      = ResolveSceneBindingContextFromInflightByVirtualPath(session, request);
    if (inflight_path_outcome.state.has_value()) {
      co_return inflight_path_outcome.state;
    }
    if (inflight_path_outcome.failed) {
      co_return std::nullopt;
    }

    if (!resolver_key.has_value()) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "script.sidecar.target_scene_missing",
        "Target scene virtual path was not found: " + target_scene_path);
      co_return std::nullopt;
    }

    const auto inflight_key_outcome
      = ResolveSceneBindingContextFromInflightBySceneKey(
        session, request, *resolver_key);
    if (inflight_key_outcome.state.has_value()) {
      co_return inflight_key_outcome.state;
    }
    if (inflight_key_outcome.failed) {
      co_return std::nullopt;
    }

    const auto cooked_outcome
      = co_await ResolveSceneBindingContextFromCookedInspection(
        session, request, *resolver_key, cooked_contexts, reader);
    if (cooked_outcome.state.has_value()) {
      co_return cooked_outcome.state;
    }
    if (cooked_outcome.failed) {
      co_return std::nullopt;
    }

    AddDiagnostic(session, request, ImportSeverity::kError,
      "script.sidecar.target_scene_missing",
      "Resolved target scene key is not present in cooked scene context");
    co_return std::nullopt;
  }

  auto LoadScriptsTableState(ImportSession& session,
    const ImportRequest& request, const lc::Inspection& inspection,
    IAsyncFileReader& reader,
    const std::vector<script::ScriptingComponentRecord>& existing_components)
    -> co::Co<std::optional<ScriptsTableState>>
  {
    using data::loose_cooked::FileKind;

    auto state = ScriptsTableState {};
    for (const auto& file : inspection.Files()) {
      if (file.kind == FileKind::kScriptsTable) {
        state.scripts_table_relpath = file.relpath;
      } else if (file.kind == FileKind::kScriptsData) {
        state.scripts_data_relpath = file.relpath;
      }
    }

    state.has_existing_script_tables = state.scripts_table_relpath.has_value()
      || state.scripts_data_relpath.has_value();

    if (state.scripts_table_relpath.has_value()
      != state.scripts_data_relpath.has_value()) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "script.sidecar.scripts_pair_mismatch",
        "scripts.table and scripts.data must either both exist or both be "
        "absent");
      co_return std::nullopt;
    }

    if (state.scripts_table_relpath.has_value()) {
      const auto table_read = co_await reader.ReadFile(
        session.CookedRoot() / *state.scripts_table_relpath);
      if (!table_read.has_value()) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "script.sidecar.scripts_table_read_failed",
          "Failed reading scripts.table: " + table_read.error().ToString());
        co_return std::nullopt;
      }

      const auto data_read = co_await reader.ReadFile(
        session.CookedRoot() / *state.scripts_data_relpath);
      if (!data_read.has_value()) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "script.sidecar.scripts_data_read_failed",
          "Failed reading scripts.data: " + data_read.error().ToString());
        co_return std::nullopt;
      }

      if (!ReadTypedVectorFromBytes(table_read.value(), state.tables.slots)) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "script.sidecar.scripts_table_size_invalid",
          "scripts.table size is incompatible with ScriptSlotRecord layout");
        co_return std::nullopt;
      }
      if (!ReadTypedVectorFromBytes(data_read.value(), state.tables.params)) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "script.sidecar.scripts_data_size_invalid",
          "scripts.data size is incompatible with ScriptParamRecord layout");
        co_return std::nullopt;
      }
    } else if (!existing_components.empty()) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "script.sidecar.scripts_tables_missing",
        "Scene has existing scripting components but "
        "scripts.table/scripts.data "
        "are missing");
      co_return std::nullopt;
    }

    co_return state;
  }

  auto BuildMergedScriptsState(ImportSession& session,
    const ImportRequest& request, content::VirtualPathResolver& resolver,
    const SidecarDocument& parsed, const uint32_t node_count,
    const std::vector<script::ScriptingComponentRecord>& existing_components,
    const ExistingScriptTables& existing_tables)
    -> std::optional<MergedScriptsState>
  {
    using data::pak::core::OffsetT;

    auto merged_bindings = std::map<uint32_t, std::vector<SlotWithParams>> {};
    for (const auto& component : existing_components) {
      const auto slot_start = component.slot_start_index;
      const auto slot_count = component.slot_count;
      if (slot_start > existing_tables.slots.size()
        || slot_count > (existing_tables.slots.size() - slot_start)) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "script.sidecar.patch_map_invariant_failure",
          "Existing scene scripting slot range is out of bounds");
        return std::nullopt;
      }

      auto slots = std::vector<SlotWithParams> {};
      slots.reserve(slot_count);
      for (uint32_t i = 0; i < slot_count; ++i) {
        const auto& slot = existing_tables.slots[slot_start + i];
        const auto param_record_size
          = uint64_t { sizeof(script::ScriptParamRecord) };
        if ((slot.params_array_offset % param_record_size) != 0U) {
          AddDiagnostic(session, request, ImportSeverity::kError,
            "script.sidecar.patch_map_invariant_failure",
            "Existing ScriptSlotRecord param offset is not record-aligned");
          return std::nullopt;
        }
        const auto param_start
          = static_cast<size_t>(slot.params_array_offset / param_record_size);
        const auto param_count = static_cast<size_t>(slot.params_count);
        if (param_start > existing_tables.params.size()
          || param_count > (existing_tables.params.size() - param_start)) {
          AddDiagnostic(session, request, ImportSeverity::kError,
            "script.sidecar.patch_map_invariant_failure",
            "Existing ScriptSlotRecord param range is out of bounds");
          return std::nullopt;
        }

        auto payload = SlotWithParams {
          .slot_id = {},
          .slot = slot,
          .params = {},
        };
        payload.params.insert(payload.params.end(),
          existing_tables.params.begin() + static_cast<ptrdiff_t>(param_start),
          existing_tables.params.begin()
            + static_cast<ptrdiff_t>(param_start + param_count));
        slots.push_back(std::move(payload));
      }
      merged_bindings.insert_or_assign(component.node_index, std::move(slots));
    }

    auto incoming_bindings = std::map<uint32_t, std::vector<SlotWithParams>> {};
    for (size_t row_index = 0; row_index < parsed.rows.size(); ++row_index) {
      const auto& row = parsed.rows[row_index];
      const auto object_path = "bindings[" + std::to_string(row_index) + "]";
      if (row.node_index >= node_count) {
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          "script.sidecar.node_ref_unresolved",
          "Node index is out of bounds for target scene",
          object_path + ".node_index");
        continue;
      }

      auto resolved_script_key = std::optional<data::AssetKey> {};
      try {
        resolved_script_key = resolver.ResolveAssetKey(row.script_virtual_path);
      } catch (const std::invalid_argument& ex) {
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          "script.sidecar.script_virtual_path_invalid",
          "Script virtual path is invalid: " + std::string(ex.what()),
          object_path + ".script_virtual_path");
        continue;
      }
      if (!resolved_script_key.has_value()) {
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          "script.sidecar.script_ref_unresolved",
          "Script virtual path did not resolve to an asset key",
          object_path + ".script_virtual_path");
        continue;
      }

      auto payload = SlotWithParams {};
      payload.slot_id = row.slot_id;
      payload.slot.script_asset_key = *resolved_script_key;
      payload.slot.params_array_offset = 0;
      payload.slot.params_count = 0;
      payload.slot.execution_order = row.execution_order;
      payload.slot.flags = script::ScriptSlotFlags::kNone;
      payload.params = row.params;
      incoming_bindings[row.node_index].push_back(std::move(payload));
    }

    if (session.HasErrors()) {
      return std::nullopt;
    }

    for (auto& [node_index, slots] : incoming_bindings) {
      std::ranges::sort(
        slots, [](const SlotWithParams& lhs, const SlotWithParams& rhs) {
          return lhs.slot_id < rhs.slot_id;
        });
      merged_bindings.insert_or_assign(node_index, std::move(slots));
    }

    auto result = MergedScriptsState {};
    auto patch_refs = std::vector<ComponentSlotPatchRef> {};
    for (const auto& [node_index, slots] : merged_bindings) {
      if (slots.empty()) {
        continue;
      }
      if (result.slots.size() > (std::numeric_limits<uint32_t>::max)()) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "script.sidecar.slot_count_overflow",
          "Scripting slot count exceeded uint32 limits");
        return std::nullopt;
      }
      if (slots.size() > (std::numeric_limits<uint32_t>::max)()) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "script.sidecar.slot_count_overflow",
          "One scene node has too many script slots");
        return std::nullopt;
      }

      auto component = script::ScriptingComponentRecord {};
      component.node_index = node_index;
      component.flags = script::ScriptingComponentFlags::kNone;
      component.slot_start_index = 0;
      component.slot_count = 0;
      result.components.push_back(component);

      patch_refs.push_back(ComponentSlotPatchRef {
        .component_index = result.components.size() - 1U,
        .slot_start_index = static_cast<uint32_t>(result.slots.size()),
        .slot_count = static_cast<uint32_t>(slots.size()),
      });

      for (const auto& slot_payload : slots) {
        auto slot = slot_payload.slot;
        const auto param_offset = uint64_t { result.params.size() }
          * sizeof(script::ScriptParamRecord);
        if (param_offset > (std::numeric_limits<OffsetT>::max)()) {
          AddDiagnostic(session, request, ImportSeverity::kError,
            "script.sidecar.param_offset_overflow",
            "Script param offset exceeded OffsetT limits");
          return std::nullopt;
        }
        if (slot_payload.params.size()
          > (std::numeric_limits<uint32_t>::max)()) {
          AddDiagnostic(session, request, ImportSeverity::kError,
            "script.sidecar.param_count_overflow",
            "Script param count exceeded uint32 limits");
          return std::nullopt;
        }
        slot.params_array_offset = static_cast<OffsetT>(param_offset);
        slot.params_count = static_cast<uint32_t>(slot_payload.params.size());
        result.slots.push_back(slot);
        result.params.insert(result.params.end(), slot_payload.params.begin(),
          slot_payload.params.end());
      }
    }

    for (const auto& patch_ref : patch_refs) {
      DCHECK_F(patch_ref.component_index < result.components.size(),
        "Sidecar patch-ref invariant failure: component index is out of "
        "bounds");
      auto& component = result.components[patch_ref.component_index];
      component.slot_start_index = patch_ref.slot_start_index;
      component.slot_count = patch_ref.slot_count;
    }

    return result;
  }

  auto EmitPatchedScene(ImportSession& session, const ImportRequest& request,
    const ResolvedSceneState& scene_state,
    const std::vector<script::ScriptingComponentRecord>& merged_components)
    -> bool
  {
    using data::AssetType;

    auto patched_scene_bytes = std::vector<std::byte> {};
    auto patch_error = std::string {};
    if (!PatchSceneDescriptorScriptingComponents(
          scene_state.source_scene_descriptor, merged_components,
          patched_scene_bytes, patch_error)) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "script.sidecar.scene_patch_failed", std::move(patch_error));
      return false;
    }

    if (EffectiveContentHashingEnabled(request.options.with_content_hashing)) {
      const auto hash = util::ComputeContentHash(patched_scene_bytes);
      constexpr auto kContentHashOffset
        = offsetof(data::pak::core::AssetHeader, content_hash);
      std::memcpy(
        patched_scene_bytes.data() + kContentHashOffset, &hash, sizeof(hash));
    }

    try {
      session.AssetEmitter().Emit(scene_state.scene_key, AssetType::kScene,
        scene_state.scene_virtual_path, scene_state.scene_descriptor_relpath,
        std::span<const std::byte>(patched_scene_bytes));
    } catch (const std::exception& ex) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "script.sidecar.scene_emit_failed", ex.what());
      return false;
    }

    return true;
  }

  auto WriteScriptsTables(ImportSession& session, const ImportRequest& request,
    IAsyncFileWriter& writer, LooseCookedIndexRegistry& index_registry,
    const ScriptsTableState& scripts_table_state,
    const MergedScriptsState& merged_scripts_state) -> co::Co<bool>
  {
    using data::loose_cooked::FileKind;

    const auto table_relpath
      = scripts_table_state.scripts_table_relpath.value_or(
        BuildScriptsTableRelPath(request));
    const auto data_relpath = scripts_table_state.scripts_data_relpath.value_or(
      BuildScriptsDataRelPath(request));

    if (!merged_scripts_state.components.empty()
      || scripts_table_state.has_existing_script_tables) {
      const auto table_write
        = co_await writer.Write(session.CookedRoot() / table_relpath,
          std::as_bytes(std::span(merged_scripts_state.slots)),
          WriteOptions { .create_directories = true, .overwrite = true });
      if (!table_write.has_value()) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "script.sidecar.scripts_table_write_failed",
          "Failed writing scripts.table: " + table_write.error().ToString());
        co_return false;
      }

      const auto data_write
        = co_await writer.Write(session.CookedRoot() / data_relpath,
          std::as_bytes(std::span(merged_scripts_state.params)),
          WriteOptions { .create_directories = true, .overwrite = true });
      if (!data_write.has_value()) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "script.sidecar.scripts_data_write_failed",
          "Failed writing scripts.data: " + data_write.error().ToString());
        co_return false;
      }

      try {
        index_registry.RegisterExternalFile(
          session.CookedRoot(), FileKind::kScriptsTable, table_relpath);
        index_registry.RegisterExternalFile(
          session.CookedRoot(), FileKind::kScriptsData, data_relpath);
      } catch (const std::exception& ex) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "script.sidecar.index_registration_failed", ex.what());
        co_return false;
      }
    }

    co_return true;
  }

} // namespace
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
    nursery.Start([this]() -> co::Co<> { co_await Worker(); });
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
          "script.sidecar.pipeline_exception",
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
  const auto& scripting = req.options.scripting;

  if (scripting.import_kind != ScriptingImportKind::kScriptingSidecar) {
    AddDiagnostic(*session, req, ImportSeverity::kError,
      "script.request.invalid_import_kind",
      "Scripting sidecar import requires "
      "options.scripting.import_kind=kScriptingSidecar");
    co_return false;
  }

  if (scripting.target_scene_virtual_path.empty()) {
    AddDiagnostic(*session, req, ImportSeverity::kError,
      "script.request.target_scene_virtual_path_missing",
      "Scripting sidecar import requires target_scene_virtual_path");
    co_return false;
  }

  const auto parsed = ParseSidecarDocument(item.source_bytes, *session, req);
  if (!parsed.has_value()) {
    co_return false;
  }

  auto* const reader = session->FileReader().get();
  auto* const writer = session->FileWriter().get();
  auto* const index_registry = item.index_registry.get();
  if (reader == nullptr || writer == nullptr || index_registry == nullptr) {
    AddDiagnostic(*session, req, ImportSeverity::kError,
      "script.sidecar.io_unavailable",
      "Scripting sidecar emission requires file reader/writer/index registry");
    co_return false;
  }

  auto cooked_contexts = std::vector<CookedInspectionContext> {};
  cooked_contexts.reserve(1U + req.cooked_context_roots.size());

  auto primary_inspection = lc::Inspection {};
  if (!LoadInspectionFromRoot(
        session->CookedRoot(), *session, req, primary_inspection)) {
    co_return false;
  }
  cooked_contexts.push_back(CookedInspectionContext {
    .cooked_root = session->CookedRoot(),
    .inspection = std::move(primary_inspection),
  });

  for (const auto& context_root : req.cooked_context_roots) {
    auto context_inspection = lc::Inspection {};
    if (!LoadInspectionFromRoot(
          context_root, *session, req, context_inspection)) {
      co_return false;
    }
    cooked_contexts.push_back(CookedInspectionContext {
      .cooked_root = context_root,
      .inspection = std::move(context_inspection),
    });
  }

  auto resolver = content::VirtualPathResolver {};
  for (const auto& context : cooked_contexts) {
    try {
      resolver.AddLooseCookedRoot(context.cooked_root);
    } catch (const std::exception& ex) {
      AddDiagnostic(*session, req, ImportSeverity::kError,
        "script.sidecar.resolver_mount_failed",
        "Failed mounting cooked root for sidecar resolution: "
          + context.cooked_root.string() + " (" + ex.what() + ")");
      co_return false;
    }
  }

  const auto resolved_scene_state = co_await ResolveTargetSceneState(
    *session, req, resolver, cooked_contexts, *reader);
  if (!resolved_scene_state.has_value()) {
    co_return false;
  }

  const auto scripts_table_state = co_await LoadScriptsTableState(*session, req,
    cooked_contexts.front().inspection, *reader,
    resolved_scene_state->existing_components);
  if (!scripts_table_state.has_value()) {
    co_return false;
  }

  const auto merged_scripts_state = BuildMergedScriptsState(*session, req,
    resolver, *parsed, resolved_scene_state->node_count,
    resolved_scene_state->existing_components, scripts_table_state->tables);
  if (!merged_scripts_state.has_value()) {
    co_return false;
  }

  if (!EmitPatchedScene(*session, req, *resolved_scene_state,
        merged_scripts_state->components)) {
    co_return false;
  }

  const auto wrote_scripts_tables = co_await WriteScriptsTables(*session, req,
    *writer, *index_registry, *scripts_table_state, *merged_scripts_state);
  if (!wrote_scripts_tables) {
    co_return false;
  }

  co_return true;
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
