//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/NodeReferenceJson.h>
#include <Oxygen/Cooker/Import/Internal/ScriptingSidecarSource.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/StableId.h>

namespace oxygen::content::import {
namespace {

  namespace script = data::pak::scripting;

  //! A location in the sidecar document.
  using JsonPointer = nlohmann::json::json_pointer;

  using SidecarParam = ScriptingSidecarParam;

  //! Adds errors to the sink and tells whether this preparation added any.
  class ErrorSink final {
  public:
    ErrorSink(std::vector<ImportDiagnostic>& diagnostics,
      const std::filesystem::path& source_path)
      : diagnostics_(&diagnostics)
      , source_path_(source_path.string())
      , first_(diagnostics.size())
    {
    }

    auto Add(std::string code, std::string message,
      const JsonPointer& location = JsonPointer {}) -> void
    {
      diagnostics_->push_back({
        .severity = ImportSeverity::kError,
        .code = std::move(code),
        .message = std::move(message),
        .source_path = source_path_,
        .json_pointer = location.to_string(),
      });
    }

    //! Whether an error was added since the sink was made.
    [[nodiscard]] auto HasErrors() const -> bool
    {
      return std::ranges::any_of(
        std::span<const ImportDiagnostic>(*diagnostics_).subspan(first_),
        [](const ImportDiagnostic& diagnostic) -> bool {
          return diagnostic.severity == ImportSeverity::kError;
        });
    }

  private:
    std::vector<ImportDiagnostic>* diagnostics_;
    std::string source_path_;
    size_t first_;
  };

  using ScriptParamParserFn = bool (*)(
    const nlohmann::json& value, SidecarParam& out, std::string& error);

  auto ParseBoolParamValue(const nlohmann::json& value, SidecarParam& param,
    std::string& error) -> bool
  {
    auto& out = param.record;
    if (!value.is_boolean()) {
      error = "bool param requires boolean value";
      return false;
    }
    out.type = script::ScriptParamType::kBool;
    out.value.as_bool = value.get<bool>() ? uint8_t { 1U } : uint8_t { 0U };
    return true;
  }

  auto ParseInt32ParamValue(const nlohmann::json& value, SidecarParam& param,
    std::string& error) -> bool
  {
    auto& out = param.record;
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
    out.type = script::ScriptParamType::kInt32;
    out.value.as_int32 = static_cast<int32_t>(parsed);
    return true;
  }

  auto ParseFloatParamValue(const nlohmann::json& value, SidecarParam& param,
    std::string& error) -> bool
  {
    auto& out = param.record;
    if (!value.is_number()) {
      error = "float param requires numeric value";
      return false;
    }
    out.type = script::ScriptParamType::kFloat;
    out.value.as_float = value.get<float>();
    return true;
  }

  auto ParseStringParamValue(const nlohmann::json& value, SidecarParam& param,
    std::string& error) -> bool
  {
    auto& out = param.record;
    if (!value.is_string()) {
      error = "string param requires string value";
      return false;
    }
    param.string_value = value.get<std::string>();
    if (param.string_value.contains('\0')) {
      error = "string param value holds a NUL character";
      return false;
    }
    out.type = script::ScriptParamType::kString;
    return true;
  }

  template <size_t N>
  auto ParseVectorParamValue(const nlohmann::json& value,
    script::ScriptParamRecord& out, const script::ScriptParamType param_type,
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
    auto components = std::array<float, N> {};
    for (size_t i = 0; i < N; ++i) {
      if (!value.at(i).is_number()) {
        error = std::string(type_name)
          + " param array must contain numeric elements";
        return false;
      }
      components.at(i) = value.at(i).get<float>();
    }
    std::ranges::copy(components, std::begin(out.value.as_vec));
    out.type = param_type;
    return true;
  }

  auto ParseVec2ParamValue(const nlohmann::json& value, SidecarParam& param,
    std::string& error) -> bool
  {
    return ParseVectorParamValue<2>(
      value, param.record, script::ScriptParamType::kVec2, "vec2", error);
  }

  auto ParseVec3ParamValue(const nlohmann::json& value, SidecarParam& param,
    std::string& error) -> bool
  {
    return ParseVectorParamValue<3>(
      value, param.record, script::ScriptParamType::kVec3, "vec3", error);
  }

  auto ParseVec4ParamValue(const nlohmann::json& value, SidecarParam& param,
    std::string& error) -> bool
  {
    return ParseVectorParamValue<4>(
      value, param.record, script::ScriptParamType::kVec4, "vec4", error);
  }

  auto ParseAssetParamValue(const nlohmann::json& value, SidecarParam& param,
    std::string& error) -> bool
  {
    if (!value.is_string() || value.get<std::string>().empty()) {
      error = "asset param requires a virtual path string value";
      return false;
    }
    param.record.type = script::ScriptParamType::kAsset;
    param.asset_virtual_path = value.get<std::string>();
    return true;
  }

  //! A node reference: `{ "node": index, "path": [node IDs] }`, the path
  //! naming a node the instance rooted at `node` brings in.
  auto ParseNodeParamValue(const nlohmann::json& value, SidecarParam& param,
    std::string& error) -> bool
  {
    if (!value.is_object() || !value.contains("node")
      || !value.at("node").is_number_unsigned()
      || value.at("node").get<uint64_t>() >= script::kUnsetNodeParam) {
      error = "node param requires a node reference with a scene node index";
      return false;
    }
    if (value.contains("path")) {
      auto path = internal::ParseNodePath(value.at("path"));
      if (!path.has_value()) {
        error = "node param path must hold one non-nil node ID per level";
        return false;
      }
      param.node_path = std::move(*path);
    }
    param.record.type = script::ScriptParamType::kNode;
    param.record.value.as_node = {
      .node_index = value.at("node").get<uint32_t>(),
      .node_path = data::pak::world::kNoNodePath,
    };
    return true;
  }

  struct ScriptParamParserEntry final {
    std::string_view type_name;
    ScriptParamParserFn parse_fn = nullptr;
  };

  constexpr auto kScriptParamParsers = std::array<ScriptParamParserEntry, 9> {
    ScriptParamParserEntry {
      .type_name = "bool",
      .parse_fn = &ParseBoolParamValue,
    },
    ScriptParamParserEntry {
      .type_name = "int32",
      .parse_fn = &ParseInt32ParamValue,
    },
    ScriptParamParserEntry {
      .type_name = "float",
      .parse_fn = &ParseFloatParamValue,
    },
    ScriptParamParserEntry {
      .type_name = "string",
      .parse_fn = &ParseStringParamValue,
    },
    ScriptParamParserEntry {
      .type_name = "vec2",
      .parse_fn = &ParseVec2ParamValue,
    },
    ScriptParamParserEntry {
      .type_name = "vec3",
      .parse_fn = &ParseVec3ParamValue,
    },
    ScriptParamParserEntry {
      .type_name = "vec4",
      .parse_fn = &ParseVec4ParamValue,
    },
    ScriptParamParserEntry {
      .type_name = "asset",
      .parse_fn = &ParseAssetParamValue,
    },
    ScriptParamParserEntry {
      .type_name = "node",
      .parse_fn = &ParseNodeParamValue,
    },
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

  auto ParseScriptParamRecord(
    const nlohmann::json& param, SidecarParam& out, std::string& error) -> bool
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
    if (key.empty()) {
      error = "Param key must not be empty";
      return false;
    }
    if (key.contains('\0')) {
      error = "Param key holds a NUL character";
      return false;
    }
    out.key = key;

    const auto type = param.at("type").get<std::string>();
    const auto& value = param.at("value");
    const auto* parser = FindScriptParamParser(type);
    if (parser == nullptr || parser->parse_fn == nullptr) {
      error = "Unsupported param type '" + type + "'";
      return false;
    }
    return parser->parse_fn(value, out, error);
  }

  //! Reads a row's `params`, each key once; reports every violation.
  auto ParseRowParams(ErrorSink& errors, const nlohmann::json& binding,
    const JsonPointer& location, std::vector<SidecarParam>& params) -> bool
  {
    if (!binding.contains("params")) {
      return true;
    }
    if (!binding.at("params").is_array()) {
      errors.Add("script.sidecar.payload_invalid",
        "Row field 'params' must be an array", location / "params");
      return false;
    }
    for (size_t j = 0; j < binding.at("params").size(); ++j) {
      auto parsed_param = SidecarParam {};
      auto error = std::string {};
      const auto param_location = location / "params" / j;
      if (!ParseScriptParamRecord(
            binding.at("params").at(j), parsed_param, error)) {
        errors.Add(
          "script.sidecar.param_invalid", std::move(error), param_location);
        continue;
      }
      const auto duplicate = std::ranges::any_of(
        params, [&parsed_param](const SidecarParam& existing) -> bool {
          return existing.key == parsed_param.key;
        });
      if (duplicate) {
        errors.Add("script.sidecar.param_duplicate",
          "Parameter '" + parsed_param.key
            + "' is overridden twice in one slot",
          param_location / "key");
        continue;
      }
      params.push_back(std::move(parsed_param));
    }
    return true;
  }

  //! The sidecar in its object form: a bare array of rows is `bindings`.
  auto NormalizeDocument(nlohmann::json parsed) -> nlohmann::json
  {
    if (parsed.is_array()) {
      return nlohmann::json { { "bindings", std::move(parsed) } };
    }
    return parsed;
  }

  //! Reads one `bindings` row, or reports why it cannot be read.
  auto ParseBindingRow(ErrorSink& errors, const nlohmann::json& binding,
    const size_t index) -> std::optional<ScriptingSidecarBindingRow>
  {
    const auto location = JsonPointer {} / "bindings" / index;
    if (!binding.is_object()) {
      errors.Add("script.sidecar.payload_invalid",
        "Binding row must be an object", location);
      return std::nullopt;
    }
    if (!binding.contains("node_index")
      || !binding.at("node_index").is_number_unsigned()) {
      errors.Add("script.sidecar.payload_invalid",
        "Binding row requires unsigned field 'node_index'", location);
      return std::nullopt;
    }
    const auto slot_id = binding.contains("slot_id")
      ? internal::ParseNonNilId<data::ScriptSlotId>(binding.at("slot_id"))
      : std::nullopt;
    if (!slot_id.has_value()) {
      errors.Add("script.sidecar.slot_id_invalid",
        "Binding row requires a non-nil UUID 'slot_id'", location / "slot_id");
      return std::nullopt;
    }
    if (!binding.contains("script_virtual_path")
      || !binding.at("script_virtual_path").is_string()) {
      errors.Add("script.sidecar.payload_invalid",
        "Binding row requires string field 'script_virtual_path'", location);
      return std::nullopt;
    }

    ScriptingSidecarBindingRow row {};
    row.source_index = index;
    row.node_index = binding.at("node_index").get<uint32_t>();
    row.slot_id = *slot_id;
    row.script_virtual_path
      = binding.at("script_virtual_path").get<std::string>();
    if (binding.contains("execution_order")) {
      if (!binding.at("execution_order").is_number_integer()) {
        errors.Add("script.sidecar.payload_invalid",
          "Binding row field 'execution_order' must be integer", location);
        return std::nullopt;
      }
      const auto order = binding.at("execution_order").get<int64_t>();
      if (order < std::numeric_limits<int32_t>::min()
        || order > std::numeric_limits<int32_t>::max()) {
        errors.Add("script.sidecar.payload_invalid",
          "Binding row field 'execution_order' is out of int32 range",
          location);
        return std::nullopt;
      }
      row.execution_order = static_cast<int32_t>(order);
    }
    if (!ParseRowParams(errors, binding, location, row.params)) {
      return std::nullopt;
    }
    return row;
  }

  //! Reads one `instance_bindings` row, or reports why it cannot be read.
  auto ParseInstanceRow(ErrorSink& errors, const nlohmann::json& binding,
    const size_t index) -> std::optional<ScriptingSidecarInstanceRow>
  {
    const auto location = JsonPointer {} / "instance_bindings" / index;
    if (!binding.is_object() || !binding.contains("node_index")
      || !binding.at("node_index").is_number_unsigned()) {
      errors.Add("script.sidecar.payload_invalid",
        "Instance binding row requires unsigned field 'node_index'", location);
      return std::nullopt;
    }
    auto path = binding.contains("node_path")
      ? internal::ParseNodePath(binding.at("node_path"))
      : std::unexpected(size_t { 0 });
    if (!path.has_value()) {
      errors.Add("script.sidecar.node_path_invalid",
        "Instance binding row requires a 'node_path' of non-nil node IDs",
        location / "node_path");
      return std::nullopt;
    }
    const auto slot_id = binding.contains("slot_id")
      ? internal::ParseNonNilId<data::ScriptSlotId>(binding.at("slot_id"))
      : std::nullopt;
    if (!slot_id.has_value()) {
      errors.Add("script.sidecar.slot_id_invalid",
        "Instance binding row requires a non-nil UUID 'slot_id'",
        location / "slot_id");
      return std::nullopt;
    }
    auto row = ScriptingSidecarInstanceRow {
      .node_index = binding.at("node_index").get<uint32_t>(),
      .node_path = std::move(*path),
      .slot_id = *slot_id,
      .params = {},
      .source_index = index,
    };
    if (!ParseRowParams(errors, binding, location, row.params)) {
      return std::nullopt;
    }
    return row;
  }

  //! Reports the rows that name one slot of one node twice.
  auto CheckDuplicateSlots(ErrorSink& errors, ScriptingSidecarDocument& out)
    -> void
  {
    std::ranges::sort(
      out.rows, {}, [](const ScriptingSidecarBindingRow& row) -> auto {
        return std::tie(row.node_index, row.slot_id);
      });
    for (size_t i = 1; i < out.rows.size(); ++i) {
      const auto& previous = out.rows.at(i - 1);
      const auto& row = out.rows.at(i);
      if (previous.node_index == row.node_index
        && previous.slot_id == row.slot_id) {
        errors.Add("script.sidecar.duplicate_slot_conflict",
          "Duplicate sidecar binding identity ("
            + std::to_string(row.node_index) + "|"
            + data::to_string(row.slot_id) + ")",
          JsonPointer {} / "bindings" / row.source_index);
      }
    }
    std::ranges::sort(out.instance_rows, {},
      [](const ScriptingSidecarInstanceRow& row) -> auto {
        return std::tie(row.node_index, row.node_path, row.slot_id);
      });
    for (size_t i = 1; i < out.instance_rows.size(); ++i) {
      const auto& previous = out.instance_rows.at(i - 1);
      const auto& row = out.instance_rows.at(i);
      if (std::tie(previous.node_index, previous.node_path, previous.slot_id)
        == std::tie(row.node_index, row.node_path, row.slot_id)) {
        errors.Add("script.sidecar.duplicate_slot_conflict",
          "Two instance binding rows change one slot of one instanced node",
          JsonPointer {} / "instance_bindings" / row.source_index);
      }
    }
  }

} // namespace

auto PrepareScriptingSidecarDocument(const std::string_view bytes,
  const std::filesystem::path& source_path,
  std::vector<ImportDiagnostic>& diagnostics)
  -> std::optional<ScriptingSidecarDocument>
{
  auto errors = ErrorSink { diagnostics, source_path };

  auto parsed = nlohmann::json {};
  try {
    parsed = nlohmann::json::parse(bytes);
  } catch (const std::exception& ex) {
    errors.Add("script.sidecar.parse_failed",
      "Failed to parse sidecar document as JSON: " + std::string(ex.what()));
    return std::nullopt;
  }

  if (!parsed.is_object() && !parsed.is_array()) {
    errors.Add("script.sidecar.payload_invalid",
      "Sidecar document root must be a JSON object or an array of rows");
    return std::nullopt;
  }
  const auto doc = NormalizeDocument(std::move(parsed));
  if (!doc.contains("bindings") || !doc.at("bindings").is_array()) {
    errors.Add("script.sidecar.payload_invalid",
      "Sidecar document requires array field 'bindings'");
    return std::nullopt;
  }

  auto out = ScriptingSidecarDocument {};
  const auto& bindings = doc.at("bindings");
  for (size_t i = 0; i < bindings.size(); ++i) {
    if (auto row = ParseBindingRow(errors, bindings.at(i), i)) {
      out.rows.push_back(std::move(*row));
    }
  }

  if (doc.contains("instance_bindings")) {
    if (!doc.at("instance_bindings").is_array()) {
      errors.Add("script.sidecar.payload_invalid",
        "Sidecar field 'instance_bindings' must be an array",
        JsonPointer {} / "instance_bindings");
      return std::nullopt;
    }
    const auto& instance_bindings = doc.at("instance_bindings");
    for (size_t i = 0; i < instance_bindings.size(); ++i) {
      if (auto row = ParseInstanceRow(errors, instance_bindings.at(i), i)) {
        out.instance_rows.push_back(std::move(*row));
      }
    }
  }

  if (errors.HasErrors()) {
    return std::nullopt;
  }
  CheckDuplicateSlots(errors, out);
  if (errors.HasErrors()) {
    return std::nullopt;
  }
  return out;
}

auto NormalizeScriptingSidecarText(const std::string_view text)
  -> std::optional<std::string>
{
  try {
    return NormalizeDocument(nlohmann::json::parse(text)).dump();
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

} // namespace oxygen::content::import
