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
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/NodeReferenceJson.h>
#include <Oxygen/Cooker/Import/Internal/ScriptingSidecarSource.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
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

  //! Reads the value of a param whose shape the schema has checked.
  using ScriptParamParserFn = bool (*)(
    const nlohmann::json& value, SidecarParam& out, std::string& error);

  auto ParseBoolParamValue(const nlohmann::json& value, SidecarParam& param,
    std::string& /*error*/) -> bool
  {
    auto& out = param.record;
    out.type = script::ScriptParamType::kBool;
    out.value.as_bool = value.get<bool>() ? uint8_t { 1U } : uint8_t { 0U };
    return true;
  }

  auto ParseInt32ParamValue(const nlohmann::json& value, SidecarParam& param,
    std::string& /*error*/) -> bool
  {
    auto& out = param.record;
    out.type = script::ScriptParamType::kInt32;
    out.value.as_int32 = value.get<int32_t>();
    return true;
  }

  auto ParseFloatParamValue(const nlohmann::json& value, SidecarParam& param,
    std::string& /*error*/) -> bool
  {
    auto& out = param.record;
    out.type = script::ScriptParamType::kFloat;
    out.value.as_float = value.get<float>();
    return true;
  }

  auto ParseStringParamValue(const nlohmann::json& value, SidecarParam& param,
    std::string& /*error*/) -> bool
  {
    param.string_value = value.get<std::string>();
    param.record.type = script::ScriptParamType::kString;
    return true;
  }

  template <size_t N>
  auto ParseVectorParamValue(const nlohmann::json& value,
    script::ScriptParamRecord& out, const script::ScriptParamType param_type)
    -> bool
  {
    auto components = std::array<float, N> {};
    for (size_t i = 0; i < N; ++i) {
      components.at(i) = value.at(i).get<float>();
    }
    std::ranges::copy(components, std::begin(out.value.as_vec));
    out.type = param_type;
    return true;
  }

  auto ParseVec2ParamValue(const nlohmann::json& value, SidecarParam& param,
    std::string& /*error*/) -> bool
  {
    return ParseVectorParamValue<2>(
      value, param.record, script::ScriptParamType::kVec2);
  }

  auto ParseVec3ParamValue(const nlohmann::json& value, SidecarParam& param,
    std::string& /*error*/) -> bool
  {
    return ParseVectorParamValue<3>(
      value, param.record, script::ScriptParamType::kVec3);
  }

  auto ParseVec4ParamValue(const nlohmann::json& value, SidecarParam& param,
    std::string& /*error*/) -> bool
  {
    return ParseVectorParamValue<4>(
      value, param.record, script::ScriptParamType::kVec4);
  }

  auto ParseAssetParamValue(const nlohmann::json& value, SidecarParam& param,
    std::string& /*error*/) -> bool
  {
    param.record.type = script::ScriptParamType::kAsset;
    param.asset_virtual_path = value.get<std::string>();
    return true;
  }

  //! A node reference: `{ "node": index, "path": [node IDs] }`, the path
  //! naming a node the instance rooted at `node` brings in. The schema states
  //! the shape; the IDs of the path are checked here.
  auto ParseNodeParamValue(const nlohmann::json& value, SidecarParam& param,
    std::string& error) -> bool
  {
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

  //! Reads one param the schema accepted.
  auto ParseScriptParamRecord(
    const nlohmann::json& param, SidecarParam& out, std::string& error) -> bool
  {
    out.key = param.at("key").get<std::string>();
    const auto type = param.at("type").get<std::string>();
    const auto* parser = FindScriptParamParser(type);
    if (parser == nullptr) {
      // The schema lists the types of the table, so this is a mismatch of the
      // two.
      error = "Unsupported param type '" + type + "'";
      return false;
    }
    return parser->parse_fn(param.at("value"), out, error);
  }

  //! Reads a row's `params`, each key once; reports every violation.
  auto ParseRowParams(ErrorSink& errors, const nlohmann::json& binding,
    const JsonPointer& location, std::vector<SidecarParam>& params) -> void
  {
    if (!binding.contains("params")) {
      return;
    }
    for (size_t j = 0; j < binding.at("params").size(); ++j) {
      auto parsed_param = SidecarParam {};
      auto error = std::string {};
      const auto param_location = location / "params" / j;
      if (!ParseScriptParamRecord(
            binding.at("params").at(j), parsed_param, error)) {
        errors.Add(std::string(import_codes::kScriptSidecarParamInvalid),
          std::move(error), param_location);
        continue;
      }
      const auto duplicate = std::ranges::any_of(
        params, [&parsed_param](const SidecarParam& existing) -> bool {
          return existing.key == parsed_param.key;
        });
      if (duplicate) {
        errors.Add(std::string(import_codes::kScriptSidecarParamDuplicate),
          "Parameter '" + parsed_param.key
            + "' is overridden twice in one slot",
          param_location / "key");
        continue;
      }
      params.push_back(std::move(parsed_param));
    }
  }

  //! The sidecar in its object form: a bare array of rows is `bindings`.
  auto NormalizeDocument(nlohmann::json parsed) -> nlohmann::json
  {
    if (parsed.is_array()) {
      return nlohmann::json { { "bindings", std::move(parsed) } };
    }
    return parsed;
  }

  auto ScriptingSidecarSchemaValidator()
    -> nlohmann::json_schema::json_validator&
  {
    thread_local auto validator = internal::MakeJsonSchemaValidator(
      nlohmann::json::parse(kScriptingSidecarSchema));
    return validator;
  }

  //! Reports every way `doc` departs from the sidecar schema.
  auto ValidateSchema(ErrorSink& errors, const nlohmann::json& doc) -> bool
  {
    constexpr auto kSchemaDiagConfig
      = internal::JsonSchemaValidationDiagnosticConfig {
          .validation_failed_code
          = import_codes::kScriptSidecarSchemaValidationFailed,
          .validation_failed_prefix = "Schema validation failed: ",
          .validator_failure_code
          = import_codes::kScriptSidecarSchemaValidatorFailure,
          .validator_failure_prefix
          = "Scripting schema validation engine failure: ",
        };

    return internal::ValidateJsonSchemaWithDiagnostics(
      ScriptingSidecarSchemaValidator(), doc, kSchemaDiagConfig,
      [&errors](const std::string_view code, std::string message,
        const std::string& json_pointer) -> void {
        errors.Add(
          std::string(code), std::move(message), JsonPointer { json_pointer });
      });
  }

  //! Reads one `bindings` row the schema accepted, or reports why it cannot
  //! be read.
  auto ParseBindingRow(ErrorSink& errors, const nlohmann::json& binding,
    const size_t index) -> std::optional<ScriptingSidecarBindingRow>
  {
    const auto location = JsonPointer {} / "bindings" / index;
    const auto slot_id
      = internal::ParseNonNilId<data::ScriptSlotId>(binding.at("slot_id"));
    if (!slot_id.has_value()) {
      errors.Add(std::string(import_codes::kScriptSidecarSlotIdInvalid),
        "Binding row requires a non-nil UUID 'slot_id'", location / "slot_id");
      return std::nullopt;
    }

    ScriptingSidecarBindingRow row {};
    row.source_index = index;
    row.node_index = binding.at("node_index").get<uint32_t>();
    row.slot_id = *slot_id;
    row.script_virtual_path
      = binding.at("script_virtual_path").get<std::string>();
    if (binding.contains("execution_order")) {
      row.execution_order = binding.at("execution_order").get<int32_t>();
    }
    ParseRowParams(errors, binding, location, row.params);
    return row;
  }

  //! Reads one `instance_bindings` row the schema accepted, or reports why it
  //! cannot be read.
  auto ParseInstanceRow(ErrorSink& errors, const nlohmann::json& binding,
    const size_t index) -> std::optional<ScriptingSidecarInstanceRow>
  {
    const auto location = JsonPointer {} / "instance_bindings" / index;
    auto path = internal::ParseNodePath(binding.at("node_path"));
    if (!path.has_value()) {
      errors.Add(std::string(import_codes::kScriptSidecarNodePathInvalid),
        "Instance binding row requires a 'node_path' of non-nil node IDs",
        location / "node_path");
      return std::nullopt;
    }
    const auto slot_id
      = internal::ParseNonNilId<data::ScriptSlotId>(binding.at("slot_id"));
    if (!slot_id.has_value()) {
      errors.Add(std::string(import_codes::kScriptSidecarSlotIdInvalid),
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
    ParseRowParams(errors, binding, location, row.params);
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
        errors.Add(
          std::string(import_codes::kScriptSidecarDuplicateSlotConflict),
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
        errors.Add(
          std::string(import_codes::kScriptSidecarDuplicateSlotConflict),
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
    errors.Add(std::string(import_codes::kScriptSidecarParseFailed),
      "Failed to parse sidecar document as JSON: " + std::string(ex.what()));
    return std::nullopt;
  }

  // The schema checks the object form, so its pointers are `/bindings/<i>`
  // for a bare array too.
  const auto doc = NormalizeDocument(std::move(parsed));
  if (!ValidateSchema(errors, doc)) {
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
