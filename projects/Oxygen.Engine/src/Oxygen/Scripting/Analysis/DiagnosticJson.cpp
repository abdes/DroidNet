//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Scripting/Analysis/DiagnosticCatalog.h>
#include <Oxygen/Scripting/Analysis/DiagnosticJson.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>

namespace oxygen::scripting {

namespace {

  using nlohmann::json;

  auto PositionToJson(const SourcePosition& position) -> json
  {
    return json { { "line", position.line }, { "column", position.column } };
  }

  auto SpanToJson(const SourceSpan& span) -> json
  {
    return json {
      { "source", span.source_name },
      { "begin", PositionToJson(span.begin) },
      { "end", PositionToJson(span.end) },
    };
  }

  auto PositionFromJson(const json& value) -> SourcePosition
  {
    return SourcePosition {
      .line = value.at("line").get<uint32_t>(),
      .column = value.at("column").get<uint32_t>(),
    };
  }

  auto SpanFromJson(const json& value) -> SourceSpan
  {
    return SourceSpan {
      .source_name = value.at("source").get<std::string>(),
      .begin = PositionFromJson(value.at("begin")),
      .end = PositionFromJson(value.at("end")),
    };
  }

  auto SeverityFromJson(const std::string_view text)
    -> std::optional<DiagnosticSeverity>
  {
    for (const auto severity : {
           DiagnosticSeverity::kError,
           DiagnosticSeverity::kWarning,
           DiagnosticSeverity::kNote,
         }) {
      if (to_string(severity) == text) {
        return severity;
      }
    }
    return std::nullopt;
  }

  auto DiagnosticFromJson(const json& value)
    -> std::expected<ScriptDiagnostic, std::string>
  {
    if (const auto version = value.at("version").get<uint32_t>();
      version != kScriptDiagnosticJsonVersion) {
      return std::unexpected(
        fmt::format("unsupported diagnostic version {}", version));
    }
    const auto severity_text = value.at("severity").get<std::string>();
    const auto severity = SeverityFromJson(severity_text);
    if (!severity.has_value()) {
      return std::unexpected(
        fmt::format("unknown severity '{}'", severity_text));
    }
    const auto code_text = value.at("code").get<std::string>();
    const auto code = ParseDiagnosticCode(code_text);
    if (!code.has_value()) {
      return std::unexpected(fmt::format("unknown code '{}'", code_text));
    }

    auto diagnostic = ScriptDiagnostic {
      .severity = *severity,
      .code = *code,
      .message = value.at("message").get<std::string>(),
      .labels = {},
      .notes = value.at("notes").get<std::vector<std::string>>(),
      .help = std::nullopt,
      .slot = std::nullopt,
    };
    for (const auto& label : value.at("labels")) {
      diagnostic.labels.push_back(DiagnosticLabel {
        .span = SpanFromJson(label.at("span")),
        .message = label.at("message").get<std::string>(),
        .primary = label.at("primary").get<bool>(),
      });
    }
    if (const auto help = value.find("help"); help != value.end()) {
      auto suggestion = Suggestion {
        .message = help->at("message").get<std::string>(),
        .span = std::nullopt,
        .replacement = std::nullopt,
      };
      if (const auto span = help->find("span"); span != help->end()) {
        suggestion.span = SpanFromJson(*span);
      }
      if (const auto text = help->find("replacement"); text != help->end()) {
        suggestion.replacement = text->get<std::string>();
      }
      diagnostic.help = std::move(suggestion);
    }
    if (const auto slot = value.find("slot"); slot != value.end()) {
      diagnostic.slot = SlotContext {
        .node = slot->at("node").get<std::string>(),
        .slot_index = slot->at("slot_index").get<uint32_t>(),
        .hook = slot->at("hook").get<std::string>(),
      };
    }
    return diagnostic;
  }

} // namespace

auto ToJson(const ScriptDiagnostic& diagnostic) -> std::string
{
  auto labels = json::array();
  for (const auto& label : diagnostic.labels) {
    labels.push_back(json {
      { "span", SpanToJson(label.span) },
      { "message", label.message },
      { "primary", label.primary },
    });
  }
  auto value = json {
    { "version", kScriptDiagnosticJsonVersion },
    { "severity", to_string(diagnostic.severity) },
    { "code", to_string(diagnostic.code) },
    { "family", to_string(FamilyOf(diagnostic.code)) },
    { "message", diagnostic.message },
    { "labels", std::move(labels) },
    { "notes", diagnostic.notes },
  };
  if (const auto& help = diagnostic.help; help.has_value()) {
    auto help_value = json { { "message", help->message } };
    if (help->span.has_value()) {
      help_value.emplace("span", SpanToJson(*help->span));
    }
    if (help->replacement.has_value()) {
      help_value.emplace("replacement", *help->replacement);
    }
    value.emplace("help", std::move(help_value));
  }
  if (const auto& slot = diagnostic.slot; slot.has_value()) {
    value.emplace("slot",
      json {
        { "node", slot->node },
        { "slot_index", slot->slot_index },
        { "hook", slot->hook },
      });
  }
  return value.dump();
}

auto ParseDiagnosticJson(const std::string_view json_text)
  -> std::expected<ScriptDiagnostic, std::string>
{
  try {
    return DiagnosticFromJson(json::parse(json_text));
  } catch (const json::exception& error) {
    return std::unexpected(std::string(error.what()));
  }
}

} // namespace oxygen::scripting
