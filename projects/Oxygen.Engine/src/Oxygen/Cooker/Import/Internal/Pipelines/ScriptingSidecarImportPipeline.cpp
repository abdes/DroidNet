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
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/VirtualPathResolver.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/IAsyncFileWriter.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/Internal/AssetReferenceBuilder.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedIndexRegistry.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/ScriptingSidecarImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableRegistry.h>
#include <Oxygen/Cooker/Import/Internal/SidecarSceneResolver.h>
#include <Oxygen/Cooker/Import/Internal/Utils/ContentHashUtils.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>

namespace oxygen::content::import {
namespace {

  namespace script = data::pak::scripting;
  namespace world = data::pak::world;
  namespace lc = oxygen::content::lc;
  using SidecarCookedInspectionContext = detail::CookedInspectionContext;
  using SidecarResolvedSceneState = detail::ResolvedSceneState;

  const auto kScriptSidecarResolverDiagnostics
    = detail::SidecarSceneResolverDiagnostics {
        .index_load_failed_code = "script.sidecar.index_load_failed",
        .inflight_target_scene_ambiguous_code
        = "script.sidecar.inflight_target_scene_ambiguous",
        .target_scene_invalid_code = "script.sidecar.target_scene_invalid",
        .target_scene_not_scene_code = "script.sidecar.target_scene_not_scene",
        .target_scene_read_failed_code
        = "script.sidecar.target_scene_read_failed",
        .target_scene_virtual_path_invalid_code
        = "script.sidecar.target_scene_virtual_path_invalid",
        .target_scene_missing_code = "script.sidecar.target_scene_missing",
      };

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

  struct SceneScriptData final {
    std::vector<script::ScriptingComponentRecord> components;
    std::vector<data::pak::scripting::ScriptSlotRecord> slots;
    std::vector<data::pak::scripting::ScriptParamRecord> params;
  };

  [[nodiscard]] auto MakeBindingIdentity(
    const uint32_t node_index, const std::string_view slot_id) -> std::string
  {
    return std::to_string(node_index) + "|" + std::string { slot_id };
  }

  using ScriptParamParserFn = bool (*)(const nlohmann::json& value,
    data::pak::scripting::ScriptParamRecord& out, std::string& error);

  auto ParseBoolParamValue(const nlohmann::json& value,
    data::pak::scripting::ScriptParamRecord& out, std::string& error) -> bool
  {
    using data::pak::scripting::ScriptParamType;
    if (!value.is_boolean()) {
      error = "bool param requires boolean value";
      return false;
    }
    out.type = ScriptParamType::kBool;
    out.value.as_bool = value.get<bool>();
    return true;
  }

  auto ParseInt32ParamValue(const nlohmann::json& value,
    data::pak::scripting::ScriptParamRecord& out, std::string& error) -> bool
  {
    using data::pak::scripting::ScriptParamType;
    if (!value.is_number_integer()) {
      error = "int32 param requires integer value";
      return false;
    }
    const auto parsed = value.get<int64_t>();
    if (parsed < std::numeric_limits<int32_t>::min()
      || parsed > std::numeric_limits<int32_t>::max()) {
      error = "int32 param value is out of range";
      return false;
    }
    out.type = ScriptParamType::kInt32;
    out.value.as_int32 = static_cast<int32_t>(parsed);
    return true;
  }

  auto ParseFloatParamValue(const nlohmann::json& value,
    data::pak::scripting::ScriptParamRecord& out, std::string& error) -> bool
  {
    using data::pak::scripting::ScriptParamType;
    if (!value.is_number()) {
      error = "float param requires numeric value";
      return false;
    }
    out.type = ScriptParamType::kFloat;
    out.value.as_float = value.get<float>();
    return true;
  }

  auto ParseStringParamValue(const nlohmann::json& value,
    data::pak::scripting::ScriptParamRecord& out, std::string& error) -> bool
  {
    using data::pak::scripting::ScriptParamType;
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

  template <size_t N>
  auto ParseVectorParamValue(const nlohmann::json& value,
    data::pak::scripting::ScriptParamRecord& out,
    const data::pak::scripting::ScriptParamType param_type,
    const std::string_view type_name, std::string& error) -> bool
  {
    if (!value.is_array()) {
      error = std::string(type_name) + " param requires array value";
      return false;
    }
    if (value.size() != N) {
      error = std::string(type_name) + " param requires array size "
        + std::to_string(N);
      return false;
    }
    for (size_t i = 0; i < N; ++i) {
      if (!value.at(i).is_number()) {
        error = std::string(type_name)
          + " param array must contain numeric elements";
        return false;
      }
      out.value.as_vec[i] = value.at(i).get<float>();
    }
    out.type = param_type;
    return true;
  }

  auto ParseVec2ParamValue(const nlohmann::json& value,
    data::pak::scripting::ScriptParamRecord& out, std::string& error) -> bool
  {
    using data::pak::scripting::ScriptParamType;
    return ParseVectorParamValue<2>(
      value, out, ScriptParamType::kVec2, "vec2", error);
  }

  auto ParseVec3ParamValue(const nlohmann::json& value,
    data::pak::scripting::ScriptParamRecord& out, std::string& error) -> bool
  {
    using data::pak::scripting::ScriptParamType;
    return ParseVectorParamValue<3>(
      value, out, ScriptParamType::kVec3, "vec3", error);
  }

  auto ParseVec4ParamValue(const nlohmann::json& value,
    data::pak::scripting::ScriptParamRecord& out, std::string& error) -> bool
  {
    using data::pak::scripting::ScriptParamType;
    return ParseVectorParamValue<4>(
      value, out, ScriptParamType::kVec4, "vec4", error);
  }

  struct ScriptParamParserEntry final {
    std::string_view type_name;
    ScriptParamParserFn parse_fn = nullptr;
  };

  constexpr auto kScriptParamParsers = std::array<ScriptParamParserEntry, 7> {
    ScriptParamParserEntry { "bool", &ParseBoolParamValue },
    ScriptParamParserEntry { "int32", &ParseInt32ParamValue },
    ScriptParamParserEntry { "float", &ParseFloatParamValue },
    ScriptParamParserEntry { "string", &ParseStringParamValue },
    ScriptParamParserEntry { "vec2", &ParseVec2ParamValue },
    ScriptParamParserEntry { "vec3", &ParseVec3ParamValue },
    ScriptParamParserEntry { "vec4", &ParseVec4ParamValue },
  };

  auto FindScriptParamParser(const std::string_view type_name)
    -> const ScriptParamParserEntry*
  {
    const auto it = std::ranges::find_if(kScriptParamParsers,
      [type_name](const ScriptParamParserEntry& entry) -> bool {
        return entry.type_name == type_name;
      });
    return it == kScriptParamParsers.end() ? nullptr : &(*it);
  }

  auto ParseScriptParamRecord(const nlohmann::json& param,
    data::pak::scripting::ScriptParamRecord& out, std::string& error) -> bool
  {
    if (!param.is_object()) {
      error = "Param record must be an object";
      return false;
    }
    if (!param.contains("key") || !param.at("key").is_string()) {
      error = "Param record requires string field 'key'";
      return false;
    }
    if (!param.contains("type") || !param.at("type").is_string()) {
      error = "Param record requires string field 'type'";
      return false;
    }
    if (!param.contains("value")) {
      error = "Param record requires field 'value'";
      return false;
    }

    const auto key = param.at("key").get<std::string>();
    if (!CopyNullTerminated(key, std::span { out.key })) {
      error = "Param key exceeds ScriptParamRecord::key capacity";
      return false;
    }

    const auto type = param.at("type").get<std::string>();
    const auto& value = param.at("value");
    const auto* parser = FindScriptParamParser(type);
    if (parser == nullptr || parser->parse_fn == nullptr) {
      error = "Unsupported param type '" + type + "'";
      return false;
    }
    return parser->parse_fn(value, out, error);
  }

  auto ParseSidecarDocument(std::span<const std::byte> bytes,
    ImportSession& session, const ImportRequest& request)
    -> std::optional<SidecarDocument>
  {
    using nlohmann::json;

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
    if (!doc.contains("bindings") || !doc.at("bindings").is_array()) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "script.sidecar.payload_invalid",
        "Sidecar document requires array field 'bindings'");
      return std::nullopt;
    }

    SidecarDocument out {};
    for (size_t i = 0; i < doc.at("bindings").size(); ++i) {
      const auto object_path = "bindings[" + std::to_string(i) + "]";
      const auto& binding = doc.at("bindings").at(i);
      if (!binding.is_object()) {
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          "script.sidecar.payload_invalid", "Binding row must be an object",
          object_path);
        continue;
      }
      if (!binding.contains("node_index")
        || !binding.at("node_index").is_number_unsigned()) {
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          "script.sidecar.payload_invalid",
          "Binding row requires unsigned field 'node_index'", object_path);
        continue;
      }
      if (!binding.contains("slot_id") || !binding.at("slot_id").is_string()) {
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          "script.sidecar.payload_invalid",
          "Binding row requires string field 'slot_id'", object_path);
        continue;
      }
      if (!binding.contains("script_virtual_path")
        || !binding.at("script_virtual_path").is_string()) {
        AddDiagnosticAtPath(session, request, ImportSeverity::kError,
          "script.sidecar.payload_invalid",
          "Binding row requires string field 'script_virtual_path'",
          object_path);
        continue;
      }

      SidecarBindingRow row {};
      row.node_index = binding.at("node_index").get<uint32_t>();
      row.slot_id = binding.at("slot_id").get<std::string>();
      row.script_virtual_path
        = binding.at("script_virtual_path").get<std::string>();
      if (binding.contains("execution_order")) {
        if (!binding.at("execution_order").is_number_integer()) {
          AddDiagnosticAtPath(session, request, ImportSeverity::kError,
            "script.sidecar.payload_invalid",
            "Binding row field 'execution_order' must be integer", object_path);
          continue;
        }
        const auto order = binding.at("execution_order").get<int64_t>();
        if (order < std::numeric_limits<int32_t>::min()
          || order > std::numeric_limits<int32_t>::max()) {
          AddDiagnosticAtPath(session, request, ImportSeverity::kError,
            "script.sidecar.payload_invalid",
            "Binding row field 'execution_order' is out of int32 range",
            object_path);
          continue;
        }
        row.execution_order = static_cast<int32_t>(order);
      }

      if (binding.contains("params")) {
        if (!binding.at("params").is_array()) {
          AddDiagnosticAtPath(session, request, ImportSeverity::kError,
            "script.sidecar.payload_invalid",
            "Binding row field 'params' must be an array", object_path);
          continue;
        }
        for (size_t j = 0; j < binding.at("params").size(); ++j) {
          auto parsed_param = data::pak::scripting::ScriptParamRecord {};
          auto error = std::string {};
          const auto param_path
            = object_path + ".params[" + std::to_string(j) + "]";
          if (!ParseScriptParamRecord(
                binding.at("params").at(j), parsed_param, error)) {
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

    std::ranges::sort(out.rows,
      [](const SidecarBindingRow& lhs, const SidecarBindingRow& rhs) -> bool {
        if (lhs.node_index != rhs.node_index) {
          return lhs.node_index < rhs.node_index;
        }
        return lhs.slot_id < rhs.slot_id;
      });

    auto seen = std::unordered_map<std::string, size_t> {};
    for (size_t i = 0; i < out.rows.size(); ++i) {
      const auto identity = MakeBindingIdentity(
        out.rows.at(i).node_index, out.rows.at(i).slot_id);
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

  class SceneDescriptorPatcher final {
  public:
    SceneDescriptorPatcher(
      const SidecarResolvedSceneState& scene, const SceneScriptData& scripts)
      : source_descriptor_(scene.source_scene_descriptor)
      , scripts_(scripts)
      , environment_offset_(scene.environment_offset)
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
      std::ranges::sort(component_payloads_,
        [](const ComponentPayload& lhs, const ComponentPayload& rhs) -> bool {
          return lhs.component_type < rhs.component_type;
        });
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

      scene_string_size_
        = static_cast<uint64_t>(source_desc_.scene_strings.size);
      if (!RangeOk(source_desc_.scene_strings.offset, scene_string_size_)) {
        error = "Scene string table range is invalid";
        return false;
      }

      payload_end_ = static_cast<uint64_t>(sizeof(SceneAssetDesc));
      payload_end_ = (std::max)(payload_end_,
        source_desc_.nodes.offset + node_table_size_);
      payload_end_ = (std::max)(payload_end_,
        source_desc_.scene_strings.offset + scene_string_size_);
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
          &entry, source_descriptor_.data() + dir_offset, sizeof(entry));

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

        const auto component_type
          = static_cast<ComponentType>(entry.component_type);
        if (component_type == ComponentType::kScripting) {
          continue;
        }

        auto payload = ComponentPayload {
          .component_type = entry.component_type,
          .entry_size = entry.table.entry_size,
          .bytes = {},
        };
        payload.bytes.resize(static_cast<size_t>(table_size));
        std::memcpy(payload.bytes.data(),
          source_descriptor_.data() + static_cast<size_t>(entry.table.offset),
          payload.bytes.size());
        component_payloads_.push_back(std::move(payload));
      }

      return true;
    }

    auto AppendScriptingComponentPayload() -> void
    {
      using data::ComponentType;
      if (scripts_.components.empty()) {
        return;
      }

      auto payload = ComponentPayload {
        .component_type = static_cast<uint32_t>(ComponentType::kScripting),
        .entry_size = sizeof(script::ScriptingComponentRecord),
        .bytes = {},
      };
      payload.bytes.resize(
        scripts_.components.size() * sizeof(script::ScriptingComponentRecord));
      std::memcpy(
        payload.bytes.data(), scripts_.components.data(), payload.bytes.size());
      component_payloads_.push_back(std::move(payload));
    }

    auto CaptureTrailingBytes() -> void
    {
      const auto trailing
        = std::span(source_descriptor_).subspan(environment_offset_);
      trailing_bytes_.assign(trailing.begin(), trailing.end());
    }

    auto SerializePatchedDescriptor(
      std::vector<std::byte>& patched_descriptor, std::string& error) -> bool
    {
      using world::SceneComponentTableDesc;

      auto out = std::vector<std::byte> {};
      out.resize(sizeof(world::SceneAssetDesc));

      auto desc = source_desc_;
      desc.header.content_hash = {};
      desc.nodes.offset = sizeof(world::SceneAssetDesc);
      const auto scene_strings_offset
        = uint64_t { desc.nodes.offset } + node_table_size_;
      using SceneStringOffsetT = decltype(desc.scene_strings.offset);
      if (scene_strings_offset
        > std::numeric_limits<SceneStringOffsetT>::max()) {
        error = "Scene string table offset overflow";
        return false;
      }
      desc.scene_strings.offset
        = static_cast<SceneStringOffsetT>(scene_strings_offset);
      desc.component_table_directory_offset = 0;
      desc.component_table_count = 0;

      const auto append_bytes = [&](std::span<const std::byte> bytes) -> void {
        out.insert(out.end(), bytes.begin(), bytes.end());
      };

      append_bytes(std::span<const std::byte>(source_descriptor_.data()
          + static_cast<size_t>(source_desc_.nodes.offset),
        static_cast<size_t>(node_table_size_)));
      append_bytes(std::span<const std::byte>(source_descriptor_.data()
          + static_cast<size_t>(source_desc_.scene_strings.offset),
        static_cast<size_t>(scene_string_size_)));

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

        std::memcpy(
          out.data() + directory_offset, directory.data(), directory_size);
      }

      desc.script_slots = {};
      if (!scripts_.slots.empty()) {
        if (scripts_.slots.size() > std::numeric_limits<uint32_t>::max()) {
          error = "Scene script slot count exceeds the format limit";
          return false;
        }
        desc.script_slots = {
          .offset = out.size(),
          .count = static_cast<uint32_t>(scripts_.slots.size()),
          .entry_size = sizeof(script::ScriptSlotRecord),
        };
        auto slots = scripts_.slots;
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
        append_bytes(std::as_bytes(std::span(scripts_.params)));
      }
      append_bytes(trailing_bytes_);

      std::memcpy(out.data(), &desc, sizeof(desc));
      patched_descriptor = std::move(out);
      return true;
    }

    const std::vector<std::byte>& source_descriptor_;
    const SceneScriptData& scripts_;
    size_t environment_offset_;
    world::SceneAssetDesc source_desc_ {};
    uint64_t node_table_size_ = 0;
    uint64_t scene_string_size_ = 0;
    uint64_t payload_end_ = 0;
    std::vector<ComponentPayload> component_payloads_ {};
    std::vector<std::byte> trailing_bytes_ {};
  };

  auto ResolveMountedAssetTypeByKey(
    std::span<const SidecarCookedInspectionContext> cooked_contexts,
    const data::AssetKey& key) -> std::optional<data::AssetType>
  {
    for (size_t i = cooked_contexts.size(); i > 0; --i) {
      const auto& context = oxygen::base::CheckedAt(cooked_contexts, i - 1U);
      for (const auto& asset : context.inspection.Assets()) {
        if (asset.key == key) {
          return static_cast<data::AssetType>(asset.asset_type);
        }
      }
    }
    return std::nullopt;
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
        slots.push_back({
          .slot_id = {},
          .slot = slot,
          .params = scene.ReadScriptParameters(slot),
        });
      }
    }
    return bindings;
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
          "script.sidecar.slot_count_overflow",
          "Scripting slot count exceeded uint32 limits");
        return std::nullopt;
      }
      if (slots.size() > std::numeric_limits<uint32_t>::max()) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "script.sidecar.slot_count_overflow",
          "One scene node has too many script slots");
        return std::nullopt;
      }

      if (slots.size()
        > std::numeric_limits<uint32_t>::max() - serialized.slots.size()) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "script.sidecar.slot_count_overflow",
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
            "script.sidecar.param_offset_overflow",
            "Script param offset exceeded OffsetT limits");
          return std::nullopt;
        }
        if (slot_payload.params.size() > std::numeric_limits<uint32_t>::max()) {
          AddDiagnostic(session, request, ImportSeverity::kError,
            "script.sidecar.param_count_overflow",
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
      content::VirtualPathResolver& resolver, const SidecarDocument& parsed,
      const uint32_t node_count,
      std::span<const SidecarCookedInspectionContext> cooked_contexts,
      const data::SceneAsset& scene)
      : session_(session)
      , request_(request)
      , resolver_(resolver)
      , parsed_(parsed)
      , node_count_(node_count)
      , cooked_contexts_(cooked_contexts)
      , scene_(scene)
    {
    }

    auto Build() -> std::optional<SceneScriptData>
    {
      using data::pak::core::OffsetT;

      auto merged_bindings = BuildBindingsByNodeFromComponents(scene_);

      auto incoming_bindings
        = std::map<uint32_t, std::vector<SlotWithParams>> {};
      for (size_t row_index = 0; row_index < parsed_.rows.size(); ++row_index) {
        const auto& row = parsed_.rows.at(row_index);
        const auto object_path = "bindings[" + std::to_string(row_index) + "]";
        if (row.node_index >= node_count_) {
          AddDiagnosticAtPath(session_, request_, ImportSeverity::kError,
            "script.sidecar.node_ref_unresolved",
            "Node index is out of bounds for target scene",
            object_path + ".node_index");
          continue;
        }

        auto resolved_script_key = std::optional<data::AssetKey> {};
        try {
          resolved_script_key
            = resolver_.ResolveAssetKey(row.script_virtual_path);
        } catch (const std::invalid_argument& ex) {
          AddDiagnosticAtPath(session_, request_, ImportSeverity::kError,
            "script.sidecar.script_virtual_path_invalid",
            "Script virtual path is invalid: " + std::string(ex.what()),
            object_path + ".script_virtual_path");
          continue;
        }
        if (!resolved_script_key.has_value()) {
          AddDiagnosticAtPath(session_, request_, ImportSeverity::kError,
            "script.sidecar.script_ref_unresolved",
            "Script virtual path did not resolve to an asset key",
            object_path + ".script_virtual_path");
          continue;
        }
        const auto resolved_asset_type = ResolveMountedAssetTypeByKey(
          cooked_contexts_, *resolved_script_key);
        if (!resolved_asset_type.has_value()) {
          AddDiagnosticAtPath(session_, request_, ImportSeverity::kError,
            "script.sidecar.script_ref_unresolved",
            "Resolved script key is not present in mounted cooked context",
            object_path + ".script_virtual_path");
          continue;
        }
        if (*resolved_asset_type != data::AssetType::kScript) {
          AddDiagnosticAtPath(session_, request_, ImportSeverity::kError,
            "script.sidecar.script_ref_not_script_asset",
            "Resolved reference does not identify a script asset",
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

      if (session_.HasErrors()) {
        return std::nullopt;
      }

      for (auto& [node_index, slots] : incoming_bindings) {
        std::ranges::sort(slots,
          [](const SlotWithParams& lhs, const SlotWithParams& rhs) -> bool {
            return lhs.slot_id < rhs.slot_id;
          });
        merged_bindings.insert_or_assign(node_index, std::move(slots));
      }

      return SerializeBindingsByNode(session_, request_, merged_bindings);
    }

  private:
    ImportSession& session_;
    const ImportRequest& request_;
    content::VirtualPathResolver& resolver_;
    const SidecarDocument& parsed_;
    uint32_t node_count_ = 0;
    std::span<const SidecarCookedInspectionContext> cooked_contexts_;
    const data::SceneAsset& scene_;
  };

  auto BuildPatchedSceneDescriptor(ImportSession& session,
    const ImportRequest& request, const SidecarResolvedSceneState& scene_state,
    const SceneScriptData& scripts) -> std::optional<std::vector<std::byte>>
  {
    auto patched_scene_bytes = std::vector<std::byte> {};
    auto patch_error = std::string {};
    auto patcher = SceneDescriptorPatcher(scene_state, scripts);
    if (!patcher.Patch(patched_scene_bytes, patch_error)) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "script.sidecar.scene_patch_failed", std::move(patch_error));
      return std::nullopt;
    }

    if (EffectiveContentHashingEnabled(request.options.with_content_hashing)) {
      const auto hash = util::ComputeContentSha256(patched_scene_bytes);
      constexpr auto kContentHashOffset
        = offsetof(data::pak::core::AssetHeader, content_hash);
      std::memcpy(
        patched_scene_bytes.data() + kContentHashOffset, &hash, sizeof(hash));
    }

    return patched_scene_bytes;
  }

  auto EmitPatchedScene(ImportSession& session, const ImportRequest& request,
    const SidecarResolvedSceneState& scene_state,
    std::span<const std::byte> patched_scene_bytes,
    const SceneScriptData& scripts) -> co::Co<bool>
  {
    using data::AssetType;

    try {
      AssetReferenceBuilder references(scene_state.references.Resources());
      for (const auto& reference : scene_state.references.Keys()) {
        if (reference.kind != data::KeyReferenceKind::kAsset
          || reference.expected_type != data::AssetType::kScript) {
          references.AddKey(
            reference.kind, reference.key, reference.expected_type);
        }
      }
      for (const auto& slot : scripts.slots) {
        references.AddAsset(slot.script_asset_key, data::AssetType::kScript);
      }
      co_await session.AssetEmitter().EmitSync(scene_state.scene_key,
        AssetType::kScene, scene_state.scene_virtual_path,
        scene_state.scene_descriptor_relpath, patched_scene_bytes,
        std::move(references).Build());
    } catch (const std::exception& ex) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "script.sidecar.scene_emit_failed", ex.what());
      co_return false;
    }

    co_return true;
  }

  struct ScriptingSidecarIoHandles final {
    IAsyncFileReader* reader = nullptr;
    IAsyncFileWriter* writer = nullptr;
    LooseCookedIndexRegistry* index_registry = nullptr;
  };

  auto ValidateScriptingSidecarRequest(
    ImportSession& session, const ImportRequest& request) -> bool
  {
    const auto& scripting = request.options.scripting;
    if (scripting.import_kind != ScriptingImportKind::kScriptingSidecar) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "script.request.invalid_import_kind",
        "Scripting sidecar import requires "
        "options.scripting.import_kind=kScriptingSidecar");
      return false;
    }

    if (scripting.target_scene_virtual_path.empty()) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "script.request.target_scene_virtual_path_missing",
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
      .reader = session.CookedReader().get(),
      .writer = session.FileWriter().get(),
      .index_registry = index_registry.get(),
    };
    if (handles.reader == nullptr || handles.writer == nullptr
      || handles.index_registry == nullptr) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "script.sidecar.io_unavailable",
        "Scripting sidecar emission requires file reader/writer/index "
        "registry");
      return std::nullopt;
    }
    return handles;
  }

  auto LoadCookedContextsAndMountResolver(ImportSession& session,
    const ImportRequest& request,
    std::vector<SidecarCookedInspectionContext>& cooked_contexts,
    content::VirtualPathResolver& resolver) -> bool
  {
    cooked_contexts.clear();
    cooked_contexts.reserve(1U + request.cooked_context_roots.size());

    auto primary_context = SidecarCookedInspectionContext {};
    if (!detail::LoadCookedInspectionContext(session.CookedRoot(), session,
          request, kScriptSidecarResolverDiagnostics, primary_context)) {
      return false;
    }
    cooked_contexts.push_back(std::move(primary_context));

    for (const auto& context_root : request.cooked_context_roots) {
      auto context = SidecarCookedInspectionContext {};
      if (!detail::LoadCookedInspectionContext(context_root, session, request,
            kScriptSidecarResolverDiagnostics, context)) {
        return false;
      }
      cooked_contexts.push_back(std::move(context));
    }

    for (const auto& context : cooked_contexts) {
      try {
        resolver.AddLooseCookedRoot(context.cooked_root);
      } catch (const std::exception& ex) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "script.sidecar.resolver_mount_failed",
          "Failed mounting cooked root for sidecar resolution: "
            + context.cooked_root.string() + " (" + ex.what() + ")");
        return false;
      }
    }

    return true;
  }

  auto ApplyScriptingSidecar(ImportSession& session,
    const ImportRequest& request, content::VirtualPathResolver& resolver,
    std::span<const SidecarCookedInspectionContext> cooked_contexts,
    const SidecarDocument& parsed,
    const SidecarResolvedSceneState& resolved_scene_state) -> co::Co<bool>
  {
    const data::SceneAsset scene(resolved_scene_state.scene_key,
      std::span<const std::byte>(resolved_scene_state.source_scene_descriptor));
    auto builder = SceneScriptBuilder(session, request, resolver, parsed,
      resolved_scene_state.node_count, cooked_contexts, scene);
    const auto scripts = builder.Build();
    if (!scripts) {
      co_return false;
    }
    const auto patched = BuildPatchedSceneDescriptor(
      session, request, resolved_scene_state, *scripts);
    if (!patched) {
      co_return false;
    }
    if (!co_await EmitPatchedScene(
          session, request, resolved_scene_state, *patched, *scripts)) {
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
    nursery.Start([this] -> co::Co<> { co_await Worker(); });
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
  if (!ValidateScriptingSidecarRequest(*session, req)) {
    co_return false;
  }

  const auto parsed = ParseSidecarDocument(item.source_bytes, *session, req);
  if (!parsed.has_value()) {
    co_return false;
  }

  const auto io_handles
    = ResolveScriptingSidecarIoHandles(*session, req, item.index_registry);
  if (!io_handles.has_value()) {
    co_return false;
  }

  auto cooked_contexts = std::vector<SidecarCookedInspectionContext> {};
  auto resolver = content::VirtualPathResolver {};
  if (!LoadCookedContextsAndMountResolver(
        *session, req, cooked_contexts, resolver)) {
    co_return false;
  }

  auto descriptor_edit = co_await item.index_registry->LockDescriptor(
    session->CookedRoot(), req.options.scripting.target_scene_virtual_path);

  const auto resolved_scene_state = co_await detail::ResolveTargetSceneState(
    *session, req, resolver, cooked_contexts, *io_handles->reader,
    req.options.scripting.target_scene_virtual_path,
    kScriptSidecarResolverDiagnostics);
  if (!resolved_scene_state.has_value()) {
    co_return false;
  }

  co_return co_await ApplyScriptingSidecar(
    *session, req, resolver, cooked_contexts, *parsed, *resolved_scene_state);
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
