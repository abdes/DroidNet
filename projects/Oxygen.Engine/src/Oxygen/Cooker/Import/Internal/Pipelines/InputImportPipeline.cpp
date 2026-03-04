//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Cooker/Import/Internal/Pipelines/InputImportPipeline.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Utils/ContentHashUtils.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Cooker/Import/Internal/Utils/StringUtils.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import {

namespace {

  using data::AssetType;
  using data::pak::input::InputActionAssetDesc;
  using data::pak::input::InputActionAssetFlags;
  using data::pak::input::InputActionMappingRecord;
  using data::pak::input::InputMappingContextAssetDesc;
  using data::pak::input::InputMappingContextFlags;
  using data::pak::input::InputTriggerAuxRecord;
  using data::pak::input::InputTriggerBehavior;
  using data::pak::input::InputTriggerRecord;
  using data::pak::input::InputTriggerType;
  using nlohmann::json;

  struct DeclaredAction final {
    std::string name;
    uint8_t value_type = 0;
    bool consumes_input = false;
    data::AssetKey key {};
    std::string virtual_path;
    std::string descriptor_relpath;
  };

  struct TriggerAuxSource final {
    std::string action_name;
    uint32_t completion_states = 0;
    uint64_t time_to_complete_ns = 0;
  };

  struct TriggerSource final {
    InputTriggerType type = InputTriggerType::kPressed;
    InputTriggerBehavior behavior = InputTriggerBehavior::kImplicit;
    float actuation_threshold = 0.5F;
    float hold_time = 0.0F;
    bool has_hold_time = false;
    float interval = 0.0F;
    bool has_interval = false;
    std::optional<std::string> linked_action_name;
    std::vector<TriggerAuxSource> aux;
  };

  struct MappingSource final {
    std::string action_name;
    std::string slot_name;
    std::array<float, 2> scale { 1.0F, 1.0F };
    std::array<float, 2> bias { 0.0F, 0.0F };
    std::vector<TriggerSource> triggers;
  };

  struct ContextSource final {
    std::string name;
    bool auto_load = false;
    bool auto_activate = false;
    int32_t default_priority = 0;
    std::vector<MappingSource> mappings;
  };

  struct BuiltContextAsset final {
    std::string name;
    data::AssetKey key {};
    std::string virtual_path;
    std::string descriptor_relpath;
    InputMappingContextFlags flags = InputMappingContextFlags::kNone;
    int32_t default_priority = 0;
    std::vector<InputActionMappingRecord> mappings;
    std::vector<InputTriggerRecord> triggers;
    std::vector<InputTriggerAuxRecord> trigger_aux;
    std::vector<char> strings;
  };

  [[nodiscard]] auto IsStopRequested(const std::stop_token& token) noexcept
    -> bool
  {
    return token.stop_possible() && token.stop_requested();
  }

  auto AddDiagnostic(ImportSession& session, const ImportRequest& request,
    const ImportSeverity severity, std::string code, std::string message,
    std::string object_path = {}) -> void
  {
    session.AddDiagnostic({
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = request.source_path.string(),
      .object_path = std::move(object_path),
    });
  }

  constexpr size_t kMaxSchemaDiagnostics = 12;

  enum class InputDocumentKind : uint8_t {
    kPrimary = 0,
    kStandaloneAction,
  };

  auto PrimaryInputSchemaValidator() -> nlohmann::json_schema::json_validator&
  {
    thread_local auto validator = [] {
      auto out = nlohmann::json_schema::json_validator {};
      out.set_root_schema(json::parse(kInputSchema));
      return out;
    }();
    return validator;
  }

  auto StandaloneInputActionSchemaValidator()
    -> nlohmann::json_schema::json_validator&
  {
    thread_local auto validator = [] {
      auto out = nlohmann::json_schema::json_validator {};
      out.set_root_schema(json::parse(kInputActionSchema));
      return out;
    }();
    return validator;
  }

  auto ValidateInputSchema(const json& doc, ImportSession& session,
    const ImportRequest& request) -> std::optional<InputDocumentKind>
  {
    auto primary_issues = std::vector<internal::JsonSchemaIssue> {};
    auto internal_error = std::string {};
    if (!internal::CollectJsonSchemaIssues(
          PrimaryInputSchemaValidator(), doc, primary_issues, internal_error)) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "input.schema.validator_failure",
        "Input primary schema validator failure: " + internal_error);
      return std::nullopt;
    }
    if (primary_issues.empty()) {
      return InputDocumentKind::kPrimary;
    }

    auto action_issues = std::vector<internal::JsonSchemaIssue> {};
    if (!internal::CollectJsonSchemaIssues(
          StandaloneInputActionSchemaValidator(), doc, action_issues,
          internal_error)) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "input.schema.validator_failure",
        "Input action schema validator failure: " + internal_error);
      return std::nullopt;
    }
    if (action_issues.empty()) {
      return InputDocumentKind::kStandaloneAction;
    }

    const bool likely_primary = doc.is_object() && doc.contains("contexts");
    if (likely_primary) {
      internal::EmitCollectedJsonSchemaIssues(primary_issues,
        "input.schema.validation_failed",
        "Schema validation failed (oxygen.input.schema.json): ",
        "Schema validation (oxygen.input.schema.json) reported ",
        kMaxSchemaDiagnostics,
        [&](const std::string_view code, std::string message,
          std::string object_path) {
          AddDiagnostic(session, request, ImportSeverity::kError,
            std::string(code), std::move(message), std::move(object_path));
        });
    } else {
      internal::EmitCollectedJsonSchemaIssues(action_issues,
        "input.schema.validation_failed",
        "Schema validation failed (oxygen.input-action.schema.json): ",
        "Schema validation (oxygen.input-action.schema.json) reported ",
        kMaxSchemaDiagnostics,
        [&](const std::string_view code, std::string message,
          std::string object_path) {
          AddDiagnostic(session, request, ImportSeverity::kError,
            std::string(code), std::move(message), std::move(object_path));
        });
    }
    return std::nullopt;
  }

  [[nodiscard]] auto SecondsToNanoseconds(const float seconds, uint64_t& out)
    -> bool
  {
    if (!std::isfinite(seconds) || seconds < 0.0F) {
      return false;
    }
    constexpr long double kNsPerSecond = 1000000000.0L;
    const auto raw = static_cast<long double>(seconds) * kNsPerSecond;
    if (raw
      > static_cast<long double>((std::numeric_limits<uint64_t>::max)())) {
      return false;
    }
    out = static_cast<uint64_t>(std::llround(raw));
    return true;
  }

  [[nodiscard]] auto ParseActionType(std::string_view value)
    -> std::optional<uint8_t>
  {
    if (value == "bool") {
      return static_cast<uint8_t>(input::ActionValueType::kBool);
    }
    if (value == "axis1d") {
      return static_cast<uint8_t>(input::ActionValueType::kAxis1D);
    }
    if (value == "axis2d") {
      return static_cast<uint8_t>(input::ActionValueType::kAxis2D);
    }
    return std::nullopt;
  }

  [[nodiscard]] auto ParseTriggerType(std::string_view value)
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

  [[nodiscard]] auto ParseTriggerBehavior(std::string_view value)
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

  [[nodiscard]] auto MakeAssetKey(const ImportRequest& request,
    const std::string_view virtual_path) -> data::AssetKey
  {
    static_cast<void>(request);
    return oxygen::data::AssetKey::FromVirtualPath(virtual_path);
  }

  [[nodiscard]] auto CanonicalizeInputSlotName(
    const std::string_view authored_name) -> std::string_view
  {
    static constexpr std::array<std::pair<std::string_view, std::string_view>,
      17>
      kAliasTable {
        std::pair { "UpArrow", "Up" },
        std::pair { "DownArrow", "Down" },
        std::pair { "RightArrow", "Right" },
        std::pair { "LeftArrow", "Left" },
        std::pair { "Alpha0", "0" },
        std::pair { "Alpha1", "1" },
        std::pair { "Alpha2", "2" },
        std::pair { "Alpha3", "3" },
        std::pair { "Alpha4", "4" },
        std::pair { "Alpha5", "5" },
        std::pair { "Alpha6", "6" },
        std::pair { "Alpha7", "7" },
        std::pair { "Alpha8", "8" },
        std::pair { "Alpha9", "9" },
        std::pair { "Exclaim", "!" },
        std::pair { "RightControl", "RightCtrl" },
        std::pair { "LeftControl", "LeftCtrl" },
      };
    for (const auto& [alias_name, canonical_name] : kAliasTable) {
      if (authored_name == alias_name) {
        return canonical_name;
      }
    }
    if (authored_name == "Print") {
      return "PrintScreen";
    }
    return authored_name;
  }

  auto BuildKnownSlotNames() -> std::unordered_set<std::string>
  {
    static constexpr std::array<std::string_view, 145> kKnownSlotNames {
      "MouseWheelUp",
      "MouseWheelDown",
      "MouseWheelLeft",
      "MouseWheelRight",
      "MouseWheelX",
      "MouseWheelY",
      "MouseWheelXY",
      "LeftMouseButton",
      "RightMouseButton",
      "MiddleMouseButton",
      "ThumbMouseButton1",
      "ThumbMouseButton2",
      "MouseX",
      "MouseY",
      "MouseXY",
      "None",
      "AnyKey",
      "BackSpace",
      "Delete",
      "Tab",
      "Clear",
      "Return",
      "Pause",
      "Escape",
      "Space",
      "Keypad0",
      "Keypad1",
      "Keypad2",
      "Keypad3",
      "Keypad4",
      "Keypad5",
      "Keypad6",
      "Keypad7",
      "Keypad8",
      "Keypad9",
      "KeypadPeriod",
      "KeypadDivide",
      "KeypadMultiply",
      "KeypadMinus",
      "KeypadPlus",
      "KeypadEnter",
      "KeypadEquals",
      "Up",
      "Down",
      "Right",
      "Left",
      "Insert",
      "Home",
      "End",
      "PageUp",
      "PageDown",
      "F1",
      "F2",
      "F3",
      "F4",
      "F5",
      "F6",
      "F7",
      "F8",
      "F9",
      "F10",
      "F11",
      "F12",
      "F13",
      "F14",
      "F15",
      "0",
      "1",
      "2",
      "3",
      "4",
      "5",
      "6",
      "7",
      "8",
      "9",
      "!",
      "DoubleQuote",
      "Hash",
      "Dollar",
      "Percent",
      "Ampersand",
      "Quote",
      "LeftParen",
      "RightParen",
      "Asterisk",
      "Plus",
      "Comma",
      "Minus",
      "Period",
      "Slash",
      "Colon",
      "Semicolon",
      "Less",
      "Equals",
      "Greater",
      "Question",
      "At",
      "LeftBracket",
      "Backslash",
      "RightBracket",
      "Caret",
      "Underscore",
      "BackQuote",
      "A",
      "B",
      "C",
      "D",
      "E",
      "F",
      "G",
      "H",
      "I",
      "J",
      "K",
      "L",
      "M",
      "N",
      "O",
      "P",
      "Q",
      "R",
      "S",
      "T",
      "U",
      "V",
      "W",
      "X",
      "Y",
      "Z",
      "NumLock",
      "CapsLock",
      "ScrollLock",
      "RightShift",
      "LeftShift",
      "RightCtrl",
      "LeftCtrl",
      "RightAlt",
      "LeftAlt",
      "LeftMeta",
      "RightMeta",
      "Help",
      "PrintScreen",
      "SysReq",
      "Menu",
    };
    auto known = std::unordered_set<std::string> {};
    known.reserve(kKnownSlotNames.size());
    for (const auto slot_name : kKnownSlotNames) {
      known.emplace(slot_name);
    }
    return known;
  }

  auto TryReadInputActionDescriptor(const std::filesystem::path& path)
    -> std::optional<InputActionAssetDesc>
  {
    auto in = std::ifstream(path, std::ios::binary);
    if (!in) {
      return std::nullopt;
    }
    auto desc = InputActionAssetDesc {};
    in.read(reinterpret_cast<char*>(&desc),
      static_cast<std::streamsize>(sizeof(desc)));
    if (in.gcount() != static_cast<std::streamsize>(sizeof(desc))) {
      return std::nullopt;
    }
    return desc;
  }

  [[nodiscard]] auto ExtractName(std::span<const char> chars) -> std::string
  {
    const auto it = std::find(chars.begin(), chars.end(), '\0');
    return std::string(chars.begin(), it);
  }

  auto BuildMountedActions(ImportSession& session, const ImportRequest& request)
    -> std::unordered_map<std::string, std::pair<data::AssetKey, uint8_t>>
  {
    auto mounted
      = std::unordered_map<std::string, std::pair<data::AssetKey, uint8_t>> {};
    auto roots = std::vector<std::filesystem::path> {};
    roots.reserve(1U + request.cooked_context_roots.size());
    roots.push_back(session.CookedRoot());
    roots.insert(roots.end(), request.cooked_context_roots.begin(),
      request.cooked_context_roots.end());

    auto visited = std::unordered_set<std::string> {};
    for (const auto& root : roots) {
      const auto normalized = root.lexically_normal();
      if (!visited.insert(normalized.string()).second) {
        continue;
      }
      std::error_code ec;
      if (!std::filesystem::exists(normalized / "container.index.bin", ec)
        || ec) {
        continue;
      }

      auto inspection = lc::Inspection {};
      try {
        inspection.LoadFromRoot(normalized);
      } catch (const std::exception& ex) {
        AddDiagnostic(session, request, ImportSeverity::kWarning,
          "input.import.context_index_unreadable",
          "Failed to load mounted index '" + normalized.string()
            + "': " + ex.what());
        continue;
      }

      for (const auto& entry : inspection.Assets()) {
        if (entry.asset_type != static_cast<uint8_t>(AssetType::kInputAction)) {
          continue;
        }
        const auto descriptor_path
          = normalized / std::filesystem::path(entry.descriptor_relpath);
        const auto desc = TryReadInputActionDescriptor(descriptor_path);
        if (!desc.has_value()) {
          continue;
        }
        const auto action_name = ExtractName(
          std::span<const char>(desc->header.name, sizeof(desc->header.name)));
        if (action_name.empty()) {
          continue;
        }
        mounted.insert_or_assign(
          action_name, std::pair { entry.key, desc->value_type });
      }
    }
    return mounted;
  }

  auto PatchContentHash(std::vector<std::byte>& bytes, const uint64_t hash)
    -> void
  {
    constexpr auto kOffset
      = offsetof(data::pak::core::AssetHeader, content_hash);
    if (bytes.size() < kOffset + sizeof(hash)) {
      return;
    }
    std::memcpy(bytes.data() + kOffset, &hash, sizeof(hash));
  }

  auto SerializeAction(const DeclaredAction& action, const bool hashing)
    -> std::optional<std::vector<std::byte>>
  {
    auto desc = InputActionAssetDesc {};
    desc.header.asset_type = static_cast<uint8_t>(AssetType::kInputAction);
    desc.header.version = data::pak::input::kInputActionAssetVersion;
    util::TruncateAndNullTerminate(
      desc.header.name, sizeof(desc.header.name), action.name);
    desc.value_type = action.value_type;
    desc.flags = action.consumes_input ? InputActionAssetFlags::kConsumesInput
                                       : InputActionAssetFlags::kNone;

    serio::MemoryStream stream;
    serio::Writer writer(stream);
    auto packed = writer.ScopedAlignment(1);
    if (auto result = writer.WriteBlob(
          std::as_bytes(std::span<const InputActionAssetDesc, 1>(&desc, 1)));
      !result.has_value()) {
      return std::nullopt;
    }

    const auto data = stream.Data();
    auto bytes = std::vector<std::byte>(data.begin(), data.end());
    if (hashing) {
      PatchContentHash(bytes, util::ComputeContentHash(bytes));
    }
    return bytes;
  }

  auto SerializeContext(const BuiltContextAsset& context, const bool hashing,
    std::string& error) -> std::optional<std::vector<std::byte>>
  {
    if (context.mappings.size() > (std::numeric_limits<uint32_t>::max)()
      || context.triggers.size() > (std::numeric_limits<uint32_t>::max)()
      || context.trigger_aux.size() > (std::numeric_limits<uint32_t>::max)()
      || context.strings.size() > (std::numeric_limits<uint32_t>::max)()) {
      error = "context record table exceeds uint32 limits";
      return std::nullopt;
    }

    auto desc = InputMappingContextAssetDesc {};
    desc.header.asset_type
      = static_cast<uint8_t>(AssetType::kInputMappingContext);
    desc.header.version = data::pak::input::kInputMappingContextAssetVersion;
    util::TruncateAndNullTerminate(
      desc.header.name, sizeof(desc.header.name), context.name);
    desc.flags = context.flags;
    desc.default_priority = context.default_priority;

    desc.mappings.offset = sizeof(InputMappingContextAssetDesc);
    desc.mappings.count = static_cast<uint32_t>(context.mappings.size());
    desc.mappings.entry_size = sizeof(InputActionMappingRecord);
    desc.triggers.offset = desc.mappings.offset
      + context.mappings.size() * sizeof(InputActionMappingRecord);
    desc.triggers.count = static_cast<uint32_t>(context.triggers.size());
    desc.triggers.entry_size = sizeof(InputTriggerRecord);
    desc.trigger_aux.offset = desc.triggers.offset
      + context.triggers.size() * sizeof(InputTriggerRecord);
    desc.trigger_aux.count = static_cast<uint32_t>(context.trigger_aux.size());
    desc.trigger_aux.entry_size = sizeof(InputTriggerAuxRecord);
    desc.strings.offset = desc.trigger_aux.offset
      + context.trigger_aux.size() * sizeof(InputTriggerAuxRecord);
    desc.strings.count = static_cast<uint32_t>(context.strings.size());
    desc.strings.entry_size = sizeof(char);

    serio::MemoryStream stream;
    serio::Writer writer(stream);
    auto packed = writer.ScopedAlignment(1);
    if (!writer
          .WriteBlob(std::as_bytes(
            std::span<const InputMappingContextAssetDesc, 1>(&desc, 1)))
          .has_value()
      || !writer.WriteBlob(std::as_bytes(std::span(context.mappings)))
        .has_value()
      || !writer.WriteBlob(std::as_bytes(std::span(context.triggers)))
        .has_value()
      || !writer.WriteBlob(std::as_bytes(std::span(context.trigger_aux)))
        .has_value()
      || !writer.WriteBlob(std::as_bytes(std::span(context.strings)))
        .has_value()) {
      error = "failed writing context descriptor payload";
      return std::nullopt;
    }

    const auto data = stream.Data();
    auto bytes = std::vector<std::byte>(data.begin(), data.end());
    if (hashing) {
      PatchContentHash(bytes, util::ComputeContentHash(bytes));
    }
    return bytes;
  }

  struct ParsedInputSource final {
    std::vector<DeclaredAction> declared_actions;
    std::unordered_map<std::string, data::AssetKey> local_action_keys;
    std::vector<ContextSource> context_sources;
  };

  auto ParseInputSourceDocument(const json& doc, const bool is_standalone,
    ImportSession& session, const ImportRequest& request,
    const std::unordered_set<std::string>& known_slots,
    const std::unordered_map<std::string, std::pair<data::AssetKey, uint8_t>>&
      mounted_actions,
    ParsedInputSource& out) -> bool
  {
    out.declared_actions.clear();
    out.local_action_keys.clear();
    out.context_sources.clear();

    auto local_action_types = std::unordered_map<std::string, uint8_t> {};

    const auto parse_action = [&](const json& node, const std::string& path) {
      auto action = DeclaredAction {};
      action.name = node.at("name").get<std::string>();
      const auto parsed_type
        = ParseActionType(node.at("type").get<std::string>());
      if (!parsed_type.has_value()) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "input.schema.contract_mismatch",
          "Unsupported action type '" + node.at("type").get<std::string>()
            + "'",
          path + ".type");
        return false;
      }
      action.value_type = *parsed_type;
      action.consumes_input = node.value("consumes_input", false);

      if (local_action_types.contains(action.name)) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "input.asset.action_conflict",
          "Duplicate action declaration '" + action.name + "'", path + ".name");
        return false;
      }

      if (const auto it = mounted_actions.find(action.name);
        it != mounted_actions.end() && it->second.second != action.value_type) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "input.asset.action_conflict",
          "Action '" + action.name + "' conflicts with mounted action type",
          path + ".type");
        return false;
      }

      action.descriptor_relpath
        = request.loose_cooked_layout.InputActionDescriptorRelPath(action.name);
      action.virtual_path
        = request.loose_cooked_layout.InputActionVirtualPath(action.name);
      action.key = MakeAssetKey(request, action.virtual_path);
      local_action_types.emplace(action.name, action.value_type);
      out.local_action_keys.emplace(action.name, action.key);
      out.declared_actions.push_back(std::move(action));
      return true;
    };

    const auto parse_trigger_aux_array
      = [&](const json& node, const std::string& path,
          std::vector<TriggerAuxSource>& trigger_aux) {
          bool ok = true;
          for (size_t i = 0; i < node.size(); ++i) {
            const auto& entry = node[i];
            const auto entry_path = path + "[" + std::to_string(i) + "]";
            bool entry_ok = true;
            auto aux = TriggerAuxSource {};
            aux.action_name = entry.at("action").get<std::string>();

            if (entry.contains("completion_states")) {
              aux.completion_states
                = entry.at("completion_states").get<uint32_t>();
            }

            if (entry.contains("time_to_complete")) {
              const auto seconds = entry.at("time_to_complete").get<float>();
              if (!SecondsToNanoseconds(seconds, aux.time_to_complete_ns)) {
                AddDiagnostic(session, request, ImportSeverity::kError,
                  "input.context.trigger_invalid",
                  "'time_to_complete' exceeds supported range",
                  entry_path + ".time_to_complete");
                ok = false;
                entry_ok = false;
              }
            }

            if (entry_ok) {
              trigger_aux.push_back(std::move(aux));
            }
          }
          return ok;
        };

    const auto parse_trigger = [&](const json& node, const std::string& path,
                                 TriggerSource& out_trigger) {
      const auto parsed_type
        = ParseTriggerType(node.at("type").get<std::string>());
      if (!parsed_type.has_value()) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "input.schema.contract_mismatch",
          "Unsupported trigger type '" + node.at("type").get<std::string>()
            + "'",
          path + ".type");
        return false;
      }
      out_trigger.type = *parsed_type;

      if (node.contains("behavior")) {
        const auto parsed_behavior
          = ParseTriggerBehavior(node.at("behavior").get<std::string>());
        if (!parsed_behavior.has_value()) {
          AddDiagnostic(session, request, ImportSeverity::kError,
            "input.schema.contract_mismatch",
            "Unsupported trigger behavior '"
              + node.at("behavior").get<std::string>() + "'",
            path + ".behavior");
          return false;
        }
        out_trigger.behavior = *parsed_behavior;
      }

      if (node.contains("actuation_threshold")) {
        out_trigger.actuation_threshold
          = node.at("actuation_threshold").get<float>();
      }
      if (node.contains("hold_time")) {
        out_trigger.has_hold_time = true;
        out_trigger.hold_time = node.at("hold_time").get<float>();
      }
      if (node.contains("interval")) {
        out_trigger.has_interval = true;
        out_trigger.interval = node.at("interval").get<float>();
      }
      if (node.contains("chord_action")) {
        out_trigger.linked_action_name
          = node.at("chord_action").get<std::string>();
      }
      if (node.contains("combo_actions")) {
        if (!parse_trigger_aux_array(node["combo_actions"],
              path + ".combo_actions", out_trigger.aux)) {
          return false;
        }
      }
      if (node.contains("aux")) {
        if (!parse_trigger_aux_array(
              node["aux"], path + ".aux", out_trigger.aux)) {
          return false;
        }
      }
      if (out_trigger.type != InputTriggerType::kCombo
        && !out_trigger.aux.empty()) {
        AddDiagnostic(session, request, ImportSeverity::kWarning,
          "input.context.trigger_aux_ignored",
          "Aux records are ignored for non-combo triggers", path);
        out_trigger.aux.clear();
      }
      return true;
    };

    const auto parse_mapping = [&](const json& node, const std::string& path,
                                 MappingSource& out_mapping) {
      out_mapping.action_name = node.at("action").get<std::string>();
      const auto authored_slot_name = node.at("slot").get<std::string>();
      const auto canonical_slot_name
        = CanonicalizeInputSlotName(authored_slot_name);
      out_mapping.slot_name = std::string(canonical_slot_name);
      if (canonical_slot_name != authored_slot_name) {
        AddDiagnostic(session, request, ImportSeverity::kInfo,
          "input.context.slot_alias_normalized",
          "Normalized slot alias '" + authored_slot_name + "' to '"
            + out_mapping.slot_name + "'",
          path + ".slot");
      }
      if (!known_slots.contains(out_mapping.slot_name)) {
        AddDiagnostic(session, request, ImportSeverity::kWarning,
          "input.context.slot_unknown",
          "Unknown slot '" + authored_slot_name + "' after normalization to '"
            + out_mapping.slot_name + "'; keeping authored value",
          path + ".slot");
      }

      if (node.contains("scale")) {
        out_mapping.scale[0] = node["scale"][0].get<float>();
        out_mapping.scale[1] = node["scale"][1].get<float>();
      }
      if (node.contains("bias")) {
        out_mapping.bias[0] = node["bias"][0].get<float>();
        out_mapping.bias[1] = node["bias"][1].get<float>();
      }

      const bool has_trigger = node.contains("trigger");
      const bool has_triggers = node.contains("triggers");

      if (has_trigger) {
        const auto type
          = ParseTriggerType(node.at("trigger").get<std::string>());
        if (!type.has_value()) {
          AddDiagnostic(session, request, ImportSeverity::kError,
            "input.schema.contract_mismatch",
            "Unknown trigger shorthand '"
              + node.at("trigger").get<std::string>() + "'",
            path + ".trigger");
          return false;
        }
        out_mapping.triggers.push_back(TriggerSource {
          .type = *type,
          .behavior = InputTriggerBehavior::kImplicit,
          .actuation_threshold = 0.5F,
        });
      } else if (has_triggers) {
        for (size_t i = 0; i < node["triggers"].size(); ++i) {
          auto trigger = TriggerSource {};
          if (!parse_trigger(node["triggers"][i],
                path + ".triggers[" + std::to_string(i) + "]", trigger)) {
            return false;
          }
          out_mapping.triggers.push_back(std::move(trigger));
        }
      }
      return true;
    };

    const auto parse_context = [&](const json& node, const std::string& path,
                                 ContextSource& out_context) {
      out_context.name = node.at("name").get<std::string>();
      out_context.auto_load = node.value("auto_load", false);
      out_context.auto_activate = node.value("auto_activate", false);
      if (node.contains("priority")) {
        out_context.default_priority = node.at("priority").get<int32_t>();
      }
      const auto& mappings = node.at("mappings");
      for (size_t i = 0; i < mappings.size(); ++i) {
        auto mapping = MappingSource {};
        if (!parse_mapping(mappings[i],
              path + ".mappings[" + std::to_string(i) + "]", mapping)) {
          return false;
        }
        out_context.mappings.push_back(std::move(mapping));
      }
      return true;
    };

    bool parse_ok = true;
    try {
      if (is_standalone) {
        parse_ok = parse_action(doc, "$");
      } else {
        if (doc.contains("actions")) {
          for (size_t i = 0; i < doc["actions"].size(); ++i) {
            parse_ok = parse_action(doc["actions"][i],
                         "$.actions[" + std::to_string(i) + "]")
              && parse_ok;
          }
        }

        auto context_names = std::unordered_set<std::string> {};
        const auto& contexts = doc.at("contexts");
        for (size_t i = 0; i < contexts.size(); ++i) {
          auto context = ContextSource {};
          if (!parse_context(contexts[i],
                "$.contexts[" + std::to_string(i) + "]", context)) {
            parse_ok = false;
            continue;
          }
          if (!context_names.insert(context.name).second) {
            AddDiagnostic(session, request, ImportSeverity::kError,
              "input.context.name_duplicate",
              "Duplicate context name '" + context.name + "'",
              "$.contexts[" + std::to_string(i) + "].name");
            parse_ok = false;
            continue;
          }
          out.context_sources.push_back(std::move(context));
        }
      }
    } catch (const std::exception& ex) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "input.schema.contract_mismatch",
        std::string(
          "Schema validated input document failed invariant extraction: ")
          + ex.what());
      return false;
    }

    return parse_ok;
  }

  auto BuildInputContextAssets(
    const std::vector<ContextSource>& context_sources,
    const std::unordered_map<std::string, data::AssetKey>& local_action_keys,
    const std::unordered_map<std::string, std::pair<data::AssetKey, uint8_t>>&
      mounted_actions,
    ImportSession& session, const ImportRequest& request)
    -> std::optional<std::vector<BuiltContextAsset>>
  {
    const auto resolve_action_key
      = [&](const std::string& action_name,
          const std::string& object_path) -> std::optional<data::AssetKey> {
      if (const auto it = local_action_keys.find(action_name);
        it != local_action_keys.end()) {
        return it->second;
      }
      if (const auto it = mounted_actions.find(action_name);
        it != mounted_actions.end()) {
        return it->second.first;
      }
      AddDiagnostic(session, request, ImportSeverity::kError,
        "input.context.action_unresolved",
        "Could not resolve action reference '" + action_name + "'",
        object_path);
      return std::nullopt;
    };

    auto built_contexts = std::vector<BuiltContextAsset> {};
    built_contexts.reserve(context_sources.size());
    for (const auto& source_ctx : context_sources) {
      auto context = BuiltContextAsset {};
      context.name = source_ctx.name;
      context.flags = InputMappingContextFlags::kNone;
      if (source_ctx.auto_load) {
        context.flags |= InputMappingContextFlags::kAutoLoad;
      }
      if (source_ctx.auto_activate) {
        context.flags |= InputMappingContextFlags::kAutoActivate;
      }
      context.default_priority = source_ctx.default_priority;
      context.descriptor_relpath
        = request.loose_cooked_layout.InputMappingContextDescriptorRelPath(
          context.name);
      context.virtual_path
        = request.loose_cooked_layout.InputMappingContextVirtualPath(
          context.name);
      context.key = MakeAssetKey(request, context.virtual_path);

      auto slot_offsets = std::unordered_map<std::string, uint32_t> {};
      const auto intern_slot
        = [&](const std::string& slot_name) -> std::optional<uint32_t> {
        if (const auto it = slot_offsets.find(slot_name);
          it != slot_offsets.end()) {
          return it->second;
        }
        if (context.strings.size()
          > (std::numeric_limits<uint32_t>::max)() - (slot_name.size() + 1U)) {
          AddDiagnostic(session, request, ImportSeverity::kError,
            "input.context.string_table_overflow",
            "Input mapping context string table exceeded uint32 limits",
            "contexts." + context.name);
          return std::nullopt;
        }
        const auto offset = static_cast<uint32_t>(context.strings.size());
        context.strings.insert(
          context.strings.end(), slot_name.begin(), slot_name.end());
        context.strings.push_back('\0');
        slot_offsets.emplace(slot_name, offset);
        return offset;
      };

      bool build_ok = true;
      for (size_t i = 0; i < source_ctx.mappings.size(); ++i) {
        const auto& source_mapping = source_ctx.mappings[i];
        const auto mapping_path
          = "contexts." + context.name + ".mappings[" + std::to_string(i) + "]";
        const auto action_key = resolve_action_key(
          source_mapping.action_name, mapping_path + ".action");
        const auto slot_offset = intern_slot(source_mapping.slot_name);
        if (!action_key.has_value() || !slot_offset.has_value()) {
          build_ok = false;
          break;
        }

        if (context.triggers.size() > (std::numeric_limits<uint32_t>::max)()) {
          AddDiagnostic(session, request, ImportSeverity::kError,
            "input.context.trigger_count_overflow",
            "Trigger table exceeded uint32 limits", mapping_path);
          build_ok = false;
          break;
        }
        const auto trigger_start
          = static_cast<uint32_t>(context.triggers.size());

        for (size_t t = 0; t < source_mapping.triggers.size(); ++t) {
          const auto& source_trigger = source_mapping.triggers[t];
          const auto trigger_path
            = mapping_path + ".triggers[" + std::to_string(t) + "]";
          auto trigger = InputTriggerRecord {};
          trigger.type = source_trigger.type;
          trigger.behavior = source_trigger.behavior;
          trigger.actuation_threshold = source_trigger.actuation_threshold;
          if (source_trigger.has_hold_time) {
            trigger.fparams[0] = source_trigger.hold_time;
          }
          if (source_trigger.has_interval) {
            trigger.fparams[1] = source_trigger.interval;
          }
          if (source_trigger.linked_action_name.has_value()) {
            const auto linked
              = resolve_action_key(*source_trigger.linked_action_name,
                trigger_path + ".chord_action");
            if (!linked.has_value()) {
              build_ok = false;
              break;
            }
            trigger.linked_action_asset_key = *linked;
          }

          if (source_trigger.type == InputTriggerType::kCombo) {
            trigger.aux_start_index
              = static_cast<uint32_t>(context.trigger_aux.size());
            trigger.aux_count
              = static_cast<uint32_t>(source_trigger.aux.size());
            for (size_t a = 0; a < source_trigger.aux.size(); ++a) {
              const auto& source_aux = source_trigger.aux[a];
              const auto aux_key = resolve_action_key(source_aux.action_name,
                trigger_path + ".aux[" + std::to_string(a) + "].action");
              if (!aux_key.has_value()) {
                build_ok = false;
                break;
              }
              context.trigger_aux.push_back(InputTriggerAuxRecord {
                .action_asset_key = *aux_key,
                .completion_states = source_aux.completion_states,
                .time_to_complete_ns = source_aux.time_to_complete_ns,
                .flags = 0,
              });
            }
          }

          if (!build_ok) {
            break;
          }
          context.triggers.push_back(trigger);
        }

        if (!build_ok) {
          break;
        }

        const auto trigger_end = static_cast<uint32_t>(context.triggers.size());
        auto mapping = InputActionMappingRecord {};
        mapping.action_asset_key = *action_key;
        mapping.slot_name_offset = *slot_offset;
        mapping.trigger_start_index = trigger_start;
        mapping.trigger_count = trigger_end - trigger_start;
        mapping.scale[0] = source_mapping.scale[0];
        mapping.scale[1] = source_mapping.scale[1];
        mapping.bias[0] = source_mapping.bias[0];
        mapping.bias[1] = source_mapping.bias[1];
        context.mappings.push_back(mapping);
      }

      if (!build_ok) {
        return std::nullopt;
      }
      built_contexts.push_back(std::move(context));
    }

    return built_contexts;
  }

  auto EmitInputActions(const std::vector<DeclaredAction>& actions,
    const bool hashing, ImportSession& session, const ImportRequest& request)
    -> bool
  {
    for (const auto& action : actions) {
      const auto bytes = SerializeAction(action, hashing);
      if (!bytes.has_value()) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "input.asset.serialize_failed",
          "Failed to serialize input action '" + action.name + "'");
        return false;
      }
      try {
        session.AssetEmitter().Emit(action.key, AssetType::kInputAction,
          action.virtual_path, action.descriptor_relpath, *bytes);
      } catch (const std::exception& ex) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "input.asset.descriptor_emit_failed", ex.what(),
          "actions." + action.name);
        return false;
      }
    }
    return true;
  }

  auto EmitInputContexts(const std::vector<BuiltContextAsset>& contexts,
    const bool hashing, ImportSession& session, const ImportRequest& request)
    -> bool
  {
    for (const auto& context : contexts) {
      auto error = std::string {};
      const auto bytes = SerializeContext(context, hashing, error);
      if (!bytes.has_value()) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "input.context.serialize_failed", error, "contexts." + context.name);
        return false;
      }
      try {
        session.AssetEmitter().Emit(context.key,
          AssetType::kInputMappingContext, context.virtual_path,
          context.descriptor_relpath, *bytes);
      } catch (const std::exception& ex) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "input.context.descriptor_emit_failed", ex.what(),
          "contexts." + context.name);
        return false;
      }
    }
    return true;
  }

} // namespace

InputImportPipeline::InputImportPipeline()
  : InputImportPipeline(Config {})
{
}

InputImportPipeline::InputImportPipeline(Config config)
  : config_(config)
  , input_channel_(config.queue_capacity)
  , output_channel_(config.queue_capacity)
{
}

InputImportPipeline::~InputImportPipeline()
{
  if (started_) {
    DLOG_IF_F(WARNING, HasPending(),
      "InputImportPipeline destroyed with {} pending items", PendingCount());
  }
  input_channel_.Close();
  output_channel_.Close();
}

auto InputImportPipeline::Start(co::Nursery& nursery) -> void
{
  DCHECK_F(!started_, "InputImportPipeline::Start() called more than once");
  started_ = true;
  const auto worker_count = std::max(1U, config_.worker_count);
  for (uint32_t i = 0; i < worker_count; ++i) {
    nursery.Start([this]() -> co::Co<> { co_await Worker(); });
  }
}

auto InputImportPipeline::Submit(WorkItem item) -> co::Co<>
{
  pending_.fetch_add(1, std::memory_order_acq_rel);
  submitted_.fetch_add(1, std::memory_order_acq_rel);
  co_await input_channel_.Send(std::move(item));
}

auto InputImportPipeline::TrySubmit(WorkItem item) -> bool
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

auto InputImportPipeline::Collect() -> co::Co<WorkResult>
{
  auto maybe_result = co_await output_channel_.Receive();
  if (!maybe_result.has_value()) {
    co_return WorkResult {
      .source_id = {},
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

auto InputImportPipeline::Close() -> void { input_channel_.Close(); }

auto InputImportPipeline::HasPending() const noexcept -> bool
{
  return pending_.load(std::memory_order_acquire) > 0;
}

auto InputImportPipeline::PendingCount() const noexcept -> size_t
{
  return pending_.load(std::memory_order_acquire);
}

auto InputImportPipeline::GetProgress() const noexcept -> PipelineProgress
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

auto InputImportPipeline::Worker() -> co::Co<>
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
    if (IsStopRequested(item.stop_token)) {
      co_await ReportCancelled(std::move(item));
      continue;
    }

    const auto start = std::chrono::steady_clock::now();
    auto result = WorkResult {
      .source_id = item.source_id,
      .telemetry = {},
      .success = false,
    };

    try {
      result.success = co_await Process(item);
    } catch (const std::exception& ex) {
      if (item.session != nullptr) {
        AddDiagnostic(*item.session, item.session->Request(),
          ImportSeverity::kError, "input.import.pipeline_exception",
          std::string("Unhandled exception in InputImportPipeline: ")
            + ex.what());
      }
      result.success = false;
    }

    result.telemetry.cook_duration
      = MakeDuration(start, std::chrono::steady_clock::now());
    co_await output_channel_.Send(std::move(result));
  }

  co_return;
}

auto InputImportPipeline::Process(WorkItem& item) -> co::Co<bool>
{
  auto* const session = item.session.get();
  if (session == nullptr) {
    co_return false;
  }

  const auto& request = session->Request();
  if (!request.input.has_value()) {
    AddDiagnostic(*session, request, ImportSeverity::kError,
      "input.request.invalid_job_type",
      "Input import pipeline requires request input payload to be set");
    co_return false;
  }

  std::string source_text {};
  source_text.resize(item.source_bytes.size());
  if (!item.source_bytes.empty()) {
    std::memcpy(
      source_text.data(), item.source_bytes.data(), item.source_bytes.size());
  }

  json doc {};
  try {
    doc = json::parse(source_text);
  } catch (const std::exception& ex) {
    AddDiagnostic(*session, request, ImportSeverity::kError,
      "input.import.parse_failed",
      std::string("Failed to parse input JSON: ") + ex.what());
    co_return false;
  }

  const auto document_kind = ValidateInputSchema(doc, *session, request);
  if (!document_kind.has_value()) {
    co_return false;
  }
  const auto known_slots = BuildKnownSlotNames();
  const auto mounted_actions = BuildMountedActions(*session, request);
  auto parsed_source = ParsedInputSource {};
  if (!ParseInputSourceDocument(doc,
        *document_kind == InputDocumentKind::kStandaloneAction, *session,
        request, known_slots, mounted_actions, parsed_source)) {
    co_return false;
  }

  auto built_contexts = BuildInputContextAssets(parsed_source.context_sources,
    parsed_source.local_action_keys, mounted_actions, *session, request);
  if (!built_contexts.has_value()) {
    co_return false;
  }

  const auto hashing
    = EffectiveContentHashingEnabled(request.options.with_content_hashing);
  if (!EmitInputActions(
        parsed_source.declared_actions, hashing, *session, request)) {
    co_return false;
  }
  if (!EmitInputContexts(*built_contexts, hashing, *session, request)) {
    co_return false;
  }

  co_return !session->HasErrors();
}

auto InputImportPipeline::ReportCancelled(WorkItem item) -> co::Co<>
{
  co_await output_channel_.Send(WorkResult {
    .source_id = std::move(item.source_id),
    .telemetry = {},
    .success = false,
  });
}

} // namespace oxygen::content::import
