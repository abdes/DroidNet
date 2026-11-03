//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/InputSource.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::import {

namespace {

  using data::pak::input::InputActionValueType;
  using data::pak::input::InputTriggerBehavior;
  using data::pak::input::InputTriggerType;
  using nlohmann::json;

  //! The reading of one document: where it is, what it names its products
  //! under, what it has declared and what it has reported.
  struct Reader final {
    std::string source_path;
    std::string_view container;
    // NOLINTNEXTLINE(*-avoid-const-or-ref-data-members)
    std::vector<ImportDiagnostic>& diagnostics;
    InputSourceDocument document;

    auto Report(const ImportSeverity severity, std::string code,
      std::string message, std::string json_pointer = {}) -> void
    {
      diagnostics.push_back(ImportDiagnostic {
        .severity = severity,
        .code = std::move(code),
        .message = std::move(message),
        .source_path = source_path,
        .json_pointer = std::move(json_pointer),
      });
    }

    auto Error(std::string code, std::string message,
      std::string json_pointer = {}) -> void
    {
      Report(ImportSeverity::kError, std::move(code), std::move(message),
        std::move(json_pointer));
    }

    //! The virtual path of the product `name` of the source.
    [[nodiscard]] auto ProductVirtualPath(const std::string_view name,
      const std::string_view extension) const -> std::string
    {
      return std::string(container) + "/" + std::string(name)
        + std::string(extension);
    }

    //! Refuses a product name that gives a virtual path the engine refuses.
    [[nodiscard]] auto RefuseProductName(
      const std::string_view virtual_path, std::string json_pointer) -> bool
    {
      auto refused = internal::CheckAssetVirtualPath(
        source_path, virtual_path, std::move(json_pointer));
      if (!refused.has_value()) {
        return false;
      }
      diagnostics.push_back(std::move(*refused));
      return true;
    }
  };

  auto InputSchemaValidator() -> nlohmann::json_schema::json_validator&
  {
    thread_local auto validator
      = internal::MakeJsonSchemaValidator(json::parse(kInputSchema));
    return validator;
  }

  auto ValidateInputSchema(const json& doc, Reader& reader) -> bool
  {
    const auto config = internal::JsonSchemaValidationDiagnosticConfig {
      .validation_failed_code = "input.schema.validation_failed",
      .validation_failed_prefix
      = "Schema validation failed (oxygen.input.schema.json): ",
      .validator_failure_code = "input.schema.validator_failure",
      .validator_failure_prefix = "Input schema validator failure: ",
    };
    return internal::ValidateJsonSchemaWithDiagnostics(InputSchemaValidator(),
      doc, config,
      [&](const std::string_view code, std::string message,
        std::string json_pointer) -> void {
        reader.Error(
          std::string(code), std::move(message), std::move(json_pointer));
      });
  }

  [[nodiscard]] auto SecondsToNanoseconds(const float seconds, uint64_t& out)
    -> bool
  {
    if (!std::isfinite(seconds) || seconds < 0.0F) {
      return false;
    }
    constexpr long double kNsPerSecond = 1000000000.0L;
    const auto raw = static_cast<long double>(seconds) * kNsPerSecond;
    if (raw > static_cast<long double>(std::numeric_limits<uint64_t>::max())) {
      return false;
    }
    out = static_cast<uint64_t>(std::llround(raw));
    return true;
  }

  [[nodiscard]] auto ParseActionType(const std::string_view value)
    -> std::optional<InputActionValueType>
  {
    if (value == "bool") {
      return InputActionValueType::kBool;
    }
    if (value == "axis1d") {
      return InputActionValueType::kAxis1D;
    }
    if (value == "axis2d") {
      return InputActionValueType::kAxis2D;
    }
    return std::nullopt;
  }

  [[nodiscard]] auto ParseTriggerType(const std::string_view value)
    -> std::optional<InputTriggerType>
  {
    if (value == "pressed") {
      return InputTriggerType::kPressed;
    }
    if (value == "released") {
      return InputTriggerType::kReleased;
    }
    if (value == "down") {
      return InputTriggerType::kDown;
    }
    if (value == "hold") {
      return InputTriggerType::kHold;
    }
    if (value == "hold_and_release") {
      return InputTriggerType::kHoldAndRelease;
    }
    if (value == "pulse") {
      return InputTriggerType::kPulse;
    }
    if (value == "tap") {
      return InputTriggerType::kTap;
    }
    if (value == "chord") {
      return InputTriggerType::kChord;
    }
    if (value == "action_chain") {
      return InputTriggerType::kActionChain;
    }
    if (value == "combo") {
      return InputTriggerType::kCombo;
    }
    return std::nullopt;
  }

  [[nodiscard]] auto ParseTriggerBehavior(const std::string_view value)
    -> std::optional<InputTriggerBehavior>
  {
    if (value == "explicit") {
      return InputTriggerBehavior::kExplicit;
    }
    if (value == "implicit") {
      return InputTriggerBehavior::kImplicit;
    }
    if (value == "blocker") {
      return InputTriggerBehavior::kBlocker;
    }
    return std::nullopt;
  }

  auto ReadAction(
    const json& node, const json::json_pointer& path, Reader& reader) -> bool
  {
    auto action = ActionSource {};
    action.name = node.at("name").get<std::string>();
    action.virtual_path = reader.ProductVirtualPath(
      action.name, data::kInputActionDescriptorExtension);
    if (reader.RefuseProductName(
          action.virtual_path, (path / "name").to_string())) {
      return false;
    }
    const auto type = node.at("type").get<std::string>();
    const auto parsed_type = ParseActionType(type);
    if (!parsed_type.has_value()) {
      reader.Error("input.schema.contract_mismatch",
        "Unsupported action type '" + type + "'", (path / "type").to_string());
      return false;
    }
    action.value_type = *parsed_type;
    action.consumes_input = node.value("consumes_input", false);
    const auto accumulation
      = node.value("accumulation", std::string { "highest_absolute" });
    if (accumulation != "highest_absolute" && accumulation != "cumulative") {
      reader.Error("input.schema.contract_mismatch",
        "Unsupported action accumulation '" + accumulation + "'",
        (path / "accumulation").to_string());
      return false;
    }
    action.cumulative = accumulation == "cumulative";

    if (reader.document.local_action_keys.contains(action.name)) {
      reader.Error("input.asset.action_conflict",
        "Duplicate action declaration '" + action.name + "'",
        (path / "name").to_string());
      return false;
    }

    action.key = data::AssetKey::FromVirtualPath(action.virtual_path);
    reader.document.local_action_keys.emplace(action.name, action.key);
    reader.document.local_action_keys.emplace(action.virtual_path, action.key);
    reader.document.actions.push_back(std::move(action));
    return true;
  }

  auto ReadTriggerAuxArray(const json& node, const json::json_pointer& path,
    Reader& reader, std::vector<TriggerAuxSource>& trigger_aux) -> bool
  {
    auto ok = true;
    for (size_t i = 0; i < node.size(); ++i) {
      const auto& entry = node[i];
      const auto entry_path = path / i;
      auto aux = TriggerAuxSource {};
      aux.action_name = entry.at("action").get<std::string>();
      aux.json_pointer = entry_path.to_string();

      if (entry.contains("completion_states")) {
        aux.completion_states = entry.at("completion_states").get<uint32_t>();
      }

      if (entry.contains("time_to_complete")) {
        const auto seconds = entry.at("time_to_complete").get<float>();
        if (!SecondsToNanoseconds(seconds, aux.time_to_complete_ns)) {
          reader.Error("input.context.trigger_invalid",
            "'time_to_complete' exceeds supported range",
            (entry_path / "time_to_complete").to_string());
          ok = false;
          continue;
        }
      }
      trigger_aux.push_back(std::move(aux));
    }
    return ok;
  }

  auto ReadTriggerKind(const json& node, const json::json_pointer& path,
    Reader& reader, TriggerSource& trigger) -> bool
  {
    const auto type = node.at("type").get<std::string>();
    const auto parsed_type = ParseTriggerType(type);
    if (!parsed_type.has_value()) {
      reader.Error("input.schema.contract_mismatch",
        "Unsupported trigger type '" + type + "'", (path / "type").to_string());
      return false;
    }
    trigger.type = *parsed_type;

    if (node.contains("behavior")) {
      const auto behavior = node.at("behavior").get<std::string>();
      const auto parsed_behavior = ParseTriggerBehavior(behavior);
      if (!parsed_behavior.has_value()) {
        reader.Error("input.schema.contract_mismatch",
          "Unsupported trigger behavior '" + behavior + "'",
          (path / "behavior").to_string());
        return false;
      }
      trigger.behavior = *parsed_behavior;
    }
    return true;
  }

  auto ReadTrigger(const json& node, const json::json_pointer& path,
    Reader& reader, TriggerSource& trigger) -> bool
  {
    trigger.json_pointer = path.to_string();
    if (!ReadTriggerKind(node, path, reader, trigger)) {
      return false;
    }

    if (node.contains("actuation_threshold")) {
      trigger.actuation_threshold = node.at("actuation_threshold").get<float>();
    }
    if (node.contains("hold_time")) {
      trigger.has_hold_time = true;
      trigger.hold_time = node.at("hold_time").get<float>();
    }
    if (node.contains("interval")) {
      trigger.has_interval = true;
      trigger.interval = node.at("interval").get<float>();
    }
    if (node.contains("chord_action")) {
      trigger.linked_action_name = node.at("chord_action").get<std::string>();
    }
    if (node.contains("combo_actions")
      && !ReadTriggerAuxArray(
        node["combo_actions"], path / "combo_actions", reader, trigger.aux)) {
      return false;
    }
    if (node.contains("aux")
      && !ReadTriggerAuxArray(node["aux"], path / "aux", reader, trigger.aux)) {
      return false;
    }

    if (trigger.type != InputTriggerType::kCombo && !trigger.aux.empty()) {
      reader.Report(ImportSeverity::kWarning,
        "input.context.trigger_aux_ignored",
        "Aux records are ignored for non-combo triggers", path.to_string());
      trigger.aux.clear();
    }
    return true;
  }

  auto ReadMapping(const json& node, const json::json_pointer& path,
    Reader& reader, MappingSource& mapping) -> bool
  {
    mapping.json_pointer = path.to_string();
    mapping.action_name = node.at("action").get<std::string>();
    // The schema admits only the slot names the platform registers.
    mapping.slot_name = node.at("slot").get<std::string>();

    if (node.contains("scale")) {
      mapping.scale[0] = node["scale"][0].get<float>();
      mapping.scale[1] = node["scale"][1].get<float>();
    }
    if (node.contains("bias")) {
      mapping.bias[0] = node["bias"][0].get<float>();
      mapping.bias[1] = node["bias"][1].get<float>();
    }

    if (node.contains("trigger")) {
      const auto shorthand = node.at("trigger").get<std::string>();
      const auto type = ParseTriggerType(shorthand);
      if (!type.has_value()) {
        reader.Error("input.schema.contract_mismatch",
          "Unknown trigger shorthand '" + shorthand + "'",
          (path / "trigger").to_string());
        return false;
      }
      mapping.triggers.push_back(TriggerSource {
        .type = *type,
        .behavior = InputTriggerBehavior::kImplicit,
        .actuation_threshold = 0.5F,
        .json_pointer = (path / "trigger").to_string(),
      });
    } else if (node.contains("triggers")) {
      for (size_t i = 0; i < node["triggers"].size(); ++i) {
        auto trigger = TriggerSource {};
        if (!ReadTrigger(
              node["triggers"][i], path / "triggers" / i, reader, trigger)) {
          return false;
        }
        mapping.triggers.push_back(std::move(trigger));
      }
    }
    return true;
  }

  auto ReadContext(const json& node, const json::json_pointer& path,
    Reader& reader, ContextSource& context) -> bool
  {
    context.name = node.at("name").get<std::string>();
    context.json_pointer = path.to_string();
    if (reader.RefuseProductName(
          reader.ProductVirtualPath(
            context.name, data::kInputMappingContextDescriptorExtension),
          (path / "name").to_string())) {
      return false;
    }
    if (node.contains("priority")) {
      context.default_priority = node.at("priority").get<int32_t>();
    }
    const auto& mappings = node.at("mappings");
    for (size_t i = 0; i < mappings.size(); ++i) {
      auto mapping = MappingSource {};
      if (!ReadMapping(mappings[i], path / "mappings" / i, reader, mapping)) {
        return false;
      }
      context.mappings.push_back(std::move(mapping));
    }
    return true;
  }

  auto ReadDeclarations(const json& doc, Reader& reader) -> bool
  {
    auto ok = true;
    if (doc.contains("actions")) {
      for (size_t i = 0; i < doc["actions"].size(); ++i) {
        const auto action_path = json::json_pointer {} / "actions" / i;
        ok = ReadAction(doc["actions"][i], action_path, reader) && ok;
      }
    }

    auto context_names = std::unordered_set<std::string> {};
    const auto contexts = doc.value("contexts", json::array());
    for (size_t i = 0; i < contexts.size(); ++i) {
      auto context = ContextSource {};
      const auto context_path = json::json_pointer {} / "contexts" / i;
      if (!ReadContext(contexts[i], context_path, reader, context)) {
        ok = false;
        continue;
      }
      if (!context_names.insert(context.name).second) {
        reader.Error("input.context.name_duplicate",
          "Duplicate context name '" + context.name + "'",
          (context_path / "name").to_string());
        ok = false;
        continue;
      }
      reader.document.contexts.push_back(std::move(context));
    }
    return ok;
  }

  //! Checks a name that is not a reference against the declared actions.
  auto CheckLocalAction(const std::string_view name,
    const std::string_view json_pointer, Reader& reader) -> bool
  {
    if (name.ends_with(data::kInputActionDescriptorExtension)
      || reader.document.local_action_keys.contains(std::string(name))) {
      return true;
    }
    reader.Error("input.context.action_unresolved",
      "No action of this source is named '" + std::string(name) + "'",
      std::string(json_pointer));
    return false;
  }

  auto CheckLocalActions(Reader& reader) -> bool
  {
    auto ok = true;
    for (const auto& context : reader.document.contexts) {
      for (const auto& mapping : context.mappings) {
        ok = CheckLocalAction(
               mapping.action_name, mapping.json_pointer + "/action", reader)
          && ok;
        for (const auto& trigger : mapping.triggers) {
          if (trigger.linked_action_name.has_value()) {
            ok = CheckLocalAction(*trigger.linked_action_name,
                   trigger.json_pointer + "/chord_action", reader)
              && ok;
          }
          for (const auto& aux : trigger.aux) {
            ok = CheckLocalAction(
                   aux.action_name, aux.json_pointer + "/action", reader)
              && ok;
          }
        }
      }
    }
    return ok;
  }

  auto ReadDocument(const json& doc, Reader& reader) -> bool
  {
    try {
      return ReadDeclarations(doc, reader) && CheckLocalActions(reader);
    } catch (const std::exception& ex) {
      reader.Error("input.schema.contract_mismatch",
        std::string(
          "Schema validated input document failed invariant extraction: ")
          + ex.what());
      return false;
    }
  }

} // namespace

auto PrepareInputDocument(const std::string_view bytes,
  const std::filesystem::path& source_path,
  const std::string_view container_virtual_path,
  std::vector<ImportDiagnostic>& diagnostics)
  -> std::optional<InputSourceDocument>
{
  auto reader = Reader {
    .source_path = source_path.string(),
    .container = container_virtual_path,
    .diagnostics = diagnostics,
    .document = {},
  };

  auto doc = json {};
  try {
    doc = json::parse(bytes);
  } catch (const json::exception& ex) {
    reader.Error("input.import.parse_failed",
      std::string("Failed to parse input JSON: ") + ex.what());
    return std::nullopt;
  }

  if (!ValidateInputSchema(doc, reader)) {
    return std::nullopt;
  }
  if (!ReadDocument(doc, reader)) {
    return std::nullopt;
  }
  return std::move(reader.document);
}

} // namespace oxygen::content::import
